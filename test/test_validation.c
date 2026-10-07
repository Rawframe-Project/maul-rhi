// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The validation layer (mrhi-0025), in builds with MAUL_RHI_VALIDATION:
// the test driver handed in from outside with one function broken at a
// time, each breach counted by mrhiGetDriverFaults and recorded in the
// instance's diagnostic queue with its rule's code while the call goes
// on, and the edges a driver keeping the contract may reach left alone;
// and a malformed frame, whose faults the walk counts without reading
// past what it checked.

#include "device_core.h"
#include "driver_test.h"
#include "frame_walk.h"
#include "test_harness.h"

#include "maul-rhi/device.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/resources.h"

#include <string.h>

static mrhiTestAdapter s_adapter;
static mrhiInstanceDriver s_made;
static mrhiInstanceDriverVtable s_vtable;
static mrhiDeviceDriverVtable s_deviceVtable;
static const mrhiDeviceDriverVtable* s_deviceInner;
// Whether the next driver lists its adapter with a name of the most bytes.
static bool s_longName;

// The test driver, its vtable copied for the test to break.
static void Open(void)
{
    s_adapter = (mrhiTestAdapter){
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .limits = mrhiDefaultLimits(),
    };
    if (s_longName)
    {
        s_adapter.info.nameLength = MRHI_ADAPTER_NAME_BYTES;
        for (uint32_t i = 0; i < MRHI_ADAPTER_NAME_BYTES; ++i)
        {
            s_adapter.info.name[i] = 'a';
        }
    }
    mrhiTestDriverDef test = {
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = &s_adapter,
        .adapterCount = 1,
    };
    mrhiAllocator allocator = {0};
    CHECK(mrhiCreateTestDriver(&allocator, &test, 16, &s_made) == mrhi_success, "a driver");
    s_vtable = *s_made.vtable;
}

static mrhiInstance* Start(void)
{
    mrhiExternalDriverDef external = {
        .chain = {.next = nullptr, .type = mrhi_structExternalDriver},
        .vtable = &s_vtable,
        .driver = s_made.self,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &external.chain;
    def.limits.diagnostics = 8;
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "an instance");
    return instance;
}

// The code of the instance's oldest diagnostic, or 0 when there is none.
static mrhiDiagnosticCode Recorded(mrhiInstance* instance)
{
    mrhiDiagnostic record = {0};
    return mrhiNextInstanceDiagnostic(instance, &record) == mrhi_success ? record.code : 0;
}

// Searches, answering at the next poll: the adapters listed.
static size_t Search(mrhiInstance* instance, mrhiAdapterId* adapter)
{
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    mrhiInstanceNotification record;
    size_t count = 0;
    CHECK(mrhiRequestAdapters(instance, &search, &request) == mrhi_success, "a search");
    if (mrhiNextInstanceNotification(instance, &record) == mrhi_success)
    {
        CHECK(mrhiGetAdapters(instance, adapter, 1, &count) == mrhi_success, "listed");
    }
    return count;
}

static void TestClean(void)
{
    Open();
    mrhiInstance* instance = Start();
    mrhiAdapterId adapter = {0};
    CHECK(Search(instance, &adapter) == 1, "one adapter");
    CHECK(mrhiGetDriverFaults(instance) == 0, "none from a driver that keeps the contract");
    CHECK(mrhiGetDriverFaults(nullptr) == 0, "no instance");
    CHECK(Recorded(instance) == 0, "nothing recorded");
    mrhiDestroyInstance(instance);
}

static mrhiResult AnswerElsewhere(void* self, uint64_t tag)
{
    return s_made.vtable->requestAdapters(self, tag + 1000);
}

static size_t ListTwice(const void* self, mrhiDriverAdapter* adapters, size_t capacity)
{
    size_t total = s_made.vtable->getAdapters(self, adapters, capacity);
    if (total == 1 && capacity >= 2)
    {
        adapters[1] = adapters[0];
        return 2;
    }
    return total;
}

// Claims more events than the room given: the layer must not let the
// core read past its array.
static size_t OverReport(void* self, mrhiDriverEvent* events, size_t capacity)
{
    for (size_t i = 0; i < capacity; ++i)
    {
        events[i] = (mrhiDriverEvent){0};
    }
    return s_made.vtable->poll(self, events, capacity) > 0 ? capacity + 1 : 0;
}

// Answers a search twice: the second answers nothing asked.
static size_t AnswerTwice(void* self, mrhiDriverEvent* events, size_t capacity)
{
    size_t moved = s_made.vtable->poll(self, events, capacity);
    if (moved == 1 && capacity >= 2)
    {
        events[1] = events[0];
        return 2;
    }
    return moved;
}

