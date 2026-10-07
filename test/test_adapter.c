// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Adapter requests on the test driver: one answer per request, the
// preference's order, stable and stale ids, and the named limits.

#include "test_harness.h"

#include "maul-rhi/test.h"

#include <string.h>

static mrhiTestAdapter Adapter(mrhiAdapterKind kind, const char* name)
{
    mrhiTestAdapter adapter = {
        .info = {.driver = mrhi_driverTest, .kind = kind, .vendorId = 0x1234},
        .limits = mrhiDefaultLimits(),
    };
    adapter.info.nameLength = (uint32_t)strlen(name);
    memcpy(adapter.info.name, name, adapter.info.nameLength);
    return adapter;
}

// A software, an integrated and a discrete adapter, in that order.
static mrhiTestAdapter s_adapters[3];
static mrhiTestDriverDef s_driver;

static mrhiInstance* Create(uint32_t notifications, uint32_t adapters, bool testDriver)
{
    s_adapters[0] = Adapter(mrhi_adapterSoftware, "soft");
    s_adapters[1] = Adapter(mrhi_adapterIntegrated, "igpu");
    s_adapters[2] = Adapter(mrhi_adapterDiscrete, "dgpu");
    s_driver = (mrhiTestDriverDef){
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = s_adapters,
        .adapterCount = 3,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = testDriver ? &s_driver.chain : nullptr;
    def.limits.notifications = notifications;
    def.limits.adapters = adapters;
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "the instance");
    return instance;
}

// Requests adapters and drains the answer; returns its outcome.
static mrhiResult Find(mrhiInstance* instance, mrhiPowerPreference preference, bool software)
{
    mrhiAdapterRequestDef def = mrhiDefaultAdapterRequestDef();
    def.preference = preference;
    def.allowSoftware = software;
    mrhiRequestId request = {0};
    CHECK(mrhiRequestAdapters(instance, &def, &request) == mrhi_success, "the request");
    mrhiInstanceNotification record = {0};
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_success, "an answer");
    CHECK(record.kind == mrhi_instanceAdaptersFound, "an adapters record");
    CHECK(record.requestId.index1 == request.index1 && request.index1 != 0, "its request");
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_empty, "exactly one answer");
    return record.outcome;
}

// The first letter of each listed adapter's name, in order.
static void Listing(mrhiInstance* instance, char* letters)
{
    mrhiAdapterId ids[4];
    size_t count = 0;
    CHECK(mrhiGetAdapters(instance, ids, 4, &count) == mrhi_success, "the listing");
    for (size_t i = 0; i < count && i < 4; ++i)
    {
        mrhiAdapterInfo info;
        CHECK(mrhiGetAdapterInfo(instance, ids[i], &info) == mrhi_success, "an adapter's facts");
        letters[i] = info.name[0];
    }
    letters[count < 4 ? count : 4] = '\0';
}

static void TestPreferenceOrders(void)
{
    mrhiInstance* instance = Create(64, 16, true);
    char letters[5];
    CHECK(Find(instance, mrhi_powerDefault, true) == mrhi_success, "default");
    Listing(instance, letters);
    CHECK(strcmp(letters, "sid") == 0, "the driver's order");
    CHECK(Find(instance, mrhi_powerHigh, true) == mrhi_success, "high");
    Listing(instance, letters);
    CHECK(strcmp(letters, "dis") == 0, "discrete first, software last");
    CHECK(Find(instance, mrhi_powerLow, true) == mrhi_success, "low");
    Listing(instance, letters);
    CHECK(strcmp(letters, "ids") == 0, "integrated first, software last");
    CHECK(Find(instance, mrhi_powerDefault, false) == mrhi_success, "no software");
    Listing(instance, letters);
    CHECK(strcmp(letters, "id") == 0, "software left out");
    mrhiDestroyInstance(instance);
}

