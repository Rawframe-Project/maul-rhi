// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's queues, seen from D3D12's side: the suite wraps the
// command queues' ExecuteCommandLists, Wait and Signal and the device's
// CreateCommandList, patching the runtime's tables in this process, and
// puts them back after each check. A frame of a transfer, an async
// compute and a graphics pass runs on the copy, compute and direct
// queues, each after a wait for the queue before; a transfer pass
// writing a texture runs on the compute queue, and one reading a texture
// a graphics pass left in a shader read state on the direct queue; an
// async compute pass on the direct queue waits for the one before it on
// the compute queue; a frame whose compute list D3D12 makes not fails
// without running anything, and the next runs; and a frame whose last
// signal fails, its work held behind a fence of the suite's, is waited
// for before its device goes, so nothing the GPU still reads is
// released. A transfer pass that clears runs on the compute queue, and
// clears past a slot's views zero their words too. Under the debug layer,
// whose errors fail the suite. Skipped where there is no D3D12 adapter,
// unless MAUL_RHI_REQUIRE_D3D12 is set.

// getenv is standard C; MSVC's runtime deprecates it for its own.
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS

#include "d3d12_debug.h"
#include "test_harness.h"

#include "maul-rhi/d3d12.h"
#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "shaders/noop_container.h"

#include <directx/d3d12.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// A call a queue took: what it was, the queue's kind, and a wait's or
// signal's fence and value.
typedef enum CallKind
{
    CALL_EXECUTE,
    CALL_WAIT,
    CALL_SIGNAL,
} CallKind;

typedef struct Call
{
    CallKind kind;
    D3D12_COMMAND_LIST_TYPE queue;
    ID3D12Fence* fence;
    uint64_t value;
} Call;

#define CALLS 256

typedef void(STDMETHODCALLTYPE* ExecuteFn)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
typedef HRESULT(STDMETHODCALLTYPE* FenceFn)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
typedef HRESULT(STDMETHODCALLTYPE* FrequencyFn)(ID3D12CommandQueue*, UINT64*);
typedef HRESULT(STDMETHODCALLTYPE* SetEventFn)(ID3D12Fence*, UINT64, HANDLE);
typedef HRESULT(STDMETHODCALLTYPE* CreateListFn)(ID3D12Device*, UINT, D3D12_COMMAND_LIST_TYPE,
                                                 ID3D12CommandAllocator*, ID3D12PipelineState*,
                                                 REFIID, void**);

// The tables patched, their entries as they were, and the calls seen
// since the hooks went in.
static ID3D12CommandQueueVtbl* s_queueTable;
static ID3D12DeviceVtbl* s_deviceTable;
static ID3D12FenceVtbl* s_fenceTable;
static ExecuteFn s_execute;
static FenceFn s_wait;
static FenceFn s_signal;
static FrequencyFn s_frequency;
static CreateListFn s_createList;
static SetEventFn s_setEvent;
static Call s_calls[CALLS];
static uint32_t s_callCount;
// While set, D3D12 makes no compute list; and a signal of the queue of
// kind s_failQueue fails, after s_failAfter of them go in, its fence and
// value kept in s_failedFence and s_failedValue.
static bool s_failCompute;
static bool s_failSignal;
// While set, a compute queue's timestamps tick one more a second than
// D3D12 says.
static bool s_skewCompute;
static D3D12_COMMAND_LIST_TYPE s_failQueue;
static uint32_t s_failAfter;
static ID3D12Fence* s_failedFence;
static uint64_t s_failedValue;
// While set, each kind of queue first waits for this fence to reach 1,
// before its first call; the fence is signalled from the CPU when a
// fence's event is first set, that is when the library first waits.
static ID3D12Fence* s_hold;
static bool s_held[4];
// While set, the direct queue's next signal that does not fail goes in
// and then this fence is signalled to 1, the fence and value it took kept in s_drained and
// s_drainedValue, with a reference.
static ID3D12Fence* s_release;
static ID3D12Fence* s_drained;
static uint64_t s_drainedValue;

static void Note(ID3D12CommandQueue* queue, CallKind kind, ID3D12Fence* fence, uint64_t value)
{
    D3D12_COMMAND_QUEUE_DESC desc;
    (void)ID3D12CommandQueue_GetDesc(queue, &desc);
    if (s_callCount < CALLS)
    {
        s_calls[s_callCount++] =
            (Call){.kind = kind, .queue = desc.Type, .fence = fence, .value = value};
    }
    if (s_hold != nullptr && desc.Type < 4 && !s_held[desc.Type])
    {
        s_held[desc.Type] = true;
        (void)s_wait(queue, s_hold, 1);
    }
}

static void STDMETHODCALLTYPE Execute(ID3D12CommandQueue* queue, UINT count,
                                      ID3D12CommandList* const* lists)
{
    Note(queue, CALL_EXECUTE, nullptr, 0);
    s_execute(queue, count, lists);
}

static HRESULT STDMETHODCALLTYPE Wait(ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64 value)
{
    Note(queue, CALL_WAIT, fence, value);
    return s_wait(queue, fence, value);
}

static HRESULT STDMETHODCALLTYPE Signal(ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64 value)
{
    Note(queue, CALL_SIGNAL, fence, value);
    if (s_failSignal && s_calls[s_callCount - 1].queue == s_failQueue && s_failAfter > 0)
    {
        --s_failAfter;
    }
    else if (s_failSignal && s_calls[s_callCount - 1].queue == s_failQueue)
    {
        s_failSignal = false;
        s_failedFence = fence;
        s_failedValue = value;
        return E_FAIL;
    }
    HRESULT result = s_signal(queue, fence, value);
    if (s_release != nullptr && s_calls[s_callCount - 1].queue == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        ID3D12Fence_AddRef(fence);
        s_drained = fence;
        s_drainedValue = value;
        (void)ID3D12Fence_Signal(s_release, 1);
        s_release = nullptr;
    }
    return result;
}

