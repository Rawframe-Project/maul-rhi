// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's queues, seen from Metal's side: the suite wraps the
// command buffers' commit, event waits and signals and encoder openings,
// swapping the methods of their class in this process, and puts them
// back after each check. A frame of a transfer, an async compute and a
// graphics pass runs on the transfer, async compute and graphics queues,
// each command buffer waiting for the queue before's signal; the next
// frame's first command buffer on a lane waits for every lane's work
// before; a lane's second command buffer waits for its first; a frame of
// graphics passes alone commits one command buffer; and a native pass's
// command buffer comes after one that only waits for what it follows,
// and signals an event that the graphics work after it waits for.
// Every check reads its words back. Skipped where there is no Metal
// adapter, unless MAUL_RHI_REQUIRE_METAL is set.

#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/metal.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "shaders/noop_container.h"

#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The device's label, which its queues carry, the lanes' with their
// names after.
#define LABEL "queues"

enum
{
    LANE_GRAPHICS,
    LANE_COMPUTE,
    LANE_TRANSFER,
    LANES,
};

#define WAITS   8
#define BUFFERS 64

// A command buffer seen since the hooks went in: its lane, the waits
// encoded into it with the encoders opened before each, its signal and
// the encoders opened before it, the encoders it opened by kind, and
// where it was committed among the others (0 for not yet).
typedef struct Seen
{
    const void* buffer;
    int lane;
    uint32_t waitCount;
    const void* waitEvents[WAITS];
    uint64_t waitValues[WAITS];
    uint32_t encodedBeforeWait[WAITS];
    const void* signalEvent;
    uint64_t signal;
    uint32_t encodedBeforeSignal;
    uint32_t render;
    uint32_t compute;
    uint32_t blit;
    uint32_t committed;
} Seen;

typedef void (*EventFn)(id, SEL, id<MTLEvent>, uint64_t);
typedef void (*CommitFn)(id, SEL);
typedef id (*EncoderFn)(id, SEL);
typedef id (*RenderFn)(id, SEL, MTLRenderPassDescriptor*);

// The methods swapped and their implementations as they were, and the
// command buffers seen.
static Method s_waitMethod;
static Method s_signalMethod;
static Method s_commitMethod;
static Method s_computeMethod;
static Method s_blitMethod;
static Method s_renderMethod;
static EventFn s_wait;
static EventFn s_signal;
static CommitFn s_commit;
static EncoderFn s_compute;
static EncoderFn s_blit;
static RenderFn s_render;
static Seen s_seen[BUFFERS];
static uint32_t s_seenCount;
static uint32_t s_commitCount;

static int LaneOf(id<MTLCommandBuffer> buffer)
{
    NSString* label = buffer.commandQueue.label;
    if ([label hasSuffix:@"(async compute)"])
    {
        return LANE_COMPUTE;
    }
    if ([label hasSuffix:@"(transfer)"])
    {
        return LANE_TRANSFER;
    }
    return [label isEqualToString:@LABEL] ? LANE_GRAPHICS : -1;
}

// The command buffer's record, made on first sight: NULL past BUFFERS.
static Seen* See(id<MTLCommandBuffer> buffer)
{
    for (uint32_t i = 0; i < s_seenCount; ++i)
    {
        if (s_seen[i].buffer == (const void*)buffer)
        {
            return &s_seen[i];
        }
    }
    if (s_seenCount == BUFFERS)
    {
        return nullptr;
    }
    Seen* seen = &s_seen[s_seenCount++];
    *seen = (Seen){.buffer = (const void*)buffer, .lane = LaneOf(buffer)};
    return seen;
}

static uint32_t Encoded(const Seen* seen)
{
    return seen->render + seen->compute + seen->blit;
}

static void WaitHook(id self, SEL command, id<MTLEvent> event, uint64_t value)
{
    Seen* seen = See(self);
    if (seen != nullptr && seen->waitCount < WAITS)
    {
        seen->waitEvents[seen->waitCount] = (const void*)event;
        seen->waitValues[seen->waitCount] = value;
        seen->encodedBeforeWait[seen->waitCount++] = Encoded(seen);
    }
    s_wait(self, command, event, value);
}

static void SignalHook(id self, SEL command, id<MTLEvent> event, uint64_t value)
{
    Seen* seen = See(self);
    if (seen != nullptr)
    {
        seen->signalEvent = (const void*)event;
        seen->signal = value;
        seen->encodedBeforeSignal = Encoded(seen);
    }
    s_signal(self, command, event, value);
}