static void TestIdsStayAndGoStale(void)
{
    mrhiInstance* instance = Create(64, 16, true);
    CHECK(Find(instance, mrhi_powerDefault, true) == mrhi_success, "the first search");
    mrhiAdapterId first[3];
    size_t count = 0;
    CHECK(mrhiGetAdapters(instance, first, 3, &count) == mrhi_success && count == 3, "three");
    CHECK(Find(instance, mrhi_powerHigh, false) == mrhi_success, "the second search");
    mrhiAdapterId second[3];
    CHECK(mrhiGetAdapters(instance, second, 3, &count) == mrhi_success && count == 2, "two");
    CHECK(memcmp(&second[0], &first[2], sizeof(mrhiAdapterId)) == 0, "the discrete id stays");
    CHECK(memcmp(&second[1], &first[1], sizeof(mrhiAdapterId)) == 0, "the integrated id stays");
    mrhiAdapterInfo info;
    CHECK(mrhiGetAdapterInfo(instance, first[0], &info) == mrhi_errorStale, "software is stale");
    CHECK(Find(instance, mrhi_powerDefault, true) == mrhi_success, "the third search");
    mrhiAdapterId third[3];
    CHECK(mrhiGetAdapters(instance, third, 3, &count) == mrhi_success && count == 3, "three");
    CHECK(third[0].index1 == first[0].index1 && third[0].generation != first[0].generation,
          "a found-again adapter is a new generation");
    CHECK(mrhiGetAdapterInfo(instance, first[0], &info) == mrhi_errorStale,
          "its old id stays stale once the slot is back in use");
    CHECK(mrhiGetAdapterInfo(instance, (mrhiAdapterId){0, 1}, &info) == mrhi_errorStale, "null");
    CHECK(mrhiGetAdapterInfo(instance, (mrhiAdapterId){17, 1}, &info) == mrhi_errorStale,
          "past the table");
    CHECK(mrhiGetAdapterInfo(instance, (mrhiAdapterId){UINT32_MAX, 1}, &info) == mrhi_errorStale,
          "far past the table");
    CHECK(mrhiGetAdapterInfo(instance, (mrhiAdapterId){16, 1}, &info) == mrhi_errorStale,
          "a slot never used");
    mrhiDestroyInstance(instance);
}

static void TestAnswerComesAtTheDrain(void)
{
    mrhiInstance* instance = Create(64, 16, true);
    mrhiAdapterRequestDef def = mrhiDefaultAdapterRequestDef();
    mrhiRequestId a = {0};
    mrhiRequestId b = {0};
    mrhiRequestId c = {0};
    CHECK(mrhiRequestAdapters(instance, &def, &a) == mrhi_success, "a");
    CHECK(mrhiRequestAdapters(instance, &def, &b) == mrhi_success, "b");
    CHECK(mrhiRequestAdapters(instance, &def, &c) == mrhi_success, "c");
    CHECK(a.index1 != b.index1 && b.index1 != c.index1, "requests have their own ids");
    size_t count = 1;
    CHECK(mrhiGetAdapters(instance, nullptr, 0, &count) == mrhi_success && count == 0,
          "nothing listed before the answer");
    mrhiInstanceNotification record;
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
              record.requestId.index1 == a.index1,
          "a answers first");
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
              record.requestId.index1 == b.index1,
          "then b");
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
              record.requestId.index1 == c.index1,
          "then c");
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_empty, "then nothing");
    CHECK(mrhiGetAdapters(instance, nullptr, 0, &count) == mrhi_success && count == 3,
          "the count without an array");
    mrhiDestroyInstance(instance);
}

static void TestManyRequestsAtOnce(void)
{
    mrhiInstance* instance = Create(64, 16, true);
    mrhiAdapterRequestDef def = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    for (int i = 0; i < 20; ++i)
    {
        CHECK(mrhiRequestAdapters(instance, &def, &request) == mrhi_success, "a request");
    }
    mrhiInstanceNotification record;
    int answers = 0;
    while (mrhiNextInstanceNotification(instance, &record) == mrhi_success)
    {
        CHECK(record.requestId.index1 == (uint32_t)answers + 1, "in order");
        ++answers;
    }
    CHECK(answers == 20, "every request answered once");
    mrhiDestroyInstance(instance);
}