static HRESULT STDMETHODCALLTYPE Frequency(ID3D12CommandQueue* queue, UINT64* frequency)
{
    HRESULT result = s_frequency(queue, frequency);
    D3D12_COMMAND_QUEUE_DESC desc;
    (void)ID3D12CommandQueue_GetDesc(queue, &desc);
    if (s_skewCompute && SUCCEEDED(result) && desc.Type == D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {
        *frequency += 1;
    }
    return result;
}

static HRESULT STDMETHODCALLTYPE SetEventOn(ID3D12Fence* fence, UINT64 value, HANDLE event)
{
    if (s_hold != nullptr)
    {
        (void)ID3D12Fence_Signal(s_hold, 1);
        s_hold = nullptr;
    }
    return s_setEvent(fence, value, event);
}

static HRESULT STDMETHODCALLTYPE CreateList(ID3D12Device* device, UINT mask,
                                            D3D12_COMMAND_LIST_TYPE type,
                                            ID3D12CommandAllocator* allocator,
                                            ID3D12PipelineState* state, REFIID riid, void** out)
{
    if (s_failCompute && type == D3D12_COMMAND_LIST_TYPE_COMPUTE)
    {
        *out = nullptr;
        return E_OUTOFMEMORY;
    }
    return s_createList(device, mask, type, allocator, state, riid, out);
}

// Writes a table's entry, which lies in the runtime's read-only data.
static void Patch(void* entry, const void* value, size_t size)
{
    DWORD old = 0;
    if (VirtualProtect(entry, size, PAGE_READWRITE, &old))
    {
        memcpy(entry, value, size);
        (void)VirtualProtect(entry, size, old, &old);
    }
}

// Wraps the tables of the device's queues and of the device, and forgets
// the calls seen before.
static void Hook(mrhiDevice* device)
{
    void* native = nullptr;
    void* queue = nullptr;
    CHECK(mrhiDevice_GetD3d12Device(device, &native, &queue) == mrhi_success, "the native objects");
    s_callCount = 0;
    if (native == nullptr || queue == nullptr || s_queueTable != nullptr)
    {
        return;
    }
    s_queueTable = (ID3D12CommandQueueVtbl*)((ID3D12CommandQueue*)queue)->lpVtbl;
    s_deviceTable = (ID3D12DeviceVtbl*)((ID3D12Device*)native)->lpVtbl;
    s_execute = s_queueTable->ExecuteCommandLists;
    s_wait = s_queueTable->Wait;
    s_frequency = s_queueTable->GetTimestampFrequency;
    s_signal = s_queueTable->Signal;
    s_createList = s_deviceTable->CreateCommandList;
    const ExecuteFn execute = Execute;
    const FenceFn wait = Wait;
    const FrequencyFn frequency = Frequency;
    const FenceFn signal = Signal;
    const CreateListFn create = CreateList;
    Patch(&s_queueTable->ExecuteCommandLists, &execute, sizeof(execute));
    Patch(&s_queueTable->Wait, &wait, sizeof(wait));
    Patch(&s_queueTable->GetTimestampFrequency, &frequency, sizeof(frequency));
    Patch(&s_queueTable->Signal, &signal, sizeof(signal));
    Patch(&s_deviceTable->CreateCommandList, &create, sizeof(create));
}

// Holds each queue's first call behind a fence until a fence's event is
// first set, through the fence table, which Unhook puts back.
static void Hold(ID3D12Fence* fence)
{
    if (fence == nullptr || s_fenceTable != nullptr)
    {
        return;
    }
    s_fenceTable = (ID3D12FenceVtbl*)fence->lpVtbl;
    s_setEvent = s_fenceTable->SetEventOnCompletion;
    const SetEventFn setEvent = SetEventOn;
    Patch(&s_fenceTable->SetEventOnCompletion, &setEvent, sizeof(setEvent));
    s_hold = fence;
    memset(s_held, 0, sizeof(s_held));
}

// Puts the tables back as they were; every check ends with it, so a
// failed one leaves the next its own D3D12.
static void Unhook(void)
{
    if (s_fenceTable != nullptr)
    {
        Patch(&s_fenceTable->SetEventOnCompletion, &s_setEvent, sizeof(s_setEvent));
        s_fenceTable = nullptr;
    }
    s_hold = nullptr;
    s_failAfter = 0;
    s_failedFence = nullptr;
    if (s_queueTable == nullptr)
    {
        return;
    }
    Patch(&s_queueTable->ExecuteCommandLists, &s_execute, sizeof(s_execute));
    Patch(&s_queueTable->Wait, &s_wait, sizeof(s_wait));
    Patch(&s_queueTable->GetTimestampFrequency, &s_frequency, sizeof(s_frequency));
    Patch(&s_queueTable->Signal, &s_signal, sizeof(s_signal));
    Patch(&s_deviceTable->CreateCommandList, &s_createList, sizeof(s_createList));
    s_queueTable = nullptr;
    s_deviceTable = nullptr;
    s_failCompute = false;
    s_failSignal = false;
    s_skewCompute = false;
    s_release = nullptr;
}

// The lists a kind of queue ran since the hooks went in.
static uint32_t Runs(D3D12_COMMAND_LIST_TYPE queue)
{
    uint32_t runs = 0;
    for (uint32_t i = 0; i < s_callCount; ++i)
    {
        runs += s_calls[i].kind == CALL_EXECUTE && s_calls[i].queue == queue ? 1 : 0;
    }
    return runs;
}

// Whether a kind of queue waited, before it ran anything, for a value
// another kind of queue signalled before the wait.
static bool FirstRunWaits(D3D12_COMMAND_LIST_TYPE waiter, D3D12_COMMAND_LIST_TYPE signaller)
{
    bool waited = false;
    for (uint32_t i = 0; i < s_callCount; ++i)
    {
        const Call* call = &s_calls[i];
        if (call->queue != waiter)
        {
            continue;
        }
        if (call->kind == CALL_EXECUTE)
        {
            return waited;
        }
        for (uint32_t j = 0; j < i && call->kind == CALL_WAIT; ++j)
        {
            const Call* signal = &s_calls[j];
            waited = waited || (signal->kind == CALL_SIGNAL && signal->queue == signaller &&
                                signal->fence == call->fence && signal->value <= call->value);
        }
    }
    return false;
}

static mrhiAccess Whole(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
}

static mrhiPassId AddPass(mrhiDevice* device, mrhiPassClass passClass, const mrhiAccess* access)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = passClass;
    def.neverCull = true;
    def.accesses = access;
    def.accessCount = 1;
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(device, &def, &pass) == mrhi_success, "a pass");
    return pass;
}

// Whether a kind of queue waited at any point for a value another kind
// of queue signalled before the wait.
static bool Waits(D3D12_COMMAND_LIST_TYPE waiter, D3D12_COMMAND_LIST_TYPE signaller)
{
    for (uint32_t i = 0; i < s_callCount; ++i)
    {
        const Call* call = &s_calls[i];
        for (uint32_t j = 0; j < i && call->kind == CALL_WAIT && call->queue == waiter; ++j)
        {
            const Call* signal = &s_calls[j];
            if (signal->kind == CALL_SIGNAL && signal->queue == signaller &&
                signal->fence == call->fence && signal->value <= call->value)
            {
                return true;
            }
        }
    }
    return false;
}

