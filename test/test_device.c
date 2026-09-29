// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Devices on the test driver: made at once and ready later, the grants
// checked against the adapter and the floor, and the order of
// destruction.

#include "test_harness.h"

#include "maul-rhi/test.h"

#include <stdlib.h>

static int s_allocations;

static void* CountingAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    ++s_allocations;
    return malloc(size);
}

static void CountingFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    --s_allocations;
    free(memory);
}

static mrhiTestAdapter s_adapter;
static mrhiTestDriverDef s_driver;
// The allocator Create gives its instance.
static mrhiAllocator s_instanceAllocator;

// An instance with one adapter that grants 16-bit floats and 16384
// texels; found, with its id in adapterOut.
static mrhiInstance* Create(uint32_t notifications, mrhiResult openOutcome,
                            mrhiAdapterId* adapterOut)
{
    s_adapter = (mrhiTestAdapter){
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .features = {.shaderF16 = true},
        .limits = mrhiDefaultLimits(),
        .openOutcome = openOutcome,
    };
    s_adapter.limits.textureDimension2d = 16384;
    s_driver = (mrhiTestDriverDef){
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = &s_adapter,
        .adapterCount = 1,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &s_driver.chain;
    def.limits.notifications = notifications;
    def.allocator = s_instanceAllocator;
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "the instance");
    mrhiAdapterRequestDef request = mrhiDefaultAdapterRequestDef();
    mrhiRequestId id;
    mrhiInstanceNotification record;
    CHECK(mrhiRequestAdapters(instance, &request, &id) == mrhi_success, "the search");
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_success, "its answer");
    size_t count = 0;
    CHECK(mrhiGetAdapters(instance, adapterOut, 1, &count) == mrhi_success && count == 1, "one");
    return instance;
}

static mrhiDeviceDef Def(mrhiAdapterId adapter)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    return def;
}

// Drains one record and checks it answers the request.
static mrhiResult Ready(mrhiInstance* instance, mrhiRequestId request)
{
    mrhiInstanceNotification record = {0};
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_success, "a record");
    CHECK(record.kind == mrhi_instanceDeviceReady, "a device ready record");
    CHECK(record.requestId.index1 == request.index1, "its request");
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_empty, "exactly one");
    return record.outcome;
}

static void TestOpensLater(void)
{
    mrhiAdapterId adapter;
    mrhiInstance* instance = Create(64, mrhi_success, &adapter);
    mrhiDeviceDef def = Def(adapter);
    def.features.shaderF16 = true;
    def.limits.textureDimension2d = 16384;
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success, "made");
    CHECK(device != nullptr && request.index1 != 0, "a device and a request");
    CHECK(mrhiGetDeviceState(device) == mrhi_deviceOpening, "opening until the drain");
    CHECK(Ready(instance, request) == mrhi_success, "ready");
    CHECK(mrhiGetDeviceState(device) == mrhi_deviceReady, "ready after it");
    mrhiFeatures features;
    mrhiLimits limits;
    CHECK(mrhiGetDeviceFeatures(device, &features) == mrhi_success && features.shaderF16,
          "the features granted");
    CHECK(mrhiGetDeviceLimits(device, &limits) == mrhi_success &&
              limits.textureDimension2d == 16384,
          "the limits granted");
    mrhiDestroyDevice(device);
    mrhiDestroyInstance(instance);
}

static void TestFailedOpening(void)
{
    mrhiAdapterId adapter;
    mrhiInstance* instance = Create(64, mrhi_errorPlatform, &adapter);
    mrhiDeviceDef def = Def(adapter);
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success, "made");
    CHECK(Ready(instance, request) == mrhi_errorPlatform, "the driver's failure");
    CHECK(mrhiGetDeviceState(device) == mrhi_deviceFailed, "failed");
    mrhiDestroyDevice(device);
    mrhiDestroyInstance(instance);
}

static void TestGrants(void)
{
    mrhiAdapterId adapter;
    mrhiInstance* instance = Create(64, mrhi_success, &adapter);
    mrhiDevice* device = (mrhiDevice*)&adapter;
    mrhiRequestId request;
    mrhiDeviceDef def = Def(adapter);
    def.features.subgroups = true;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorUnsupported,
          "a feature the adapter lacks");
    CHECK(device == nullptr, "no device after a refusal");
    def = Def(adapter);
    def.limits.textureDimension2d = 32768;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorUnsupported,
          "a limit beyond the adapter");
    def = Def(adapter);
    def.limits.uniformOffsetAlignment = 128;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorUnsupported,
          "an alignment finer than the adapter's");
    CHECK(mrhiGetInstanceMisuse(instance) == 0, "unsupported asks are not misuse");
    def = Def(adapter);
    def.limits.textureDimension2d = 4096;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid,
          "a limit below the floor");
    def = Def((mrhiAdapterId){adapter.index1, adapter.generation + 1});
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorStale,
          "a stale adapter");
    CHECK(mrhiGetInstanceMisuse(instance) == 1, "only the invalid ask is misuse");
    mrhiInstanceNotification record;
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_empty, "refusals queue nothing");
    mrhiDestroyInstance(instance);
}

