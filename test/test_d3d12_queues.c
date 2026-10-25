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
// released. Under the debug layer,
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
typedef HRESULT(STDMETHODCALLTYPE* CreateListFn)(ID3D12Device*, UINT, D3D12_COMMAND_LIST_TYPE,
                                                 ID3D12CommandAllocator*, ID3D12PipelineState*,
                                                 REFIID, void**);

// The tables patched, their entries as they were, and the calls seen
// since the hooks went in.
static ID3D12CommandQueueVtbl* s_queueTable;
static ID3D12DeviceVtbl* s_deviceTable;
static ExecuteFn s_execute;
static FenceFn s_wait;
static FenceFn s_signal;
static CreateListFn s_createList;
static Call s_calls[CALLS];
static uint32_t s_callCount;
// While set, D3D12 makes no compute list; and the direct queue's next
// signal fails.
static bool s_failCompute;
static bool s_failSignal;
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
    if (s_failSignal && s_calls[s_callCount - 1].queue == D3D12_COMMAND_LIST_TYPE_DIRECT)
    {
        s_failSignal = false;
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
    CHECK(mrhiGetD3d12Device(device, &native, &queue) == mrhi_success, "the native objects");
    s_callCount = 0;
    if (native == nullptr || queue == nullptr || s_queueTable != nullptr)
    {
        return;
    }
    s_queueTable = (ID3D12CommandQueueVtbl*)((ID3D12CommandQueue*)queue)->lpVtbl;
    s_deviceTable = (ID3D12DeviceVtbl*)((ID3D12Device*)native)->lpVtbl;
    s_execute = s_queueTable->ExecuteCommandLists;
    s_wait = s_queueTable->Wait;
    s_signal = s_queueTable->Signal;
    s_createList = s_deviceTable->CreateCommandList;
    const ExecuteFn execute = Execute;
    const FenceFn wait = Wait;
    const FenceFn signal = Signal;
    const CreateListFn create = CreateList;
    Patch(&s_queueTable->ExecuteCommandLists, &execute, sizeof(execute));
    Patch(&s_queueTable->Wait, &wait, sizeof(wait));
    Patch(&s_queueTable->Signal, &signal, sizeof(signal));
    Patch(&s_deviceTable->CreateCommandList, &create, sizeof(create));
}

// Puts the tables back as they were; every check ends with it, so a
// failed one leaves the next its own D3D12.
static void Unhook(void)
{
    if (s_queueTable == nullptr)
    {
        return;
    }
    Patch(&s_queueTable->ExecuteCommandLists, &s_execute, sizeof(s_execute));
    Patch(&s_queueTable->Wait, &s_wait, sizeof(s_wait));
    Patch(&s_queueTable->Signal, &s_signal, sizeof(s_signal));
    Patch(&s_deviceTable->CreateCommandList, &s_createList, sizeof(s_createList));
    s_queueTable = nullptr;
    s_deviceTable = nullptr;
    s_failCompute = false;
    s_failSignal = false;
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

// Records a dispatch of the noop kernel in a pass.
static void Dispatch(mrhiDevice* device, mrhiPassId pass, mrhiComputePipelineId pipeline)
{
    CHECK(mrhiBeginPass(device, pass) == mrhi_success &&
              mrhiSetComputePipeline(device, pass, pipeline) == mrhi_success &&
              mrhiDispatch(device, pass, 1, 1, 1) == mrhi_success &&
              mrhiEndPass(device, pass) == mrhi_success,
          "a dispatch");
}

// Submits the frame and waits for it, draining the device's
// notifications.
static void Finish(mrhiDevice* device)
{
    mrhiRequestId token = {0};
    CHECK(mrhiSubmitFrame(device, &token) == mrhi_success &&
              mrhiWaitFrame(device, token, UINT64_C(10000000000)) == mrhi_success,
          "submitted and finished");
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(device, &record) == mrhi_success)
    {
    }
}