// Whether any queue waited for a fence to reach a value or past it.
static bool WaitedFor(const ID3D12Fence* fence, uint64_t value)
{
    for (uint32_t i = 0; i < s_callCount; ++i)
    {
        if (s_calls[i].kind == CALL_WAIT && s_calls[i].fence == fence && s_calls[i].value >= value)
        {
            return true;
        }
    }
    return false;
}

// Records a dispatch of the noop kernel in a pass.
static void Dispatch(mrhiDevice* device, mrhiPassId pass, mrhiComputePipelineId pipeline)
{
    CHECK(mrhiPass_Begin(device, pass) == mrhi_success &&
              mrhiPass_SetComputePipeline(device, pass, pipeline) == mrhi_success &&
              mrhiPass_Dispatch(device, pass, 1, 1, 1) == mrhi_success &&
              mrhiPass_End(device, pass) == mrhi_success,
          "a dispatch");
}

// Prints why a frame did not finish: whether the hooks were in and the
// queues held, and each call the queues took since the hooks went in, a
// wait's or a signal's fence with the value it named. With the hooks in,
// every call noted is this check's, on the device still open, so each
// fence is read too: its completed value beside each call, and for each
// kind of queue the first wait it took whose fence has not reached the
// value, where that queue is stuck. With the hooks out, a call noted
// before the last device went may name a released fence, so none is read.
static void PrintCalls(mrhiRequestId token, mrhiResult waited)
{
    fprintf(stderr, "frame %u.%u not finished within 10 s: %s\n", token.index1, token.generation,
            mrhiGetResultName(waited));
    bool live = s_queueTable != nullptr;
    fprintf(stderr, "  hooks %s, hold %s, held: copy %d, compute %d, direct %d\n",
            live ? "on" : "off", s_hold == nullptr ? "off" : "on",
            s_held[D3D12_COMMAND_LIST_TYPE_COPY], s_held[D3D12_COMMAND_LIST_TYPE_COMPUTE],
            s_held[D3D12_COMMAND_LIST_TYPE_DIRECT]);
    if (s_hold != nullptr)
    {
        fprintf(stderr, "  hold fence %p completed %llu\n", (void*)s_hold,
                (unsigned long long)ID3D12Fence_GetCompletedValue(s_hold));
    }
    static const char* const kinds[] = {"execute", "wait", "signal"};
    bool stuck[4] = {false};
    for (uint32_t i = 0; i < s_callCount; ++i)
    {
        const Call* call = &s_calls[i];
        fprintf(stderr, "  %u: %s on queue type %d, fence %p, value %llu", i, kinds[call->kind],
                (int)call->queue, (void*)call->fence, (unsigned long long)call->value);
        if (!live || call->fence == nullptr)
        {
            fprintf(stderr, "\n");
            continue;
        }
        uint64_t completed = ID3D12Fence_GetCompletedValue(call->fence);
        fprintf(stderr, ", completed %llu\n", (unsigned long long)completed);
        if (call->kind == CALL_WAIT && completed < call->value && call->queue < 4 &&
            !stuck[call->queue])
        {
            stuck[call->queue] = true;
            fprintf(stderr,
                    "  stuck: queue type %d at call %u, waiting for fence %p to reach %llu, "
                    "completed %llu\n",
                    (int)call->queue, i, (void*)call->fence, (unsigned long long)call->value,
                    (unsigned long long)completed);
        }
    }
}

// Submits the frame and waits for it, draining the device's
// notifications.
static void Finish(mrhiDevice* device)
{
    mrhiRequestId token = {0};
    bool submitted = mrhiDevice_SubmitFrame(device, &token) == mrhi_success;
    mrhiResult waited =
        submitted ? mrhiRequest_WaitFrame(device, token, UINT64_C(10000000000)) : mrhi_errorState;
    if (submitted && waited != mrhi_success)
    {
        PrintCalls(token, waited);
    }
    CHECK(submitted && waited == mrhi_success, "submitted and finished");
    mrhiDeviceNotification record;
    while (mrhiDevice_TakeNotification(device, &record) == mrhi_success)
    {
    }
}

// Submits a frame importing a new buffer, whose clear runs on the
// compute queue, so that later frames hold only the passes they check.
static void Zero(mrhiDevice* device, mrhiBufferId buffer)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId resource = {0};
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
              mrhiImportBuffer(device, buffer, &resource) == mrhi_success,
          "the clearing frame");
    Finish(device);
}

static bool Took(mrhiDevice* device, mrhiRequestId request, const void* expected, size_t bytes)
{
    uint8_t taken[64] = {0};
    size_t size = 0;
    return bytes <= sizeof(taken) &&
           mrhiRequest_TakeReadback(device, request, taken, sizeof(taken), &size) == mrhi_success &&
           size == bytes && memcmp(taken, expected, bytes) == 0;
}

static const uint32_t kWords[8] = {1, 2, 3, 4, 5, 6, 7, 8};

// An upload in a transfer pass, a dispatch in an async compute pass and
// a readback in a graphics pass, each following the one before: the
// copy, compute and direct queues each run after a wait for the queue
// before.
static void CheckLanes(mrhiDevice* device, mrhiBufferId buffer, mrhiComputePipelineId pipeline)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId b = {0};
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
              mrhiImportBuffer(device, buffer, &b) == mrhi_success,
          "a frame with the buffer");
    mrhiAccess write = Whole(b, mrhi_accessCopyDestination);
    mrhiAccess storage = Whole(b, mrhi_accessStorageReadWrite);
    mrhiAccess read = Whole(b, mrhi_accessCopySource);
    mrhiPassId upload = AddPass(device, mrhi_passTransfer, &write);
    mrhiPassId compute = AddPass(device, mrhi_passAsyncCompute, &storage);
    mrhiPassId back = AddPass(device, mrhi_passGraphics, &read);
    mrhiRequestId request = {0};
    CHECK(mrhiDevice_CompileFrame(device) == mrhi_success &&
              mrhiPass_Begin(device, upload) == mrhi_success &&
              mrhiPass_WriteBuffer(device, upload, b, 0, kWords, sizeof(kWords)) == mrhi_success &&
              mrhiPass_End(device, upload) == mrhi_success,
          "uploaded");
    Dispatch(device, compute, pipeline);
    CHECK(mrhiPass_Begin(device, back) == mrhi_success &&
              mrhiPass_ReadBuffer(device, back, b, 0, sizeof(kWords), &request) == mrhi_success &&
              mrhiPass_End(device, back) == mrhi_success,
          "read back");
    Hook(device);
    Finish(device);
    CHECK(Runs(D3D12_COMMAND_LIST_TYPE_COPY) > 0 && Runs(D3D12_COMMAND_LIST_TYPE_COMPUTE) > 0 &&
              Runs(D3D12_COMMAND_LIST_TYPE_DIRECT) > 0,
          "work on the copy, compute and direct queues");
    CHECK(FirstRunWaits(D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_TYPE_COPY),
          "the compute queue waits for the copy queue");
    CHECK(FirstRunWaits(D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE),
          "the direct queue waits for the compute queue");
    Unhook();
    CHECK(Took(device, request, kWords, sizeof(kWords)), "the words uploaded");
}