static void CommitHook(id self, SEL command)
{
    Seen* seen = See(self);
    if (seen != nullptr)
    {
        seen->committed = ++s_commitCount;
    }
    s_commit(self, command);
}

static id ComputeHook(id self, SEL command)
{
    Seen* seen = See(self);
    if (seen != nullptr)
    {
        ++seen->compute;
    }
    return s_compute(self, command);
}

static id BlitHook(id self, SEL command)
{
    Seen* seen = See(self);
    if (seen != nullptr)
    {
        ++seen->blit;
    }
    return s_blit(self, command);
}

static id RenderHook(id self, SEL command, MTLRenderPassDescriptor* descriptor)
{
    Seen* seen = See(self);
    if (seen != nullptr)
    {
        ++seen->render;
    }
    return s_render(self, command, descriptor);
}

// Swaps a method of the command buffers' class, keeping the one it was.
static Method Swap(Class class_, SEL selector, IMP hook, IMP* was)
{
    Method method = class_getInstanceMethod(class_, selector);
    *was = method_setImplementation(method, hook);
    return method;
}

// Wraps the methods of the class of the device's command buffers, and
// forgets what was seen before.
static void Hook(mrhiDevice* device)
{
    s_seenCount = 0;
    s_commitCount = 0;
    void* native = nullptr;
    void* queue = nullptr;
    CHECK(mrhiGetMetalDevice(device, &native, &queue) == mrhi_success && queue != nullptr,
          "the native objects");
    if (queue == nullptr || s_commitMethod != nullptr)
    {
        return;
    }
    Class class_ = nil;
    @autoreleasepool
    {
        class_ = [[(id<MTLCommandQueue>)queue commandBuffer] class];
    }
    s_waitMethod = Swap(class_, @selector(encodeWaitForEvent:value:), (IMP)WaitHook, (IMP*)&s_wait);
    s_signalMethod =
        Swap(class_, @selector(encodeSignalEvent:value:), (IMP)SignalHook, (IMP*)&s_signal);
    s_commitMethod = Swap(class_, @selector(commit), (IMP)CommitHook, (IMP*)&s_commit);
    s_computeMethod =
        Swap(class_, @selector(computeCommandEncoder), (IMP)ComputeHook, (IMP*)&s_compute);
    s_blitMethod = Swap(class_, @selector(blitCommandEncoder), (IMP)BlitHook, (IMP*)&s_blit);
    s_renderMethod = Swap(class_, @selector(renderCommandEncoderWithDescriptor:), (IMP)RenderHook,
                          (IMP*)&s_render);
}

// Puts the methods back as they were; every check ends with it, so a
// failed one leaves the next its own Metal.
static void Unhook(void)
{
    if (s_commitMethod == nullptr)
    {
        return;
    }
    method_setImplementation(s_waitMethod, (IMP)s_wait);
    method_setImplementation(s_signalMethod, (IMP)s_signal);
    method_setImplementation(s_commitMethod, (IMP)s_commit);
    method_setImplementation(s_computeMethod, (IMP)s_compute);
    method_setImplementation(s_blitMethod, (IMP)s_blit);
    method_setImplementation(s_renderMethod, (IMP)s_render);
    s_commitMethod = nullptr;
}

// The committed command buffers on a lane (-1 for any), in the order
// they were committed: how many there are, the nth kept in *seenOut.
static uint32_t Committed(int lane, uint32_t nth, const Seen** seenOut)
{
    uint32_t count = 0;
    for (uint32_t order = 1; order <= s_commitCount; ++order)
    {
        for (uint32_t i = 0; i < s_seenCount; ++i)
        {
            const Seen* seen = &s_seen[i];
            if (seen->committed == order && (lane < 0 || seen->lane == lane))
            {
                if (count == nth && seenOut != nullptr)
                {
                    *seenOut = seen;
                }
                ++count;
            }
        }
    }
    return count;
}

// Whether a command buffer waits, before any encoder it opens, for an
// event to reach a value or past it.
static bool WaitsFor(const Seen* seen, const void* event, uint64_t value)
{
    for (uint32_t i = 0; seen != nullptr && i < seen->waitCount; ++i)
    {
        if (seen->waitEvents[i] == event && seen->waitValues[i] >= value &&
            seen->encodedBeforeWait[i] == 0)
        {
            return true;
        }
    }
    return false;
}