static void TestTheFloor(void)
{
    mrhiInstance* instance = Create(64, 16, true);
    s_adapters[0].limits.textureDimension2d = 4096;
    s_adapters[1].limits.uniformOffsetAlignment = 512;
    s_adapters[2].limits.uniformOffsetAlignment = 64;
    s_adapters[2].limits.textureDimension2d = 16384;
    mrhiInstance* again = nullptr;
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &s_driver.chain;
    CHECK(mrhiCreateInstance(&def, &again) == mrhi_success, "an instance of weak adapters");
    CHECK(Find(again, mrhi_powerDefault, true) == mrhi_success, "found");
    char letters[5];
    Listing(again, letters);
    CHECK(strcmp(letters, "d") == 0, "only the adapter at or above the floor");
    mrhiAdapterId id;
    size_t count = 0;
    mrhiLimits limits;
    CHECK(mrhiGetAdapters(again, &id, 1, &count) == mrhi_success && count == 1, "one");
    CHECK(mrhiGetAdapterLimits(again, id, &limits) == mrhi_success &&
              limits.textureDimension2d == 16384 && limits.uniformOffsetAlignment == 64,
          "its own limits");
    mrhiDestroyInstance(again);
    mrhiDestroyInstance(instance);
}

static void TestFeaturesAreMaskedByTheApi(void)
{
    mrhiInstance* instance = Create(64, 16, true);
    mrhiDestroyInstance(instance);
    s_adapters[2].info.driver = mrhi_driverWebGpu;
    s_adapters[2].features = (mrhiFeatures){.shaderInt64 = true, .timestampQuery = true};
    s_adapters[1].info.driver = mrhi_driverD3d12;
    s_adapters[1].features = (mrhiFeatures){.textureCompressionAstc = true, .multiview = true};
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &s_driver.chain;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "the instance");
    CHECK(Find(instance, mrhi_powerHigh, false) == mrhi_success, "found");
    mrhiAdapterId ids[2];
    size_t count = 0;
    CHECK(mrhiGetAdapters(instance, ids, 2, &count) == mrhi_success && count == 2, "two");
    mrhiFeatures features;
    CHECK(mrhiGetAdapterFeatures(instance, ids[0], &features) == mrhi_success, "web features");
    CHECK(!features.shaderInt64 && features.timestampQuery, "no 64-bit integers in WGSL");
    CHECK(mrhiGetAdapterFeatures(instance, ids[1], &features) == mrhi_success, "d3d12 features");
    CHECK(!features.textureCompressionAstc && features.multiview, "no ASTC on D3D12");
    CHECK(mrhiGetAdapterFeatures(instance, (mrhiAdapterId){0, 0}, &features) == mrhi_errorStale,
          "a null id");
    uint64_t misuse = mrhiGetInstanceMisuse(instance);
    CHECK(mrhiGetAdapterFeatures(instance, ids[0], nullptr) == mrhi_errorInvalid &&
              mrhiGetInstanceMisuse(instance) == misuse + 1,
          "no out, counted as misuse");
    mrhiLimits limits;
    CHECK(mrhiGetAdapterLimits(instance, (mrhiAdapterId){0, 0}, &limits) == mrhi_errorStale,
          "a null id's limits");
    CHECK(mrhiGetAdapterLimits(nullptr, ids[0], &limits) == mrhi_errorInvalid, "no instance");
    mrhiDestroyInstance(instance);
}

static void TestDefaultLimits(void)
{
    mrhiLimits floor = mrhiDefaultLimits();
    CHECK(floor.textureDimension2d == 8192 && floor.bindingTables == 4, "WebGPU's defaults");
    CHECK(floor.rootBlockBytes == 64 && floor.heapSize == 0 && floor.framesInFlight == 2,
          "the contract's own");
    CHECK(floor.storageBindingBytes == 134217728 && floor.bufferBytes == 268435456, "64-bit");
}

static void TestLimits(void)
{
    mrhiInstance* instance = Create(2, 2, true);
    CHECK(Find(instance, mrhi_powerHigh, true) == mrhi_errorCapacity, "three adapters, room for 2");
    size_t count = 0;
    CHECK(mrhiGetAdapters(instance, nullptr, 0, &count) == mrhi_success && count == 2, "two kept");
    mrhiAdapterRequestDef def = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    CHECK(mrhiRequestAdapters(instance, &def, &request) == mrhi_success, "one");
    CHECK(mrhiRequestAdapters(instance, &def, &request) == mrhi_success, "two");
    CHECK(mrhiRequestAdapters(instance, &def, &request) == mrhi_errorCapacity,
          "no room for a third answer");
    mrhiInstanceNotification record;
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_success, "a drain");
    CHECK(mrhiRequestAdapters(instance, &def, &request) == mrhi_success, "room again");
    mrhiDestroyInstance(instance);
}