// A texture written in a transfer pass, sampled in a graphics pass and
// read back in a transfer pass: a copy queue takes no texture, and a
// compute list cannot move one out of a pixel shader's state, so the
// upload runs on the compute queue and the readback on the direct one.
static void CheckTextures(mrhiDevice* device, mrhiTextureId texture)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId t = {0};
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
              mrhiImportTexture(device, texture, &t) == mrhi_success,
          "a frame with the texture");
    mrhiAccess write = Whole(t, mrhi_accessCopyDestination);
    mrhiAccess sampled = Whole(t, mrhi_accessSampled);
    mrhiAccess read = Whole(t, mrhi_accessCopySource);
    mrhiPassId upload = AddPass(device, mrhi_passTransfer, &write);
    mrhiPassId sample = AddPass(device, mrhi_passGraphics, &sampled);
    mrhiPassId back = AddPass(device, mrhi_passTransfer, &read);
    const mrhiTextureCopy whole = {.resource = t};
    const mrhiTexelLayout layout = {.bytesPerRow = 8, .rowsPerImage = 2};
    const mrhiExtent3d extent = {2, 2, 1};
    mrhiRequestId request = {0};
    CHECK(mrhiDevice_CompileFrame(device) == mrhi_success &&
              mrhiPass_Begin(device, upload) == mrhi_success &&
              mrhiPass_WriteTexture(device, upload, &whole, kWords, 16, &layout, &extent) ==
                  mrhi_success &&
              mrhiPass_End(device, upload) == mrhi_success &&
              mrhiPass_Begin(device, sample) == mrhi_success &&
              mrhiPass_End(device, sample) == mrhi_success &&
              mrhiPass_Begin(device, back) == mrhi_success &&
              mrhiPass_ReadTexture(device, back, &whole, &extent, &request) == mrhi_success &&
              mrhiPass_End(device, back) == mrhi_success,
          "uploaded, sampled and read back");
    Hook(device);
    Finish(device);
    CHECK(Runs(D3D12_COMMAND_LIST_TYPE_COPY) == 0, "no texture on the copy queue");
    CHECK(Runs(D3D12_COMMAND_LIST_TYPE_COMPUTE) == 1, "the upload alone on the compute queue");
    CHECK(FirstRunWaits(D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE),
          "the direct queue waits for the upload");
    Unhook();
    CHECK(Took(device, request, kWords, 16), "the texels uploaded");
}

// Two async compute passes that share nothing, the second sampling a
// texture and so on the direct queue: it still runs after the first.
static void CheckClassOrder(mrhiDevice* device, mrhiBufferId buffer, mrhiTextureId texture,
                            mrhiComputePipelineId pipeline)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId b = {0};
    mrhiResourceId t = {0};
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
              mrhiImportBuffer(device, buffer, &b) == mrhi_success &&
              mrhiImportTexture(device, texture, &t) == mrhi_success,
          "a frame with the buffer and texture");
    mrhiAccess storage = Whole(b, mrhi_accessStorageReadWrite);
    mrhiAccess sampled = Whole(t, mrhi_accessSampled);
    mrhiPassId first = AddPass(device, mrhi_passAsyncCompute, &storage);
    mrhiPassId second = AddPass(device, mrhi_passAsyncCompute, &sampled);
    CHECK(mrhiDevice_CompileFrame(device) == mrhi_success, "compiled");
    Dispatch(device, first, pipeline);
    Dispatch(device, second, pipeline);
    Hook(device);
    Finish(device);
    CHECK(Runs(D3D12_COMMAND_LIST_TYPE_COMPUTE) == 1, "the first on the compute queue");
    CHECK(FirstRunWaits(D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE),
          "the second after it");
    Unhook();
}

// A frame whose compute list D3D12 makes not fails with nothing run, and
// the same frame then runs.
static void CheckFailedList(mrhiDevice* device, mrhiBufferId buffer, mrhiComputePipelineId pipeline)
{
    for (int run = 0; run < 2; ++run)
    {
        mrhiFrameDef frame = mrhiDefaultFrameDef();
        mrhiResourceId b = {0};
        CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
                  mrhiImportBuffer(device, buffer, &b) == mrhi_success,
              "a frame with the buffer");
        mrhiAccess storage = Whole(b, mrhi_accessStorageReadWrite);
        mrhiPassId compute = AddPass(device, mrhi_passAsyncCompute, &storage);
        CHECK(mrhiDevice_CompileFrame(device) == mrhi_success, "compiled");
        Dispatch(device, compute, pipeline);
        Hook(device);
        if (run == 0)
        {
            s_failCompute = true;
            mrhiRequestId token = {0};
            CHECK(mrhiDevice_SubmitFrame(device, &token) == mrhi_errorCapacity,
                  "no compute list, no frame");
            CHECK(s_callCount == 0, "nothing run");
        }
        else
        {
            Finish(device);
            CHECK(Runs(D3D12_COMMAND_LIST_TYPE_COMPUTE) == 1, "the next frame runs");
        }
        Unhook();
    }
}