static void TestDestroyWhileOpening(void)
{
    mrhiAdapterId adapter;
    mrhiInstance* instance = Create(64, mrhi_success, &adapter);
    mrhiDeviceDef def = Def(adapter);
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success, "made");
    mrhiDestroyDevice(device);
    CHECK(Ready(instance, request) == mrhi_errorStale, "answered once, as stale");
    mrhiDestroyInstance(instance);
}

static void TestInstanceOutlivesDevices(void)
{
    mrhiAdapterId adapter;
    s_instanceAllocator = (mrhiAllocator){CountingAlloc, CountingFree, nullptr};
    mrhiInstance* instance = Create(64, mrhi_success, &adapter);
    s_instanceAllocator = (mrhiAllocator){0};
    mrhiDeviceDef def = Def(adapter);
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success, "made");
    CHECK(Ready(instance, request) == mrhi_success, "ready");
    mrhiDestroyInstance(instance);
    CHECK(mrhiGetInstanceMisuse(instance) == 1, "refused while a device exists");
    CHECK(mrhiGetDeviceState(device) == mrhi_deviceReady, "the device is untouched");
    mrhiDestroyDevice(device);
    mrhiDestroyInstance(instance);
    CHECK(s_allocations == 0, "the instance is destroyed once its devices are");
}

static void TestQueueRoom(void)
{
    mrhiAdapterId adapter;
    mrhiInstance* instance = Create(1, mrhi_success, &adapter);
    mrhiDeviceDef def = Def(adapter);
    mrhiDevice* first = nullptr;
    mrhiDevice* second = nullptr;
    mrhiRequestId request;
    CHECK(mrhiCreateDevice(instance, &def, &first, &request) == mrhi_success, "one");
    CHECK(mrhiCreateDevice(instance, &def, &second, &request) == mrhi_errorCapacity,
          "no room for a second answer");
    CHECK(second == nullptr, "no second device");
    mrhiDestroyDevice(first);
    mrhiInstanceNotification record;
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_success, "the stale answer");
    mrhiDestroyInstance(instance);
}

static void TestInvalidDefs(void)
{
    mrhiAdapterId adapter;
    mrhiInstance* instance = Create(64, mrhi_success, &adapter);
    mrhiDeviceDef def = Def(adapter);
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    CHECK(mrhiCreateDevice(nullptr, &def, &device, &request) == mrhi_errorInvalid, "no instance");
    CHECK(mrhiCreateDevice(instance, nullptr, &device, &request) == mrhi_errorInvalid, "no def");
    CHECK(mrhiCreateDevice(instance, &def, nullptr, &request) == mrhi_errorInvalid, "no out");
    CHECK(mrhiCreateDevice(instance, &def, &device, nullptr) == mrhi_errorInvalid, "no request");
    def.cookie = 0;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid, "no cookie");
    def = Def(adapter);
    def.deviceLimits.notifications = 0;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid, "zero");
    def = Def(adapter);
    mrhiChain critical = {.next = nullptr, .type = 0x7000u};
    def.next = &critical;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorUnsupported,
          "an unknown critical extension");
    mrhiChain untyped = {.next = nullptr, .type = mrhi_structNone};
    def.next = &untyped;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid, "untyped");
    CHECK(mrhiGetInstanceMisuse(instance) == 6, "each invalid call counted");
    mrhiDestroyInstance(instance);
}

static void TestDeviceAllocatorAndMisuse(void)
{
    mrhiAdapterId adapter;
    mrhiInstance* instance = Create(64, mrhi_success, &adapter);
    mrhiDeviceDef def = Def(adapter);
    def.allocator = (mrhiAllocator){CountingAlloc, CountingFree, nullptr};
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success, "made");
    CHECK(s_allocations == 1, "the device's own allocator");
    CHECK(mrhiGetDeviceFeatures(device, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiGetDeviceLimits(device, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiGetDeviceMisuse(device) == 2 && mrhiGetDeviceMisuse(nullptr) == 0, "counted");
    CHECK(mrhiGetDeviceState(nullptr) == mrhi_deviceFailed, "no device");
    mrhiDestroyDevice(device);
    CHECK(s_allocations == 0, "returned");
    mrhiDestroyDevice(nullptr);
    mrhiDestroyInstance(instance);
}

int main(void)
{
    TestOpensLater();
    TestFailedOpening();
    TestGrants();
    TestDestroyWhileOpening();
    TestInstanceOutlivesDevices();
    TestQueueRoom();
    TestInvalidDefs();
    TestDeviceAllocatorAndMisuse();
    return s_failures == 0 ? 0 : 1;
}