static bool Took(mrhiDevice* device, mrhiRequestId request, const void* expected, size_t bytes)
{
    uint8_t taken[64] = {0};
    size_t size = 0;
    return bytes <= sizeof(taken) &&
           mrhiTakeReadback(device, request, taken, sizeof(taken), &size) == mrhi_success &&
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
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
              mrhiImportBuffer(device, buffer, &b) == mrhi_success,
          "a frame with the buffer");
    mrhiAccess write = Whole(b, mrhi_accessCopyDestination);
    mrhiAccess storage = Whole(b, mrhi_accessStorageReadWrite);
    mrhiAccess read = Whole(b, mrhi_accessCopySource);
    mrhiPassId upload = AddPass(device, mrhi_passTransfer, &write);
    mrhiPassId compute = AddPass(device, mrhi_passAsyncCompute, &storage);
    mrhiPassId back = AddPass(device, mrhi_passGraphics, &read);
    mrhiRequestId request = {0};
    CHECK(mrhiCompileFrame(device) == mrhi_success &&
              mrhiBeginPass(device, upload) == mrhi_success &&
              mrhiWriteBuffer(device, upload, b, 0, kWords, sizeof(kWords)) == mrhi_success &&
              mrhiEndPass(device, upload) == mrhi_success,
          "uploaded");
    Dispatch(device, compute, pipeline);
    CHECK(mrhiBeginPass(device, back) == mrhi_success &&
              mrhiReadBuffer(device, back, b, 0, sizeof(kWords), &request) == mrhi_success &&
              mrhiEndPass(device, back) == mrhi_success,
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
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
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
    CHECK(mrhiCompileFrame(device) == mrhi_success &&
              mrhiBeginPass(device, upload) == mrhi_success &&
              mrhiWriteTexture(device, upload, &whole, kWords, 16, &layout, &extent) ==
                  mrhi_success &&
              mrhiEndPass(device, upload) == mrhi_success &&
              mrhiBeginPass(device, sample) == mrhi_success &&
              mrhiEndPass(device, sample) == mrhi_success &&
              mrhiBeginPass(device, back) == mrhi_success &&
              mrhiReadTexture(device, back, &whole, &extent, &request) == mrhi_success &&
              mrhiEndPass(device, back) == mrhi_success,
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
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
              mrhiImportBuffer(device, buffer, &b) == mrhi_success &&
              mrhiImportTexture(device, texture, &t) == mrhi_success,
          "a frame with the buffer and texture");
    mrhiAccess storage = Whole(b, mrhi_accessStorageReadWrite);
    mrhiAccess sampled = Whole(t, mrhi_accessSampled);
    mrhiPassId first = AddPass(device, mrhi_passAsyncCompute, &storage);
    mrhiPassId second = AddPass(device, mrhi_passAsyncCompute, &sampled);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
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
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        mrhiFrameDef frame = mrhiDefaultFrameDef();
        mrhiResourceId b = {0};
        CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
                  mrhiImportBuffer(device, buffer, &b) == mrhi_success,
              "a frame with the buffer");
        mrhiAccess storage = Whole(b, mrhi_accessStorageReadWrite);
        mrhiPassId compute = AddPass(device, mrhi_passAsyncCompute, &storage);
        CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
        Dispatch(device, compute, pipeline);
        Hook(device);
        if (attempt == 0)
        {
            s_failCompute = true;
            mrhiRequestId token = {0};
            CHECK(mrhiSubmitFrame(device, &token) == mrhi_errorCapacity,
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
    CHECK(mrhiGetD3d12Device(device, &native, &queue) == mrhi_success &&
              SUCCEEDED(ID3D12Device_CreateFence((ID3D12Device*)native, 0, D3D12_FENCE_FLAG_NONE,
                                                 &IID_ID3D12Fence, (void**)&fence)) &&
              SUCCEEDED(ID3D12CommandQueue_Wait((ID3D12CommandQueue*)queue, fence, 1)),
          "the direct queue held behind a fence");
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = sizeof(kWords);
    mrhiResourceId b = {0};
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
              mrhiDeclareBuffer(device, &def, &b) == mrhi_success,
          "a frame with a transient");
    mrhiAccess write = Whole(b, mrhi_accessCopyDestination);
    mrhiAccess read = Whole(b, mrhi_accessCopySource);
    mrhiPassId upload = AddPass(device, mrhi_passGraphics, &write);
    mrhiPassId back = AddPass(device, mrhi_passGraphics, &read);
    mrhiRequestId request = {0};
    CHECK(mrhiCompileFrame(device) == mrhi_success &&
              mrhiBeginPass(device, upload) == mrhi_success &&
              mrhiWriteBuffer(device, upload, b, 0, kWords, sizeof(kWords)) == mrhi_success &&
              mrhiEndPass(device, upload) == mrhi_success &&
              mrhiBeginPass(device, back) == mrhi_success &&
              mrhiReadBuffer(device, back, b, 0, sizeof(kWords), &request) == mrhi_success &&
              mrhiEndPass(device, back) == mrhi_success,
          "recorded");
    Hook(device);
    s_failSignal = true;
    s_release = fence;
    mrhiRequestId token = {0};
    CHECK(mrhiSubmitFrame(device, &token) == mrhi_errorDeviceLost, "the frame's signal failed");
    CHECK(s_drained != nullptr && ID3D12Fence_GetCompletedValue(s_drained) >= s_drainedValue,
          "the failed submit waited for its queued work");
    Unhook();
    mrhiDestroyDevice(device);
    if (s_drained != nullptr)
    {
        ID3D12Fence_Release(s_drained);
        s_drained = nullptr;
    }
    if (fence != nullptr)
    {
        // Whatever the queue still holds may run.
        (void)ID3D12Fence_Signal(fence, 1);
        ID3D12Fence_Release(fence);
    }
}

static mrhiDevice* OpenDevice(mrhiInstance* instance, mrhiAdapterId adapter)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    mrhiDevice* device = nullptr;
    mrhiRequestId opened = {0};
    mrhiInstanceNotification record;
    CHECK(mrhiCreateDevice(instance, &def, &device, &opened) == mrhi_success &&
              mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
              record.outcome == mrhi_success,
          "a device");
    return device;
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
              mrhiNextDeviceNotification(device, &record) == mrhi_success &&
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
    // A new device has made no compute list yet.
    if (failing)
    {
        CheckFailedList(device, buffer, pipeline);
    }
    else
    {
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
                 mrhiRequestAdapters(instance, &request, &searched) == mrhi_success &&
                 mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
                 mrhiGetAdapters(instance, &adapter, 1, &count) == mrhi_success && count >= 1;
    if (found)
    {
        CheckDevice(instance, adapter, false);
        CheckDevice(instance, adapter, true);
        mrhiDevice* lost = OpenDevice(instance, adapter);
        if (lost != nullptr)
        {
            CheckLostFrame(lost);
        }
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