// A frame filling a transient and reading it back, queued behind a wait
// for a fence of the suite's, whose last signal fails: the device is
// lost. Its work runs once the fence is signalled, so the frame's lists,
// allocators and transient must outlive it: the failed submit must
// signal the direct queue again and wait for that signal before
// releasing them. The fence is signalled only from inside that signal,
// so a submit that waits for nothing returns with the frame still held.
static void CheckLostFrame(mrhiDevice* device)
{
    void* native = nullptr;
    void* queue = nullptr;
    ID3D12Fence* fence = nullptr;
    CHECK(mrhiDevice_GetD3d12Device(device, &native, &queue) == mrhi_success &&
              SUCCEEDED(ID3D12Device_CreateFence((ID3D12Device*)native, 0, D3D12_FENCE_FLAG_NONE,
                                                 &IID_ID3D12Fence, (void**)&fence)) &&
              SUCCEEDED(ID3D12CommandQueue_Wait((ID3D12CommandQueue*)queue, fence, 1)),
          "the direct queue held behind a fence");
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = sizeof(kWords);
    mrhiResourceId b = {0};
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
              mrhiDeclareBuffer(device, &def, &b) == mrhi_success,
          "a frame with a transient");
    mrhiAccess write = Whole(b, mrhi_accessCopyDestination);
    mrhiAccess read = Whole(b, mrhi_accessCopySource);
    mrhiPassId upload = AddPass(device, mrhi_passGraphics, &write);
    mrhiPassId back = AddPass(device, mrhi_passGraphics, &read);
    mrhiRequestId request = {0};
    CHECK(mrhiDevice_CompileFrame(device) == mrhi_success &&
              mrhiPass_Begin(device, upload) == mrhi_success &&
              mrhiPass_WriteBuffer(device, upload, b, 0, kWords, sizeof(kWords)) == mrhi_success &&
              mrhiPass_End(device, upload) == mrhi_success &&
              mrhiPass_Begin(device, back) == mrhi_success &&
              mrhiPass_ReadBuffer(device, back, b, 0, sizeof(kWords), &request) == mrhi_success &&
              mrhiPass_End(device, back) == mrhi_success,
          "recorded");
    Hook(device);
    s_failSignal = true;
    s_failQueue = D3D12_COMMAND_LIST_TYPE_DIRECT;
    s_release = fence;
    mrhiRequestId token = {0};
    CHECK(mrhiDevice_SubmitFrame(device, &token) == mrhi_errorDeviceLost,
          "the frame's signal failed");
    CHECK(s_drained != nullptr && ID3D12Fence_GetCompletedValue(s_drained) >= s_drainedValue,
          "the failed submit waited for its queued work");
    Unhook();
    if (fence != nullptr)
    {
        // Whatever the queue still holds may run: a teardown with the
        // queue still held does not return.
        (void)ID3D12Fence_Signal(fence, 1);
    }
    mrhiDestroyDevice(device);
    if (s_drained != nullptr)
    {
        ID3D12Fence_Release(s_drained);
        s_drained = nullptr;
    }
    if (fence != nullptr)
    {
        ID3D12Fence_Release(fence);
    }
}

static mrhiDevice* OpenWith(mrhiInstance* instance, mrhiAdapterId adapter, bool timestamps)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    def.features.timestampQuery = timestamps;
    mrhiDevice* device = nullptr;
    mrhiRequestId opened = {0};
    mrhiInstanceNotification record;
    CHECK(mrhiCreateDevice(instance, &def, &device, &opened) == mrhi_success &&
              mrhiInstance_TakeNotification(instance, &record) == mrhi_success &&
              record.outcome == mrhi_success,
          "a device");
    return device;
}

static mrhiDevice* OpenDevice(mrhiInstance* instance, mrhiAdapterId adapter)
{
    return OpenWith(instance, adapter, false);
}

// The noop kernel's pipeline, which takes no tables.
static mrhiComputePipelineId NoopPipeline(mrhiDevice* device, mrhiShaderId* shaderOut)
{
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_noopContainer;
    shaderDef.byteCount = sizeof(s_noopContainer);
    mrhiComputePipelineDef def = mrhiDefaultComputePipelineDef();
    def.entry = "cs";
    def.entryLength = 2;
    mrhiComputePipelineId pipeline = {0};
    mrhiRequestId request = {0};
    mrhiDeviceNotification record;
    CHECK(mrhiCreateShader(device, &shaderDef, shaderOut) == mrhi_success, "the shader");
    def.shader = *shaderOut;
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_success &&
              mrhiDevice_TakeNotification(device, &record) == mrhi_success &&
              record.kind == mrhi_devicePipelineReady && record.outcome == mrhi_success,
          "the pipeline");
    return pipeline;
}

static void CheckDevice(mrhiInstance* instance, mrhiAdapterId adapter, bool failing)
{
    mrhiDevice* device = OpenDevice(instance, adapter);
    if (device == nullptr)
    {
        return;
    }
    mrhiShaderId shader = {0};
    mrhiComputePipelineId pipeline = NoopPipeline(device, &shader);
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = sizeof(kWords);
    bufferDef.usage = mrhi_bufferStorage | mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = 2;
    textureDef.height = 2;
    textureDef.usage = mrhi_textureSampled | mrhi_textureCopySource | mrhi_textureCopyDestination;
    mrhiBufferId buffer = {0};
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateBuffer(device, &bufferDef, &buffer) == mrhi_success &&
              mrhiCreateTexture(device, &textureDef, &texture) == mrhi_success,
          "a buffer and a texture");
    // A new device has made no compute list yet; the buffer's clear,
    // which runs on the compute queue, is in the failing frame.
    if (failing)
    {
        CheckFailedList(device, buffer, pipeline);
    }
    else
    {
        Zero(device, buffer);
        CheckLanes(device, buffer, pipeline);
        CheckTextures(device, texture);
        CheckClassOrder(device, buffer, texture, pipeline);
    }
    CHECK(mrhiDestroyTexture(device, texture) == mrhi_success &&
              mrhiDestroyBuffer(device, buffer) == mrhi_success &&
              mrhiDestroyComputePipeline(device, pipeline) == mrhi_success &&
              mrhiDestroyShader(device, shader) == mrhi_success,
          "the objects ended");
    mrhiDestroyDevice(device);
}

