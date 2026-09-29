// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Query sets on a test driver device: the def's checks, the timestamp
// feature, the device's state, and its two limits: sets, and runs of
// queries.

#include "test_device_setup.h"

#include "maul-rhi/resources.h"

// A ready device holding at most sets query sets of queries in all, or
// an opening one; timestamps granted when asked.
static mrhiDevice* Open(uint32_t sets, uint32_t queries, bool timestamps, bool ready)
{
    s_adapter.features.timestampQuery = timestamps;
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.deviceLimits.querySets = sets;
    def.deviceLimits.queries = queries;
    def.features.timestampQuery = timestamps;
    return OpenWith(def, ready);
}

static mrhiQuerySetDef Def(mrhiQueryType type, uint32_t count)
{
    mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
    def.type = type;
    def.count = count;
    return def;
}

static void TestMakeAndDestroy(void)
{
    mrhiDevice* device = Open(4, 4097, true, true);
    mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
    CHECK(def.type == mrhi_queryOcclusion && def.count == 1, "one occlusion query");
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    CHECK(deviceDef.deviceLimits.querySets == 16 && deviceDef.deviceLimits.queries == 4096,
          "the device's defaults");
    mrhiQuerySetId a = {0};
    mrhiQuerySetId b = {0};
    CHECK(mrhiCreateQuerySet(device, &def, &a) == mrhi_success && a.index1 != 0, "a");
    def = Def(mrhi_queryTimestamp, 4096);
    CHECK(mrhiCreateQuerySet(device, &def, &b) == mrhi_success, "all the other queries");
    CHECK(mrhiDestroyQuerySet(device, a) == mrhi_success, "a destroyed");
    CHECK(mrhiDestroyQuerySet(device, a) == mrhi_errorStale, "its id has ended");
    CHECK(mrhiDestroyQuerySet(device, (mrhiQuerySetId){0, 0}) == mrhi_errorStale, "the null id");
    CHECK(mrhiDestroyQuerySet(nullptr, b) == mrhi_errorInvalid, "no device");
    CHECK(mrhiDestroyQuerySet(device, b) == mrhi_success, "b destroyed");
    CHECK(mrhiGetDeviceMisuse(device) == 0, "no misuse");
    Close(device);
}

static void TestInvalidDefs(void)
{
    mrhiDevice* device = Open(4, 8192, false, true);
    mrhiQuerySetId set;
    mrhiQuerySetDef def = Def(mrhi_queryOcclusion, 0);
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorInvalid, "no queries");
    def.count = 4097;
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorInvalid, "past 4096");
    def = Def(2, 1);
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorInvalid, "a type");
    def = mrhiDefaultQuerySetDef();
    def.cookie = 0;
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorInvalid, "no cookie");
    def = mrhiDefaultQuerySetDef();
    def.labelLength = 1;
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorInvalid, "a length, no label");
    def = mrhiDefaultQuerySetDef();
    CHECK(mrhiCreateQuerySet(device, nullptr, &set) == mrhi_errorInvalid, "no def");
    CHECK(mrhiCreateQuerySet(device, &def, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiCreateQuerySet(nullptr, &def, &set) == mrhi_errorInvalid, "no device");
    CHECK(mrhiGetDeviceMisuse(device) == 7, "each counted on the device");
    def = Def(mrhi_queryTimestamp, 1);
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorUnsupported,
          "timestamps without the feature");
    mrhiChain critical = {.next = nullptr, .type = 0x7000u};
    def = mrhiDefaultQuerySetDef();
    def.next = &critical;
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorUnsupported, "an extension");
    def.label = "occlusion";
    def.labelLength = 9;
    def.next = nullptr;
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_success, "a labeled set");
    CHECK(mrhiGetDeviceMisuse(device) == 7, "neither counted");
    Close(device);
}