// Whether a command buffer waits for the signal of another committed
// before it.
static bool LaneWaitsFor(const Seen* waiter, const Seen* signaller)
{
    return waiter != nullptr && signaller != nullptr && signaller->signal != 0 &&
           signaller->committed < waiter->committed &&
           WaitsFor(waiter, signaller->signalEvent, signaller->signal);
}

// The first committed command buffer of a lane that opened an encoder.
static const Seen* Working(int lane)
{
    const Seen* seen = nullptr;
    for (uint32_t n = 0; n < Committed(lane, UINT32_MAX, nullptr); ++n)
    {
        (void)Committed(lane, n, &seen);
        if (Encoded(seen) > 0)
        {
            return seen;
        }
    }
    return nullptr;
}

// Whether every committed command buffer signals its lane's event last,
// after its encoders, one event per lane.
static bool SignalsLast(void)
{
    const void* events[LANES] = {0};
    for (uint32_t i = 0; i < s_seenCount; ++i)
    {
        const Seen* seen = &s_seen[i];
        if (seen->committed == 0 || seen->lane < 0 || seen->signal == 0)
        {
            continue;
        }
        if (seen->encodedBeforeSignal != Encoded(seen) ||
            (events[seen->lane] != nullptr && events[seen->lane] != seen->signalEvent))
        {
            return false;
        }
        events[seen->lane] = seen->signalEvent;
    }
    return true;
}

static mrhiAccess Whole(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
}

static mrhiPassId AddPass(mrhiDevice* device, mrhiPassClass passClass, const mrhiAccess* access,
                          bool native)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = passClass;
    def.neverCull = true;
    def.native = native;
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

static void Upload(mrhiDevice* device, mrhiPassId pass, mrhiResourceId buffer, const void* words,
                   size_t bytes)
{
    CHECK(mrhiBeginPass(device, pass) == mrhi_success &&
              mrhiWriteBuffer(device, pass, buffer, 0, words, bytes) == mrhi_success &&
              mrhiEndPass(device, pass) == mrhi_success,
          "uploaded");
}

static mrhiRequestId ReadBack(mrhiDevice* device, mrhiPassId pass, mrhiResourceId buffer,
                              size_t bytes)
{
    mrhiRequestId request = {0};
    CHECK(mrhiBeginPass(device, pass) == mrhi_success &&
              mrhiReadBuffer(device, pass, buffer, 0, bytes, &request) == mrhi_success &&
              mrhiEndPass(device, pass) == mrhi_success,
          "read back");
    return request;
}

// Begins a frame importing the buffer.
static mrhiResourceId Begin(mrhiDevice* device, mrhiBufferId buffer)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId b = {0};
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
              mrhiImportBuffer(device, buffer, &b) == mrhi_success,
          "a frame with the buffer");
    return b;
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
static const uint32_t kOther[8] = {9, 10, 11, 12, 13, 14, 15, 16};

// The signal of the last committed command buffer of a lane, with its
// event in *eventOut.
static uint64_t LastSignal(int lane, const void** eventOut)
{
    const Seen* seen = nullptr;
    uint32_t count = Committed(lane, UINT32_MAX, nullptr);
    if (count > 0)
    {
        (void)Committed(lane, count - 1, &seen);
    }
    *eventOut = seen != nullptr ? seen->signalEvent : nullptr;
    return seen != nullptr ? seen->signal : 0;
}