// Five passes alternating async compute and graphics on one buffer,
// each following the one before, with the compute and direct queues held
// until the failed submit first waits: compute signals c1, direct waits
// for it and signals d1, compute waits for that and signals c2, and so
// on. The compute queue's signal after failAfter of them fails. Nothing
// may wait for the failed signal, since the lanes stop queuing there,
// and the submit must return once the queues run.
static void CheckLostLane(mrhiInstance* instance, mrhiAdapterId adapter, uint32_t failAfter)
{
    mrhiDevice* device = OpenDevice(instance, adapter);
    if (device == nullptr)
    {
        return;
    }
    void* native = nullptr;
    void* queue = nullptr;
    ID3D12Fence* hold = nullptr;
    CHECK(mrhiDevice_GetD3d12Device(device, &native, &queue) == mrhi_success &&
              SUCCEEDED(ID3D12Device_CreateFence((ID3D12Device*)native, 0, D3D12_FENCE_FLAG_NONE,
                                                 &IID_ID3D12Fence, (void**)&hold)),
          "a fence to hold the queues");
    mrhiShaderId shader = {0};
    mrhiComputePipelineId pipeline = NoopPipeline(device, &shader);
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = sizeof(kWords);
    def.usage = mrhi_bufferStorage | mrhi_bufferCopyDestination;
    mrhiBufferId buffer = {0};
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId b = {0};
    CHECK(mrhiCreateBuffer(device, &def, &buffer) == mrhi_success &&
              mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
              mrhiImportBuffer(device, buffer, &b) == mrhi_success,
          "a frame with a buffer");
    mrhiAccess storage = Whole(b, mrhi_accessStorageReadWrite);
    mrhiAccess write = Whole(b, mrhi_accessCopyDestination);
    mrhiPassId passes[5];
    for (int i = 0; i < 5; ++i)
    {
        passes[i] = i % 2 == 0 ? AddPass(device, mrhi_passAsyncCompute, &storage)
                               : AddPass(device, mrhi_passGraphics, &write);
    }
    CHECK(mrhiDevice_CompileFrame(device) == mrhi_success, "compiled");
    for (int i = 0; i < 5; ++i)
    {
        if (i % 2 == 0)
        {
            Dispatch(device, passes[i], pipeline);
        }
        else
        {
            CHECK(mrhiPass_Begin(device, passes[i]) == mrhi_success &&
                      mrhiPass_WriteBuffer(device, passes[i], b, 0, kWords, sizeof(kWords)) ==
                          mrhi_success &&
                      mrhiPass_End(device, passes[i]) == mrhi_success,
                  "a write");
        }
    }
    Hook(device);
    Hold(hold);
    s_failSignal = true;
    s_failQueue = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    s_failAfter = failAfter;
    mrhiRequestId token = {0};
    CHECK(mrhiDevice_SubmitFrame(device, &token) == mrhi_errorDeviceLost,
          "the compute queue's signal failed");
    CHECK(s_hold == nullptr, "the queues held until the submit waited");
    CHECK(failAfter == 0 ||
              (Waits(D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE) &&
               Waits(D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_TYPE_DIRECT)),
          "the queues waited for each other before the failed signal");
    CHECK(s_failedFence != nullptr && !WaitedFor(s_failedFence, s_failedValue),
          "nothing waited for the failed signal");
    if (s_hold != nullptr)
    {
        // A teardown with a queue still held does not return.
        (void)ID3D12Fence_Signal(s_hold, 1);
    }
    Unhook();
    CHECK(mrhiDestroyBuffer(device, buffer) == mrhi_success &&
              mrhiDestroyComputePipeline(device, pipeline) == mrhi_success &&
              mrhiDestroyShader(device, shader) == mrhi_success,
          "the objects ended");
    mrhiDestroyDevice(device);
    if (hold != nullptr)
    {
        ID3D12Fence_Release(hold);
    }
}

// A device buffer of a size and usage, imported into the open frame.
// Makes buffers of the usages given and submits the frame that clears
// them, so that a later frame holds no pass of the library's.
static void Cleared(mrhiDevice* device, const mrhiBufferUsage* usages, mrhiBufferId* objects,
                    int count)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success, "the clearing frame");
    for (int i = 0; i < count; ++i)
    {
        mrhiBufferDef def = mrhiDefaultBufferDef();
        def.size = 256;
        def.usage = usages[i];
        mrhiResourceId resource = {0};
        CHECK(mrhiCreateBuffer(device, &def, &objects[i]) == mrhi_success &&
                  mrhiImportBuffer(device, objects[i], &resource) == mrhi_success,
              "a buffer cleared");
    }
    Finish(device);
}

static mrhiResourceId Imported(mrhiDevice* device, mrhiBufferId object)
{
    mrhiResourceId resource = {0};
    CHECK(mrhiImportBuffer(device, object, &resource) == mrhi_success, "an imported buffer");
    return resource;
}

// A pass of a class with one access, timed by two queries of a set when
// it is given.
static mrhiPassId Timed(mrhiDevice* device, mrhiPassClass passClass, mrhiAccess access,
                        mrhiQuerySetId set, uint32_t begin)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = passClass;
    def.neverCull = true;
    def.accesses = &access;
    def.accessCount = 1;
    def.timestampQuerySet = set;
    def.timestampBegin = set.index1 == 0 ? MRHI_NO_QUERY : begin;
    def.timestampEnd = set.index1 == 0 ? MRHI_NO_QUERY : begin + 1;
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(device, &def, &pass) == mrhi_success, "a pass");
    return pass;
}

// Whether the compute and direct queues of a device's D3D12 device tick
// their timestamps alike, as a compute queue of the test's own reads it.
static bool SameFrequencies(mrhiDevice* device)
{
    void* native = nullptr;
    void* queue = nullptr;
    ID3D12CommandQueue* compute = nullptr;
    const D3D12_COMMAND_QUEUE_DESC desc = {.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE};
    UINT64 direct = 0;
    UINT64 computed = 0;
    bool read =
        mrhiDevice_GetD3d12Device(device, &native, &queue) == mrhi_success &&
        SUCCEEDED(ID3D12Device_CreateCommandQueue((ID3D12Device*)native, &desc,
                                                  &IID_ID3D12CommandQueue, (void**)&compute)) &&
        SUCCEEDED(ID3D12CommandQueue_GetTimestampFrequency((ID3D12CommandQueue*)queue, &direct)) &&
        SUCCEEDED(ID3D12CommandQueue_GetTimestampFrequency(compute, &computed));
    CHECK(read, "both queues' timestamp frequencies");
    printf("timestamp frequencies: direct %llu, compute %llu\n", (unsigned long long)direct,
           (unsigned long long)computed);
    if (compute != nullptr)
    {
        ID3D12CommandQueue_Release(compute);
    }
    return read && direct == computed;
}