// Without the test driver, an instance lists the native driver's
// adapters, where the host has any, and never a test adapter.
static void TestWithoutTheTestDriver(void)
{
    mrhiInstance* instance = Create(64, 16, false);
    CHECK(Find(instance, mrhi_powerDefault, true) == mrhi_success, "answered");
    mrhiAdapterId ids[16];
    size_t count = 0;
    CHECK(mrhiGetAdapters(instance, ids, 16, &count) == mrhi_success && count <= 16, "listed");
    for (size_t i = 0; i < count; ++i)
    {
        mrhiAdapterInfo info;
        CHECK(mrhiGetAdapterInfo(instance, ids[i], &info) == mrhi_success &&
                  info.driver != mrhi_driverTest,
              "a native adapter");
    }
    mrhiDestroyInstance(instance);
}

static void TestInvalidRequests(void)
{
    mrhiInstance* instance = Create(64, 16, true);
    mrhiAdapterRequestDef def = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    mrhiInstanceNotification record;
    size_t count;
    CHECK(mrhiRequestAdapters(nullptr, &def, &request) == mrhi_errorInvalid, "no instance");
    CHECK(mrhiRequestAdapters(instance, nullptr, &request) == mrhi_errorInvalid, "no def");
    CHECK(mrhiRequestAdapters(instance, &def, nullptr) == mrhi_errorInvalid, "no out");
    def.preference = 3;
    CHECK(mrhiRequestAdapters(instance, &def, &request) == mrhi_errorInvalid, "a bad preference");
    def = mrhiDefaultAdapterRequestDef();
    def.cookie = 0;
    CHECK(mrhiRequestAdapters(instance, &def, &request) == mrhi_errorInvalid, "no cookie");
    def = mrhiDefaultAdapterRequestDef();
    mrhiChain critical = {.next = nullptr, .type = 0x7000u};
    def.next = &critical;
    CHECK(mrhiRequestAdapters(instance, &def, &request) == mrhi_errorUnsupported,
          "an unknown critical extension");
    mrhiChain untyped = {.next = nullptr, .type = mrhi_structNone};
    def.next = &untyped;
    CHECK(mrhiRequestAdapters(instance, &def, &request) == mrhi_errorInvalid, "an untyped node");
    CHECK(mrhiNextInstanceNotification(instance, nullptr) == mrhi_errorInvalid, "no record");
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_empty, "refusals queue nothing");
    CHECK(mrhiGetAdapters(instance, nullptr, 1, &count) == mrhi_errorInvalid, "a NULL array");
    CHECK(mrhiGetAdapters(instance, nullptr, 0, nullptr) == mrhi_errorInvalid, "no count");
    CHECK(mrhiGetInstanceMisuse(instance) == 8, "each invalid call on the instance counted once");
    mrhiAdapterInfo info;
    CHECK(mrhiGetAdapterInfo(instance, (mrhiAdapterId){3, 9}, &info) == mrhi_errorStale, "stale");
    CHECK(mrhiGetAdapterInfo(instance, (mrhiAdapterId){3, 9}, nullptr) == mrhi_errorInvalid,
          "no out");
    CHECK(mrhiGetInstanceMisuse(instance) == 9, "stale ids and unknown extensions are not misuse");
    CHECK(mrhiGetInstanceMisuse(nullptr) == 0, "no instance");
    mrhiDestroyInstance(instance);
}

static void TestTestDriverDef(void)
{
    mrhiTestDriverDef driver = {
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = nullptr,
        .adapterCount = 1,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &driver.chain;
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_errorInvalid && instance == nullptr,
          "adapters without an array");
}

int main(void)
{
    TestPreferenceOrders();
    TestIdsStayAndGoStale();
    TestAnswerComesAtTheDrain();
    TestManyRequestsAtOnce();
    TestLimits();
    TestTheFloor();
    TestFeaturesAreMaskedByTheApi();
    TestDefaultLimits();
    TestWithoutTheTestDriver();
    TestInvalidRequests();
    TestTestDriverDef();
    return s_failures == 0 ? 0 : 1;
}
