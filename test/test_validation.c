// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The validation layer (mrhi-0025), in builds with MAUL_RHI_VALIDATION:
// the test driver handed in from outside with one function broken at a
// time, each breach counted by mrhiGetDriverFaults while the call goes
// on; and a malformed frame, whose faults the walk counts without
// reading past what it checked.

#include "driver_test.h"
#include "frame_walk.h"
#include "test_harness.h"

#include "maul-rhi/device.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/resources.h"

static mrhiTestAdapter s_adapter;
static mrhiInstanceDriver s_made;
static mrhiInstanceDriverVtable s_vtable;
static mrhiDeviceDriverVtable s_deviceVtable;
static const mrhiDeviceDriverVtable* s_deviceInner;

// The test driver, its vtable copied for the test to break.
static void Open(void)
{
    s_adapter = (mrhiTestAdapter){
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .limits = mrhiDefaultLimits(),
    };
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
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "an instance");
    return instance;
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

static void TestInstanceAnswers(void)
{
    Open();
    s_vtable.requestAdapters = AnswerElsewhere;
    mrhiInstance* instance = Start();
    mrhiAdapterId adapter = {0};
    CHECK(Search(instance, &adapter) == 0, "a search answered for another request");
    CHECK(mrhiGetDriverFaults(instance) == 1, "counted");
    mrhiDestroyInstance(instance);
    Open();
    s_vtable.getAdapters = ListTwice;
    instance = Start();
    Search(instance, &adapter);
    CHECK(mrhiGetDriverFaults(instance) == 1, "an adapter listed twice");
    mrhiDestroyInstance(instance);
}

static mrhiResult MakeNothing(void* self, const mrhiBufferDef* def, uint64_t* handleOut)
{
    mrhiResult status = s_deviceInner->createBuffer(self, def, handleOut);
    *handleOut = 0;
    return status;
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
    CHECK(mrhiGetDriverFaults(instance) == 1, "a buffer whose handle is zero");
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

int main(void)
{
    TestClean();
    TestInstanceAnswers();
    TestDeviceAnswers();
    TestWalk();
    return s_failures == 0 ? 0 : 1;
}