// A frame with a pass of a class timed by queries 0 and 1, uploading a
// buffer or, for an async compute pass, dispatching over it, and with
// beside set an untimed transfer pass uploading another buffer; then a
// graphics pass resolving the set and one reading the times back. The
// times are checked; the calls the queues took stay in the log.
static void TimeFrame(mrhiDevice* device, mrhiPassClass passClass, mrhiComputePipelineId pipeline,
                      bool beside)
{
    mrhiQuerySetDef setDef = mrhiDefaultQuerySetDef();
    setDef.type = mrhi_queryTimestamp;
    setDef.count = 2;
    mrhiQuerySetId set = {0};
    CHECK(mrhiCreateQuerySet(device, &setDef, &set) == mrhi_success, "a timestamp set");
    bool dispatch = passClass == mrhi_passAsyncCompute;
    const mrhiBufferUsage usages[3] = {
        dispatch ? mrhi_bufferStorage : mrhi_bufferCopyDestination,
        mrhi_bufferCopyDestination,
        mrhi_bufferQueryResolve | mrhi_bufferCopySource,
    };
    mrhiBufferId objects[3] = {0};
    Cleared(device, usages, objects, 3);
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success, "a frame");
    mrhiResourceId timed = Imported(device, objects[0]);
    mrhiResourceId other = Imported(device, objects[1]);
    mrhiResourceId times = Imported(device, objects[2]);
    mrhiPassId work = Timed(
        device, passClass,
        Whole(timed, dispatch ? mrhi_accessStorageReadWrite : mrhi_accessCopyDestination), set, 0);
    mrhiPassId untimed = {0};
    if (beside)
    {
        untimed = Timed(device, mrhi_passTransfer, Whole(other, mrhi_accessCopyDestination),
                        (mrhiQuerySetId){0}, 0);
    }
    mrhiPassId resolve = Timed(device, mrhi_passGraphics, Whole(times, mrhi_accessQueryResolve),
                               (mrhiQuerySetId){0}, 0);
    mrhiPassId back = Timed(device, mrhi_passGraphics, Whole(times, mrhi_accessCopySource),
                            (mrhiQuerySetId){0}, 0);
    CHECK(mrhiDevice_CompileFrame(device) == mrhi_success, "compiled");
    if (dispatch)
    {
        Dispatch(device, work, pipeline);
    }
    else
    {
        CHECK(mrhiPass_Begin(device, work) == mrhi_success &&
                  mrhiPass_WriteBuffer(device, work, timed, 0, kWords, sizeof(kWords)) ==
                      mrhi_success &&
                  mrhiPass_End(device, work) == mrhi_success,
              "a timed upload");
    }
    CHECK(!beside || (mrhiPass_Begin(device, untimed) == mrhi_success &&
                      mrhiPass_WriteBuffer(device, untimed, other, 0, kWords, sizeof(kWords)) ==
                          mrhi_success &&
                      mrhiPass_End(device, untimed) == mrhi_success),
          "an untimed upload");
    mrhiRequestId request = {0};
    CHECK(mrhiPass_Begin(device, resolve) == mrhi_success &&
              mrhiPass_ResolveQueries(device, resolve, set, 0, 2, times, 0) == mrhi_success &&
              mrhiPass_End(device, resolve) == mrhi_success &&
              mrhiPass_Begin(device, back) == mrhi_success &&
              mrhiPass_ReadBuffer(device, back, times, 0, 16, &request) == mrhi_success &&
              mrhiPass_End(device, back) == mrhi_success,
          "the resolve and the readback");
    Hook(device);
    Finish(device);
    uint64_t ticks[2] = {0};
    size_t size = 0;
    CHECK(mrhiRequest_TakeReadback(device, request, ticks, sizeof(ticks), &size) == mrhi_success &&
              size == sizeof(ticks) && ticks[0] != 0 && ticks[1] >= ticks[0],
          "the pass's times");
    for (int i = 0; i < 3; ++i)
    {
        CHECK(mrhiDestroyBuffer(device, objects[i]) == mrhi_success, "a buffer ended");
    }
    CHECK(mrhiDestroyQuerySet(device, set) == mrhi_success, "the set ended");
}

// Timed passes on a device with timestamps: where the compute queue ticks
// as the direct queue does, a timed transfer pass runs on the compute
// queue while an untimed one in the same frame runs on the copy queue,
// a timed async compute pass on the compute queue, and the resolve on the
// direct queue after a wait for the compute queue. A second device whose
// compute queue is made to tick otherwise runs a timed transfer pass,
// alone in its frame, on the direct queue, and an untimed one beside it
// still on the copy queue.
static void CheckTimed(mrhiInstance* instance, mrhiAdapterId adapter)
{
    mrhiDevice* device = OpenWith(instance, adapter, true);
    if (device == nullptr)
    {
        return;
    }
    mrhiShaderId shader = {0};
    mrhiComputePipelineId pipeline = NoopPipeline(device, &shader);
    bool same = SameFrequencies(device);
    TimeFrame(device, mrhi_passTransfer, pipeline, true);
    CHECK(Runs(D3D12_COMMAND_LIST_TYPE_COPY) > 0, "the untimed transfer on the copy queue");
    CHECK(!same || (Runs(D3D12_COMMAND_LIST_TYPE_COMPUTE) > 0 &&
                    Waits(D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE)),
          "the timed transfer on the compute queue, the resolve after it");
    CHECK(same || Runs(D3D12_COMMAND_LIST_TYPE_COMPUTE) == 0,
          "the timed transfer on the direct queue");
    Unhook();
    TimeFrame(device, mrhi_passAsyncCompute, pipeline, true);
    CHECK(!same || (Runs(D3D12_COMMAND_LIST_TYPE_COMPUTE) > 0 &&
                    Waits(D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_COMPUTE)),
          "the timed dispatch on the compute queue, the resolve after it");
    // The table is shared, so the next device's compute queue reads the
    // skewed frequency when it opens.
    s_skewCompute = true;
    mrhiDevice* skewed = OpenWith(instance, adapter, true);
    s_skewCompute = false;
    Unhook();
    if (skewed != nullptr)
    {
        // The timed pass alone, so that no other pass's lists stand in
        // for it on any queue.
        TimeFrame(skewed, mrhi_passTransfer, pipeline, false);
        CHECK(Runs(D3D12_COMMAND_LIST_TYPE_COMPUTE) == 0 && Runs(D3D12_COMMAND_LIST_TYPE_COPY) == 0,
              "a timed transfer on the direct queue where the compute queue ticks otherwise");
        Unhook();
        TimeFrame(skewed, mrhi_passTransfer, pipeline, true);
        CHECK(Runs(D3D12_COMMAND_LIST_TYPE_COMPUTE) == 0 && Runs(D3D12_COMMAND_LIST_TYPE_COPY) > 0,
              "and an untimed one beside it still on the copy queue");
        Unhook();
        mrhiDestroyDevice(skewed);
    }
    CHECK(mrhiDestroyComputePipeline(device, pipeline) == mrhi_success &&
              mrhiDestroyShader(device, shader) == mrhi_success,
          "the objects ended");
    mrhiDestroyDevice(device);
}

