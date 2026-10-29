// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Devices on the test driver: made at once and ready later, the grants
// checked against the adapter and the floor, and the order of
// destruction.

#include "test_harness.h"

#include "maul-rhi/test.h"

#include <stdlib.h>
#include <string.h>

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

// An instance with one adapter that grants 16-bit floats, 16384 texels
// and 64-byte offset alignments; found, with its id in adapterOut.
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
    s_adapter.limits.uniformOffsetAlignment = 64;
    s_adapter.limits.storageOffsetAlignment = 64;
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
    CHECK(mrhiInstance_RequestAdapters(instance, &request, &id) == mrhi_success, "the search");
    CHECK(mrhiInstance_TakeNotification(instance, &record) == mrhi_success, "its answer");
    size_t count = 0;
    CHECK(mrhiInstance_GetAdapters(instance, adapterOut, 1, &count) == mrhi_success && count == 1,
          "one");
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
    CHECK(mrhiInstance_TakeNotification(instance, &record) == mrhi_success, "a record");
    CHECK(record.kind == mrhi_instanceDeviceReady, "a device ready record");
    CHECK(record.request.index1 == request.index1, "its request");
    CHECK(mrhiInstance_TakeNotification(instance, &record) == mrhi_empty, "exactly one");
    return record.outcome;
}

static void TestOpensLater(void)
{
    mrhiAdapterId adapter;
    mrhiInstance* instance = Create(64, mrhi_success, &adapter);
    mrhiDeviceDef def = Def(adapter);
    def.features.shaderF16 = true;
    def.limits.textureDimension2d = 16384;
    def.budgets.frameUploadBytes = 3u << 20;
    def.budgets.diagnostics = 5;
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success, "made");
    CHECK(device != nullptr && request.index1 != 0, "a device and a request");
    CHECK(mrhiDevice_GetState(device) == mrhi_deviceOpening, "opening until the drain");
    mrhiDeviceBudgets own;
    CHECK(mrhiDevice_GetBudgets(device, &own) == mrhi_success &&
              memcmp(&own, &def.budgets, sizeof(own)) == 0,
          "its budgets as asked, while opening");
    CHECK(Ready(instance, request) == mrhi_success, "ready");
    CHECK(mrhiDevice_GetState(device) == mrhi_deviceReady, "ready after it");
    mrhiFeatures features;
    mrhiLimits limits;
    CHECK(mrhiDevice_GetFeatures(device, &features) == mrhi_success && features.shaderF16,
          "the features granted");
    CHECK(mrhiDevice_GetLimits(device, &limits) == mrhi_success &&
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
    CHECK(mrhiDevice_GetState(device) == mrhi_deviceFailed, "failed");
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
    def.limits.uniformOffsetAlignment = 32;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorUnsupported,
          "an alignment finer than the adapter's");
    CHECK(mrhiInstance_GetMisuse(instance) == 0, "unsupported asks are not misuse");
    def = Def(adapter);
    def.limits.textureDimension2d = 4096;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid,
          "a limit below the floor");
    def = Def((mrhiAdapterId){adapter.index1, adapter.generation + 1});
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorStale,
          "a stale adapter");
    CHECK(mrhiInstance_GetMisuse(instance) == 1, "only the invalid ask is misuse");
    mrhiInstanceNotification record;
    CHECK(mrhiInstance_TakeNotification(instance, &record) == mrhi_empty, "refusals queue nothing");
    mrhiDestroyInstance(instance);
}

// An alignment limit is a power of two, as WebGPU requires: a device
// asking for another would hand its driver offsets the adapter cannot
// take.
static void TestAlignmentLimits(void)
{
    mrhiAdapterId adapter;
    mrhiInstance* instance = Create(64, mrhi_success, &adapter);
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    mrhiDeviceDef def = Def(adapter);
    def.limits.uniformOffsetAlignment = 96;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid,
          "a uniform alignment that is not a power of two");
    def = Def(adapter);
    def.limits.storageOffsetAlignment = 192;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid,
          "a storage alignment that is not a power of two");
    CHECK(mrhiInstance_GetMisuse(instance) == 2, "each is misuse");
    def = Def(adapter);
    def.limits.uniformOffsetAlignment = 64;
    def.limits.storageOffsetAlignment = 128;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success,
          "powers of two within the adapter's");
    mrhiDestroyDevice(device);
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