static void TestInstanceAnswers(void)
{
    Open();
    s_vtable.requestAdapters = AnswerElsewhere;
    mrhiInstance* instance = Start();
    mrhiAdapterId adapter = {0};
    CHECK(Search(instance, &adapter) == 0, "a search answered for another request");
    CHECK(mrhiGetDriverFaults(instance) == 1, "counted");
    CHECK(Recorded(instance) == mrhi_diagnosticDriverUnaskedAnswer, "recorded");
    mrhiDestroyInstance(instance);
    Open();
    s_vtable.getAdapters = ListTwice;
    instance = Start();
    Search(instance, &adapter);
    CHECK(mrhiGetDriverFaults(instance) == 1 && Recorded(instance) == mrhi_diagnosticDriverAdapter,
          "an adapter listed twice");
    mrhiDestroyInstance(instance);
    Open();
    s_vtable.poll = OverReport;
    instance = Start();
    Search(instance, &adapter);
    CHECK(mrhiGetDriverFaults(instance) >= 1 &&
              Recorded(instance) == mrhi_diagnosticDriverEventsOverrun,
          "more events than room, clamped");
    mrhiDestroyInstance(instance);
    Open();
    s_vtable.poll = AnswerTwice;
    instance = Start();
    Search(instance, &adapter);
    CHECK(mrhiGetDriverFaults(instance) == 1 &&
              Recorded(instance) == mrhi_diagnosticDriverUnaskedAnswer,
          "a search answered twice");
    mrhiDestroyInstance(instance);
    // A name of the most bytes keeps the contract.
    s_longName = true;
    Open();
    s_longName = false;
    instance = Start();
    CHECK(Search(instance, &adapter) == 1 && mrhiGetDriverFaults(instance) == 0,
          "an adapter's name of the most bytes");
    mrhiDestroyInstance(instance);
}

// A device whose acquire, timestamp period and events the test gives.
static uint64_t s_image;
static mrhiResult s_acquired;
static double s_period;
static mrhiDriverEvent s_events[2];
static size_t s_eventCount;
static bool s_inject;

static mrhiResult AcquireGiven(void* self, uint64_t swapchain, uint64_t* imageOut)
{
    (void)self;
    (void)swapchain;
    *imageOut = s_image;
    return s_acquired;
}

static double PeriodGiven(void* self)
{
    (void)self;
    return s_period;
}

static size_t PollGiven(void* self, mrhiDriverEvent* events, size_t capacity)
{
    if (!s_inject)
    {
        return s_deviceInner->poll(self, events, capacity);
    }
    for (size_t i = 0; i < s_eventCount && i < capacity; ++i)
    {
        events[i] = s_events[i];
    }
    return s_eventCount;
}

static mrhiResult CreateGivenDevice(void* self, uint64_t adapter, const mrhiDeviceDef* def,
                                    uint64_t tag, mrhiDeviceDriver* deviceOut)
{
    mrhiResult status = s_made.vtable->createDevice(self, adapter, def, tag, deviceOut);
    if (status == mrhi_success)
    {
        s_deviceInner = deviceOut->vtable;
        s_deviceVtable = *deviceOut->vtable;
        s_deviceVtable.acquireImage = AcquireGiven;
        s_deviceVtable.timestampPeriod = PeriodGiven;
        s_deviceVtable.poll = PollGiven;
        deviceOut->vtable = &s_deviceVtable;
    }
    return status;
}