// Fills a buffer of words with 1, 2, 3 and on in a transfer pass,
// zeroes the given words of it in another, and reads it back in a
// graphics pass, under the hooks: whether every step was recorded.
static bool ClearFrame(mrhiDevice* device, mrhiBufferId buffer, const uint32_t* words,
                       uint32_t count, uint32_t first, uint32_t cleared, mrhiRequestId* requestOut)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId b = {0};
    if (mrhiDevice_BeginFrame(device, &frame) != mrhi_success ||
        mrhiImportBuffer(device, buffer, &b) != mrhi_success)
    {
        return false;
    }
    mrhiAccess write = Whole(b, mrhi_accessCopyDestination);
    mrhiAccess read = Whole(b, mrhi_accessCopySource);
    mrhiPassId upload = AddPass(device, mrhi_passTransfer, &write);
    mrhiPassId clear = AddPass(device, mrhi_passTransfer, &write);
    mrhiPassId back = AddPass(device, mrhi_passGraphics, &read);
    bool recorded = mrhiDevice_CompileFrame(device) == mrhi_success &&
                    mrhiPass_Begin(device, upload) == mrhi_success &&
                    mrhiPass_WriteBuffer(device, upload, b, 0, words, count * sizeof(uint32_t)) ==
                        mrhi_success &&
                    mrhiPass_End(device, upload) == mrhi_success &&
                    mrhiPass_Begin(device, clear) == mrhi_success;
    // One clear a word, so that each takes a view of its own.
    for (uint32_t i = first; i < first + cleared && recorded; ++i)
    {
        recorded = mrhiPass_ClearBuffer(device, clear, b, i * sizeof(uint32_t), sizeof(uint32_t)) ==
                   mrhi_success;
    }
    recorded = recorded && mrhiPass_End(device, clear) == mrhi_success &&
               mrhiPass_Begin(device, back) == mrhi_success &&
               mrhiPass_ReadBuffer(device, back, b, 0, count * sizeof(uint32_t), requestOut) ==
                   mrhi_success &&
               mrhiPass_End(device, back) == mrhi_success;
    Hook(device);
    Finish(device);
    return recorded;
}

// Clears: a transfer pass that clears runs on the compute queue, whose
// lists take ClearUnorderedAccessViewUint, after the copy queue's upload
// of the words it zeroes some of. On a device of one frame resource,
// whose slots hold 257 clear views, 300 clears of a word each zero the
// words past the views too, with copies.
static void CheckClears(mrhiInstance* instance, mrhiAdapterId adapter)
{
    enum
    {
        WORDS = 300
    };
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    def.budgets.frameResources = 1;
    mrhiDevice* device = nullptr;
    mrhiRequestId opened = {0};
    mrhiInstanceNotification record;
    CHECK(mrhiCreateDevice(instance, &def, &device, &opened) == mrhi_success &&
              mrhiInstance_TakeNotification(instance, &record) == mrhi_success &&
              record.outcome == mrhi_success,
          "a device of one frame resource");
    if (device == nullptr)
    {
        return;
    }
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = WORDS * sizeof(uint32_t);
    bufferDef.usage = mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    mrhiBufferId buffer = {0};
    CHECK(mrhiCreateBuffer(device, &bufferDef, &buffer) == mrhi_success, "a buffer of words");
    Zero(device, buffer);
    static uint32_t words[WORDS];
    for (uint32_t i = 0; i < WORDS; ++i)
    {
        words[i] = i + 1;
    }
    mrhiRequestId request = {0};
    CHECK(ClearFrame(device, buffer, words, 8, 1, 3, &request), "three words cleared");
    CHECK(Runs(D3D12_COMMAND_LIST_TYPE_COPY) == 1 && Runs(D3D12_COMMAND_LIST_TYPE_COMPUTE) == 1 &&
              FirstRunWaits(D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_TYPE_COPY),
          "the upload on the copy queue, the clear on the compute queue after it");
    Unhook();
    const uint32_t expected[8] = {1, 0, 0, 0, 5, 6, 7, 8};
    CHECK(Took(device, request, expected, sizeof(expected)), "the three words zeroed");
    CHECK(ClearFrame(device, buffer, words, WORDS, 0, WORDS, &request), "every word cleared");
    Unhook();
    static uint32_t taken[WORDS];
    size_t size = 0;
    bool zeroed =
        mrhiRequest_TakeReadback(device, request, taken, sizeof(taken), &size) == mrhi_success &&
        size == sizeof(taken);
    for (uint32_t i = 0; i < WORDS && zeroed; ++i)
    {
        zeroed = taken[i] == 0;
    }
    CHECK(zeroed, "every word zeroed, past the clear views too");
    CHECK(mrhiDestroyBuffer(device, buffer) == mrhi_success, "the buffer ended");
    mrhiDestroyDevice(device);
}

int main(void)
{
    // Before the library opens a device, as the debug layer needs.
    mrhiTestWatchD3d12();
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "an instance");
    mrhiAdapterRequestDef request = mrhiDefaultAdapterRequestDef();
    request.allowSoftware = true;
    mrhiRequestId searched = {0};
    mrhiInstanceNotification record;
    mrhiAdapterId adapter = {0};
    size_t count = 0;
    bool found = instance != nullptr &&
                 mrhiInstance_RequestAdapters(instance, &request, &searched) == mrhi_success &&
                 mrhiInstance_TakeNotification(instance, &record) == mrhi_success &&
                 mrhiInstance_GetAdapters(instance, &adapter, 1, &count) == mrhi_success &&
                 count >= 1;
    if (found)
    {
        CheckDevice(instance, adapter, false);
        CheckDevice(instance, adapter, true);
        mrhiDevice* lost = OpenDevice(instance, adapter);
        if (lost != nullptr)
        {
            CheckLostFrame(lost);
        }
        CheckLostLane(instance, adapter, 0);
        CheckLostLane(instance, adapter, 1);
        CheckTimed(instance, adapter);
        CheckClears(instance, adapter);
    }
    else
    {
        const char* required = getenv("MAUL_RHI_REQUIRE_D3D12");
        CHECK(required == nullptr || required[0] == '\0', "D3D12 where required");
        printf("skip: no D3D12 adapter\n");
    }
    mrhiDestroyInstance(instance);
    CHECK(mrhiTestD3d12Errors() == 0, "no D3D12 debug layer error");
    return s_failures == 0 ? 0 : 1;
}