// Two frames. The first: an upload in a transfer pass, a dispatch in an
// async compute pass and a readback in a graphics pass, each following
// the one before, so each runs on its lane's queue after a wait for the
// signal of the one before. The second: an upload in a transfer pass, a
// dispatch and a readback in a transfer pass after it. Its first
// transfer command buffer waits for the first frame's last signal on
// every lane, and its second for its first.
static void CheckLanes(mrhiDevice* device, mrhiBufferId buffer, mrhiComputePipelineId pipeline)
{
    mrhiResourceId b = Begin(device, buffer);
    mrhiAccess write = Whole(b, mrhi_accessCopyDestination);
    mrhiAccess storage = Whole(b, mrhi_accessStorageReadWrite);
    mrhiAccess read = Whole(b, mrhi_accessCopySource);
    mrhiPassId upload = AddPass(device, mrhi_passTransfer, &write, false);
    mrhiPassId compute = AddPass(device, mrhi_passAsyncCompute, &storage, false);
    mrhiPassId back = AddPass(device, mrhi_passGraphics, &read, false);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "the first frame compiled");
    Upload(device, upload, b, kWords, sizeof(kWords));
    Dispatch(device, compute, pipeline);
    mrhiRequestId first = ReadBack(device, back, b, sizeof(kWords));
    Hook(device);
    Finish(device);
    const Seen* transfer = Working(LANE_TRANSFER);
    const Seen* dispatched = Working(LANE_COMPUTE);
    const Seen* graphics = Working(LANE_GRAPHICS);
    CHECK(transfer != nullptr && transfer->blit > 0, "the upload on the transfer queue");
    CHECK(dispatched != nullptr && dispatched->compute > 0,
          "the dispatch on the async compute queue");
    CHECK(graphics != nullptr && graphics->blit > 0, "the readback on the graphics queue");
    CHECK(LaneWaitsFor(dispatched, transfer), "the async compute queue waits for the transfer");
    CHECK(LaneWaitsFor(graphics, dispatched), "the graphics queue waits for the dispatch");
    const void* events[LANES] = {0};
    uint64_t lasts[LANES] = {0};
    for (int lane = 0; lane < LANES; ++lane)
    {
        lasts[lane] = LastSignal(lane, &events[lane]);
    }
    uint32_t before = Committed(LANE_TRANSFER, UINT32_MAX, nullptr);
    uint32_t dispatches = Committed(LANE_COMPUTE, UINT32_MAX, nullptr);
    b = Begin(device, buffer);
    write = Whole(b, mrhi_accessCopyDestination);
    storage = Whole(b, mrhi_accessStorageReadWrite);
    read = Whole(b, mrhi_accessCopySource);
    upload = AddPass(device, mrhi_passTransfer, &write, false);
    compute = AddPass(device, mrhi_passAsyncCompute, &storage, false);
    back = AddPass(device, mrhi_passTransfer, &read, false);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "the second frame compiled");
    Upload(device, upload, b, kOther, sizeof(kOther));
    Dispatch(device, compute, pipeline);
    mrhiRequestId second = ReadBack(device, back, b, sizeof(kOther));
    Finish(device);
    const Seen* opening = nullptr;
    const Seen* closing = nullptr;
    const Seen* between = nullptr;
    CHECK(Committed(LANE_TRANSFER, UINT32_MAX, nullptr) == before + 2,
          "two command buffers on the transfer queue");
    (void)Committed(LANE_TRANSFER, before, &opening);
    (void)Committed(LANE_TRANSFER, before + 1, &closing);
    (void)Committed(LANE_COMPUTE, dispatches, &between);
    bool all = opening != nullptr;
    for (int lane = 0; all && lane < LANES; ++lane)
    {
        all = lasts[lane] == 0 || WaitsFor(opening, events[lane], lasts[lane]);
    }
    CHECK(all, "the next frame's first transfer waits for the frame before on every lane");
    CHECK(LaneWaitsFor(between, opening) && LaneWaitsFor(closing, between),
          "the dispatch between the transfers waits for the first, the second for it");
    CHECK(LaneWaitsFor(closing, opening), "the transfer queue's second waits for its first");
    CHECK(SignalsLast(), "each command buffer signals its lane's event last");
    Unhook();
    CHECK(Took(device, first, kWords, sizeof(kWords)), "the first frame's words");
    CHECK(Took(device, second, kOther, sizeof(kOther)), "the second frame's words");
}

// A frame of graphics passes alone, an upload and a readback: one
// command buffer, on the graphics queue.
static void CheckGraphics(mrhiDevice* device, mrhiBufferId buffer)
{
    mrhiResourceId b = Begin(device, buffer);
    mrhiAccess write = Whole(b, mrhi_accessCopyDestination);
    mrhiAccess read = Whole(b, mrhi_accessCopySource);
    mrhiPassId upload = AddPass(device, mrhi_passGraphics, &write, false);
    mrhiPassId back = AddPass(device, mrhi_passGraphics, &read, false);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "the frame compiled");
    Upload(device, upload, b, kWords, sizeof(kWords));
    mrhiRequestId request = ReadBack(device, back, b, sizeof(kWords));
    Hook(device);
    Finish(device);
    CHECK(Committed(-1, UINT32_MAX, nullptr) == 1 &&
              Committed(LANE_GRAPHICS, UINT32_MAX, nullptr) == 1,
          "one command buffer, on the graphics queue");
    Unhook();
    CHECK(Took(device, request, kWords, sizeof(kWords)), "the words");
}