// The layer's answers at their edges, called as the core calls them.
static void TestDeviceEdges(void)
{
    Open();
    s_vtable.createDevice = CreateGivenDevice;
    mrhiInstance* instance = Start();
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    CHECK(Search(instance, &deviceDef.adapter) == 1, "one adapter");
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    CHECK(mrhiCreateDevice(instance, &deviceDef, &device, &request) == mrhi_success &&
              mrhiNextInstanceNotification(instance, &record) == mrhi_success,
          "a device");
    const mrhiDeviceDriverVtable* layer = device->driver.vtable;
    void* self = device->driver.self;
    uint64_t image = 1;
    s_image = 0;
    s_acquired = mrhi_errorDeviceLost;
    CHECK(layer->acquireImage(self, 1, &image) == mrhi_errorDeviceLost &&
              mrhiGetDriverFaults(instance) == 0,
          "no image with a failed acquire");
    s_acquired = mrhi_success;
    CHECK(layer->acquireImage(self, 1, &image) == mrhi_success &&
              mrhiGetDriverFaults(instance) == 1 &&
              Recorded(instance) == mrhi_diagnosticDriverZeroHandle,
          "an acquire that gives no image");
    s_period = 0.0;
    CHECK(layer->timestampPeriod(self) == 0.0 && mrhiGetDriverFaults(instance) == 1,
          "a period of zero from a driver that does not say");
    s_period = -1.0;
    CHECK(layer->timestampPeriod(self) == -1.0 && mrhiGetDriverFaults(instance) == 2 &&
              Recorded(instance) == mrhi_diagnosticDriverTimestampPeriod,
          "a negative period");
    // As many events as room, the last the device's loss under tag 0.
    mrhiDriverEvent events[2];
    s_events[0] = (mrhiDriverEvent){5, mrhi_success};
    s_events[1] = (mrhiDriverEvent){0, mrhi_errorDeviceLost};
    s_eventCount = 2;
    s_inject = true;
    CHECK(layer->poll(self, events, 2) == 2 && mrhiGetDriverFaults(instance) == 2,
          "events that fill the room, and the loss under tag 0");
    s_events[0] = (mrhiDriverEvent){0, mrhi_success};
    s_eventCount = 1;
    CHECK(layer->poll(self, events, 2) == 1 && mrhiGetDriverFaults(instance) == 3 &&
              Recorded(instance) == mrhi_diagnosticDriverEventTag,
          "tag 0 for anything but the loss");
    s_inject = false;
    mrhiDestroyDevice(device);
    mrhiDestroyInstance(instance);
}

static mrhiResult MakeNothing(void* self, const mrhiBufferDef* def, uint64_t* handleOut)
{
    mrhiResult status = s_deviceInner->createBuffer(self, def, handleOut);
    *handleOut = 0;
    return status;
}

// Takes any frame without the test driver's own walk, which would trap.
static mrhiResult TakeAnything(void* self, const mrhiDriverFrame* frame, uint64_t tag)
{
    (void)self;
    (void)frame;
    (void)tag;
    return mrhi_success;
}

static mrhiResult CreateBreakingDevice(void* self, uint64_t adapter, const mrhiDeviceDef* def,
                                       uint64_t tag, mrhiDeviceDriver* deviceOut)
{
    mrhiResult status = s_made.vtable->createDevice(self, adapter, def, tag, deviceOut);
    if (status == mrhi_success)
    {
        s_deviceInner = deviceOut->vtable;
        s_deviceVtable = *deviceOut->vtable;
        s_deviceVtable.createBuffer = MakeNothing;
        s_deviceVtable.submitFrame = TakeAnything;
        deviceOut->vtable = &s_deviceVtable;
    }
    return status;
}

static void TestDeviceAnswers(void)
{
    Open();
    s_vtable.createDevice = CreateBreakingDevice;
    mrhiInstance* instance = Start();
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    CHECK(Search(instance, &deviceDef.adapter) == 1, "one adapter");
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    CHECK(mrhiCreateDevice(instance, &deviceDef, &device, &request) == mrhi_success &&
              mrhiNextInstanceNotification(instance, &record) == mrhi_success,
          "a device");
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 256;
    bufferDef.usage = mrhi_bufferCopyDestination;
    mrhiBufferId buffer;
    CHECK(mrhiCreateBuffer(device, &bufferDef, &buffer) == mrhi_success, "the call goes on");
    CHECK(mrhiGetDriverFaults(instance) == 1 &&
              Recorded(instance) == mrhi_diagnosticDriverZeroHandle,
          "a buffer whose handle is zero");
    // A frame no driver could translate, handed to the layer as the core
    // hands frames: one pass whose chunk the frame lacks.
    mrhiDriverPass pass = {.id = {1, 1}, .firstChunk = 1};
    mrhiDriverFrame frame = {.passes = &pass, .passCount = 1};
    CHECK(device->driver.vtable->submitFrame(device->driver.self, &frame, 1) == mrhi_success &&
              mrhiGetDriverFaults(instance) == 2 &&
              Recorded(instance) == mrhi_diagnosticDriverFrameWalk,
          "a frame walked and found wanting");
    mrhiDestroyDevice(device);
    mrhiDestroyInstance(instance);
}

static bool AnyHandle(const void* handles, uint64_t handle)
{
    (void)handles;
    return handle != 0;
}