static void TestStateAndLimits(void)
{
    mrhiDevice* device = Open(2, 8, false, false);
    mrhiQuerySetDef three = Def(mrhi_queryOcclusion, 3);
    mrhiQuerySetId a;
    mrhiQuerySetId b;
    mrhiQuerySetId c;
    CHECK(mrhiCreateQuerySet(device, &three, &a) == mrhi_errorState, "not ready yet");
    mrhiInstanceNotification record;
    CHECK(mrhiNextInstanceNotification(s_instance, &record) == mrhi_success, "ready");
    mrhiQuerySetDef nine = Def(mrhi_queryOcclusion, 9);
    CHECK(mrhiCreateQuerySet(device, &nine, &a) == mrhi_errorCapacity, "more than the device");
    CHECK(mrhiCreateQuerySet(device, &three, &a) == mrhi_success, "queries 0 to 2");
    CHECK(mrhiCreateQuerySet(device, &three, &b) == mrhi_success, "3 to 5");
    CHECK(mrhiCreateQuerySet(device, &three, &c) == mrhi_errorCapacity, "the set limit first");
    CHECK(mrhiDestroyQuerySet(device, a) == mrhi_success, "0 to 2 free");
    mrhiQuerySetDef four = Def(mrhi_queryOcclusion, 4);
    CHECK(mrhiCreateQuerySet(device, &four, &c) == mrhi_errorCapacity,
          "five free, but not four in a run");
    CHECK(mrhiCreateQuerySet(device, &three, &c) == mrhi_success, "three in the first run");
    CHECK(mrhiDestroyQuerySet(device, b) == mrhi_success, "3 to 7 free");
    mrhiQuerySetDef five = Def(mrhi_queryOcclusion, 5);
    CHECK(mrhiCreateQuerySet(device, &five, &b) == mrhi_success, "five after it");
    CHECK(mrhiDestroyQuerySet(device, c) == mrhi_success &&
              mrhiDestroyQuerySet(device, b) == mrhi_success,
          "all free");
    mrhiQuerySetDef eight = Def(mrhi_queryOcclusion, 8);
    CHECK(mrhiCreateQuerySet(device, &eight, &a) == mrhi_success, "all eight");
    mrhiQuerySetDef one = mrhiDefaultQuerySetDef();
    CHECK(mrhiCreateQuerySet(device, &one, &b) == mrhi_errorCapacity, "none left");
    CHECK(mrhiGetDeviceMisuse(device) == 0, "no misuse");
    Close(device);
}

// Runs are found whatever order the sets' slots hold them in: a slot
// earlier in the table may hold a run after the one that moves the
// search.
static void TestRunOrder(void)
{
    mrhiDevice* device = Open(4, 6, false, true);
    mrhiQuerySetDef two = Def(mrhi_queryOcclusion, 2);
    mrhiQuerySetId a;
    mrhiQuerySetId b;
    CHECK(mrhiCreateQuerySet(device, &two, &a) == mrhi_success &&
              mrhiCreateQuerySet(device, &two, &b) == mrhi_success &&
              mrhiDestroyQuerySet(device, a) == mrhi_success &&
              mrhiDestroyQuerySet(device, b) == mrhi_success,
          "two slots used and freed");
    // The second slot is taken first, for queries 0 and 1; the first then
    // takes 2 and 3.
    CHECK(mrhiCreateQuerySet(device, &two, &b) == mrhi_success &&
              mrhiCreateQuerySet(device, &two, &a) == mrhi_success && a.index1 < b.index1,
          "the later run in the earlier slot");
    mrhiQuerySetId c;
    CHECK(mrhiCreateQuerySet(device, &two, &c) == mrhi_success, "4 and 5");
    mrhiQuerySetDef one = mrhiDefaultQuerySetDef();
    CHECK(mrhiCreateQuerySet(device, &one, &c) == mrhi_errorCapacity, "none left");
    Close(device);
}

static void TestNoQueries(void)
{
    mrhiDevice* device = Open(0, 0, false, true);
    mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
    mrhiQuerySetId set;
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorCapacity, "a device without");
    Close(device);
    device = Open(1, 0, false, true);
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorCapacity, "sets but no queries");
    Close(device);
    device = Open(0, 1, false, true);
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorCapacity, "queries but no sets");
    Close(device);
}

static void TestDriverFailure(void)
{
    s_adapter.objectsBeforeFailure = 1;
    mrhiDevice* device = Open(2, 2, false, true);
    mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
    mrhiQuerySetId a;
    mrhiQuerySetId b;
    CHECK(mrhiCreateQuerySet(device, &def, &a) == mrhi_success, "the one the driver makes");
    CHECK(mrhiCreateQuerySet(device, &def, &b) == mrhi_errorPlatform, "the driver fails");
    CHECK(mrhiDestroyQuerySet(device, a) == mrhi_success, "one freed");
    CHECK(mrhiCreateQuerySet(device, &def, &b) == mrhi_errorPlatform &&
              mrhiCreateQuerySet(device, &def, &b) == mrhi_errorPlatform,
          "and again, no slot lost");
    s_adapter.objectsBeforeFailure = 0;
    Close(device);
}

int main(void)
{
    ResetAdapter();
    TestMakeAndDestroy();
    TestInvalidDefs();
    TestStateAndLimits();
    TestRunOrder();
    TestNoQueries();
    TestDriverFailure();
    return s_failures == 0 ? 0 : 1;
}