// An upload in a transfer pass, a native pass reading what it wrote, and
// a readback in a graphics pass: the program's command buffer comes right
// after one of the library's on the graphics queue that only waits, for
// the upload among the rest, and signals an event the readback's command
// buffer waits for.
static void CheckNative(mrhiDevice* device, mrhiBufferId buffer)
{
    void* native = nullptr;
    void* queue = nullptr;
    CHECK(mrhiGetMetalDevice(device, &native, &queue) == mrhi_success && queue != nullptr,
          "the native objects");
    if (queue == nullptr)
    {
        return;
    }
    mrhiResourceId b = Begin(device, buffer);
    mrhiAccess write = Whole(b, mrhi_accessCopyDestination);
    mrhiAccess read = Whole(b, mrhi_accessCopySource);
    mrhiPassId upload = AddPass(device, mrhi_passTransfer, &write, false);
    mrhiPassId pass = AddPass(device, mrhi_passGraphics, &read, true);
    mrhiPassId back = AddPass(device, mrhi_passGraphics, &read, false);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "the frame compiled");
    Upload(device, upload, b, kWords, sizeof(kWords));
    id<MTLCommandBuffer> commands = nil;
    @autoreleasepool
    {
        commands = [[(id<MTLCommandQueue>)queue commandBuffer] retain];
    }
    CHECK(mrhiBeginPass(device, pass) == mrhi_success &&
              mrhiSetMetalPassCommands(device, pass, (void*)commands) == mrhi_success &&
              mrhiEndPass(device, pass) == mrhi_success,
          "the native pass");
    mrhiRequestId request = ReadBack(device, back, b, sizeof(kWords));
    Hook(device);
    Finish(device);
    const Seen* program = nullptr;
    const Seen* gate = nullptr;
    const Seen* after = nullptr;
    for (uint32_t i = 0; i < s_seenCount; ++i)
    {
        program = s_seen[i].buffer == (const void*)commands ? &s_seen[i] : program;
    }
    for (uint32_t i = 0; program != nullptr && i < s_seenCount; ++i)
    {
        const Seen* seen = &s_seen[i];
        gate = seen->committed + 1 == program->committed ? seen : gate;
        after =
            seen->committed > program->committed && seen->lane == LANE_GRAPHICS && Encoded(seen) > 0
                ? seen
                : after;
    }
    const Seen* transfer = Working(LANE_TRANSFER);
    CHECK(program != nullptr && program->committed != 0 && program->signal != 0,
          "the program's command buffer committed, signalling an event");
    CHECK(gate != nullptr && gate->lane == LANE_GRAPHICS && Encoded(gate) == 0 &&
              gate->signal == 0 && LaneWaitsFor(gate, transfer),
          "right after one that only waits, for the upload among the rest");
    CHECK(after != nullptr && program != nullptr &&
              WaitsFor(after, program->signalEvent, program->signal),
          "the readback after the program's signal");
    Unhook();
    [commands release];
    CHECK(Took(device, request, kWords, sizeof(kWords)), "the words");
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

static void CheckDevice(mrhiInstance* instance, mrhiAdapterId adapter)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    def.label = LABEL;
    def.labelLength = strlen(LABEL);
    mrhiDevice* device = nullptr;
    mrhiRequestId opened = {0};
    mrhiInstanceNotification record;
    CHECK(mrhiCreateDevice(instance, &def, &device, &opened) == mrhi_success &&
              mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
              record.outcome == mrhi_success,
          "a device");
    if (device == nullptr)
    {
        return;
    }
    mrhiShaderId shader = {0};
    mrhiComputePipelineId pipeline = NoopPipeline(device, &shader);
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = sizeof(kWords);
    bufferDef.usage = mrhi_bufferStorage | mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    mrhiBufferId buffer = {0};
    CHECK(mrhiCreateBuffer(device, &bufferDef, &buffer) == mrhi_success, "the buffer");
    CheckLanes(device, buffer, pipeline);
    CheckGraphics(device, buffer);
    CheckNative(device, buffer);
    CHECK(mrhiDestroyBuffer(device, buffer) == mrhi_success &&
              mrhiDestroyComputePipeline(device, pipeline) == mrhi_success &&
              mrhiDestroyShader(device, shader) == mrhi_success,
          "the objects ended");
    mrhiDestroyDevice(device);
}

int main(void)
{
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
        CheckDevice(instance, adapter);
    }
    else
    {
        const char* required = getenv("MAUL_RHI_REQUIRE_METAL");
        CHECK(required == nullptr || required[0] == '\0', "Metal where required");
        printf("skip: no Metal adapter\n");
    }
    mrhiDestroyInstance(instance);
    return s_failures == 0 ? 0 : 1;
}