// Each device opening takes a request of its own.
static void TestOwnRequests(void)
{
    mrhiAdapterId adapter;
    mrhiInstance* instance = Create(64, mrhi_success, &adapter);
    mrhiDeviceDef def = Def(adapter);
    mrhiDevice* first = nullptr;
    mrhiDevice* second = nullptr;
    mrhiRequestId firstRequest;
    mrhiRequestId secondRequest;
    CHECK(mrhiCreateDevice(instance, &def, &first, &firstRequest) == mrhi_success &&
              mrhiCreateDevice(instance, &def, &second, &secondRequest) == mrhi_success,
          "two devices");
    CHECK(firstRequest.index1 != secondRequest.index1, "each with its own request");
    mrhiDestroyDevice(second);
    mrhiDestroyDevice(first);
    mrhiInstanceNotification record;
    while (mrhiInstance_TakeNotification(instance, &record) == mrhi_success)
    {
    }
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
    CHECK(mrhiInstance_GetMisuse(instance) == 1, "refused while a device exists");
    CHECK(mrhiDevice_GetState(device) == mrhi_deviceReady, "the device is untouched");
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
    CHECK(mrhiInstance_TakeNotification(instance, &record) == mrhi_success, "the stale answer");
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
    def.label = "\xC0\xAF";
    def.labelLength = 2;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid,
          "an overlong label");
    def = Def(adapter);
    static char longLabel[MRHI_MAX_LABEL_BYTES + 1];
    memset(longLabel, 'd', sizeof(longLabel));
    def.label = longLabel;
    def.labelLength = MRHI_MAX_LABEL_BYTES + 1;
    uint64_t misuse = mrhiInstance_GetMisuse(instance);
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorLimit &&
              mrhiInstance_GetMisuse(instance) == misuse + 1,
          "a label past the bound, counted");
    def = Def(adapter);
    def.budgets.notifications = 0;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid, "zero");
    def.budgets.notifications = 1;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid,
          "only the record kept for the loss notice");
    def = Def(adapter);
    def.budgets.samplers = 0;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid, "no samplers");
    def = Def(adapter);
    def.budgets.buffers = 0;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid, "no buffers");
    def = Def(adapter);
    def.budgets.textures = 0;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid, "no textures");
    def = Def(adapter);
    def.budgets.views = 0;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid, "no views");
    def = Def(adapter);
    def.budgets.surfaces = 0;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid, "no surfaces");
    def = Def(adapter);
    def.budgets.frameResources = 0;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid,
          "no frame resources");
    def = Def(adapter);
    def.budgets.framePasses = 0;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid, "no passes");
    def = Def(adapter);
    def.budgets.frameAccesses = 0;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid, "no accesses");
    def = Def(adapter);
    def.budgets.frameBarriers = 0;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid, "no barriers");
    def = Def(adapter);
    def.budgets.shaders = 0;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid, "no shaders");
    def = Def(adapter);
    def.budgets.pipelines = 0;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid, "no pipelines");
    def = Def(adapter);
    def.budgets.frameCommandBytes = 4095;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid,
          "less than a chunk of commands");
    def = Def(adapter);
    def.budgets.readbackBytes = 1000;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid,
          "a readback ring off 512 bytes");
    def = Def(adapter);
    mrhiChain critical = {.next = nullptr, .type = 0x7000u};
    def.next = &critical;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorUnsupported,
          "an unknown critical extension");
    mrhiChain untyped = {.next = nullptr, .type = mrhi_structNone};
    def.next = &untyped;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid, "untyped");
    def = Def(adapter);
    def.allocator.free = CountingFree;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_errorInvalid,
          "an allocator with one function");
    CHECK(mrhiInstance_GetMisuse(instance) == 23, "each invalid or limit call counted");
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
    CHECK(s_allocations == 2, "the device's own allocator, for the core and the driver");
    CHECK(mrhiDevice_GetFeatures(device, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiDevice_GetLimits(device, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiDevice_GetBudgets(device, nullptr) == mrhi_errorInvalid, "no out");
    mrhiDeviceBudgets own;
    CHECK(mrhiDevice_GetBudgets(nullptr, &own) == mrhi_errorInvalid, "no device");
    CHECK(mrhiDevice_GetMisuse(device) == 3 && mrhiDevice_GetMisuse(nullptr) == 0, "counted");
    CHECK(mrhiDevice_GetState(nullptr) == mrhi_deviceFailed, "no device");
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
    TestAlignmentLimits();
    TestDestroyWhileOpening();
    TestOwnRequests();
    TestInstanceOutlivesDevices();
    TestQueueRoom();
    TestInvalidDefs();
    TestDeviceAllocatorAndMisuse();
    return s_failures == 0 ? 0 : 1;
}