static void TestWalk(void)
{
    // A pass whose chunk is past the frame's, then one whose command's
    // payload runs past its chunk: the walk stops at each.
    mrhiCommandChunk chunk = {.count = 1};
    chunk.commands[0] = (mrhiCommand){.type = mrhiCommandDraw, .payload = 5};
    mrhiDriverPass passes[2] = {
        {.id = {1, 1}, .firstChunk = 2},
        {.id = {2, 1}, .firstChunk = 1},
    };
    mrhiDriverFrame frame = {
        .passes = passes,
        .passCount = 2,
        .chunks = &chunk,
        .chunkCount = 1,
    };
    // The two, and the chunk no walked pass took.
    CHECK(mrhiWalkFrame(&frame, AnyHandle, nullptr, nullptr) == 3, "each fault counted");
    chunk.commands[0].payload = 0;
    passes[0].firstChunk = 0;
    CHECK(mrhiWalkFrame(&frame, AnyHandle, nullptr, nullptr) == 0, "a frame that holds");
}

// Walks one pass over one chunk of a frame of a buffer (1) and a
// texture (2): its faults.
static uint64_t WalkOne(const mrhiCommandChunk* chunk, const mrhiDriverPass* pass)
{
    static const mrhiTextureDef texture = {.width = 4, .height = 4};
    static const mrhiDriverResource resources[2] = {
        {.kind = mrhiDriverDeviceBuffer, .handle = 1, .size = 256},
        {.kind = mrhiDriverDeviceTexture, .handle = 2, .texture = &texture},
    };
    mrhiDriverFrame frame = {
        .resources = resources,
        .resourceCount = 2,
        .passes = pass,
        .passCount = 1,
        .chunks = chunk,
        .chunkCount = 1,
    };
    return mrhiWalkFrame(&frame, AnyHandle, nullptr, nullptr);
}

// The walk at its edges: the last binding kind, both objects of a
// counted draw, a chunk of the most commands, a payload one past its
// chunk, and a pass of the most color targets.
static void TestWalkEdges(void)
{
    static mrhiCommandChunk chunk;
    mrhiDriverPass pass = {.id = {1, 1}, .firstChunk = 1};
    mrhiCommandBinding binding = {.object = 2, .kind = mrhi_bindingStorageTexture};
    chunk = (mrhiCommandChunk){.count = 2};
    chunk.commands[0] = (mrhiCommand){.type = mrhiCommandBindings, .payload = 1};
    memcpy(&chunk.commands[1], &binding, sizeof(binding));
    CHECK(WalkOne(&chunk, &pass) == 0, "a storage texture binding");
    chunk = (mrhiCommandChunk){.count = 1};
    chunk.commands[0] = (mrhiCommand){.type = mrhiCommandDrawIndirectCount, .a = 1, .b = 2};
    CHECK(WalkOne(&chunk, &pass) == 1, "a counted draw whose count is a texture");
    chunk.commands[0].a = 2;
    chunk.commands[0].b = 1;
    CHECK(WalkOne(&chunk, &pass) == 1, "and one whose arguments are");
    chunk = (mrhiCommandChunk){.count = MRHI_CHUNK_COMMANDS};
    for (uint32_t i = 0; i < MRHI_CHUNK_COMMANDS; ++i)
    {
        chunk.commands[i] = (mrhiCommand){.type = mrhiCommandDraw};
    }
    CHECK(WalkOne(&chunk, &pass) == 0, "a chunk of the most commands");
    chunk = (mrhiCommandChunk){.count = 2};
    chunk.commands[0] = (mrhiCommand){.type = mrhiCommandDraw};
    chunk.commands[1] = (mrhiCommand){.type = mrhiCommandDraw, .payload = 1};
    // The walk stops there, so the chunk counts as no pass's too.
    CHECK(WalkOne(&chunk, &pass) == 2, "a payload one record past its chunk");
    chunk = (mrhiCommandChunk){.count = 1};
    chunk.commands[0] = (mrhiCommand){.type = mrhiCommandDraw};
    pass.colorTargetCount = MRHI_COLOR_TARGETS;
    for (uint32_t i = 0; i < MRHI_COLOR_TARGETS; ++i)
    {
        pass.colorTargets[i].resource = (mrhiResourceId){2, 1};
    }
    CHECK(WalkOne(&chunk, &pass) == 0, "a pass of the most color targets");
}

int main(void)
{
    TestClean();
    TestInstanceAnswers();
    TestDeviceAnswers();
    TestDeviceEdges();
    TestWalk();
    TestWalkEdges();
    return s_failures == 0 ? 0 : 1;
}
