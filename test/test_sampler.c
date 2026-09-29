// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Samplers on a test driver device: the def's checks, the device's state
// and limit, and ids that end with their sampler.

#include "test_harness.h"

#include "maul-rhi/resources.h"
#include "maul-rhi/test.h"

#include <math.h>

static mrhiTestAdapter s_adapter;
static mrhiTestDriverDef s_driver;
static mrhiInstance* s_instance;

// The objects a device makes before its driver fails; 0 for none.
static uint32_t s_objectsBeforeFailure;

// A ready device holding at most samplers samplers, or an opening one.
static mrhiDevice* Open(uint32_t samplers, bool ready)
{
    s_adapter = (mrhiTestAdapter){
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .limits = mrhiDefaultLimits(),
        .objectsBeforeFailure = s_objectsBeforeFailure,
    };
    s_driver = (mrhiTestDriverDef){
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = &s_adapter,
        .adapterCount = 1,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &s_driver.chain;
    CHECK(mrhiCreateInstance(&def, &s_instance) == mrhi_success, "the instance");
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    mrhiInstanceNotification record;
    CHECK(mrhiRequestAdapters(s_instance, &search, &request) == mrhi_success, "the search");
    CHECK(mrhiNextInstanceNotification(s_instance, &record) == mrhi_success, "found");
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    size_t count = 0;
    CHECK(mrhiGetAdapters(s_instance, &deviceDef.adapter, 1, &count) == mrhi_success, "one");
    deviceDef.deviceLimits.samplers = samplers;
    mrhiDevice* device = nullptr;
    CHECK(mrhiCreateDevice(s_instance, &deviceDef, &device, &request) == mrhi_success, "made");
    if (ready)
    {
        CHECK(mrhiNextInstanceNotification(s_instance, &record) == mrhi_success, "ready");
    }
    return device;
}

static void Close(mrhiDevice* device)
{
    mrhiDestroyDevice(device);
    mrhiDestroyInstance(s_instance);
}

static void TestMakeAndDestroy(void)
{
    mrhiDevice* device = Open(4, true);
    mrhiSamplerDef def = mrhiDefaultSamplerDef();
    CHECK(def.lodMax == 32.0f && def.maxAnisotropy == 1 && def.compare == mrhi_compareNone,
          "WebGPU's defaults");
    mrhiSamplerId a = {0};
    mrhiSamplerId b = {0};
    CHECK(mrhiCreateSampler(device, &def, &a) == mrhi_success && a.index1 != 0, "a");
    def.magFilter = mrhi_filterLinear;
    def.minFilter = mrhi_filterLinear;
    def.mipFilter = mrhi_filterLinear;
    def.maxAnisotropy = 16;
    def.compare = mrhi_compareGreater;
    CHECK(mrhiCreateSampler(device, &def, &b) == mrhi_success, "an anisotropic comparison");
    CHECK(mrhiDestroySampler(device, a) == mrhi_success, "a destroyed");
    CHECK(mrhiDestroySampler(device, a) == mrhi_errorStale, "its id has ended");
    CHECK(mrhiDestroySampler(device, (mrhiSamplerId){0, 0}) == mrhi_errorStale, "the null id");
    CHECK(mrhiDestroySampler(nullptr, b) == mrhi_errorInvalid, "no device");
    CHECK(mrhiDestroySampler(device, b) == mrhi_success, "b destroyed");
    Close(device);
}

static void TestInvalidDefs(void)
{
    mrhiDevice* device = Open(4, true);
    mrhiSamplerId sampler;
    mrhiSamplerDef def = mrhiDefaultSamplerDef();
    def.maxAnisotropy = 4;
    CHECK(mrhiCreateSampler(device, &def, &sampler) == mrhi_errorInvalid, "anisotropy, nearest");
    def = mrhiDefaultSamplerDef();
    def.magFilter = def.minFilter = def.mipFilter = mrhi_filterLinear;
    def.maxAnisotropy = 17;
    CHECK(mrhiCreateSampler(device, &def, &sampler) == mrhi_errorInvalid, "anisotropy 17");
    def.maxAnisotropy = 0;
    CHECK(mrhiCreateSampler(device, &def, &sampler) == mrhi_errorInvalid, "anisotropy 0");
    def = mrhiDefaultSamplerDef();
    def.lodMin = 4.0f;
    def.lodMax = 2.0f;
    CHECK(mrhiCreateSampler(device, &def, &sampler) == mrhi_errorInvalid, "lods out of order");
    def = mrhiDefaultSamplerDef();
    def.lodMin = -1.0f;
    CHECK(mrhiCreateSampler(device, &def, &sampler) == mrhi_errorInvalid, "a negative lod");
    def.lodMin = NAN;
    CHECK(mrhiCreateSampler(device, &def, &sampler) == mrhi_errorInvalid, "NaN");
    def = mrhiDefaultSamplerDef();
    def.addressW = 3;
    CHECK(mrhiCreateSampler(device, &def, &sampler) == mrhi_errorInvalid, "clamp to border");
    def = mrhiDefaultSamplerDef();
    def.mipFilter = 2;
    CHECK(mrhiCreateSampler(device, &def, &sampler) == mrhi_errorInvalid, "a filter");
    def = mrhiDefaultSamplerDef();
    def.compare = 9;
    CHECK(mrhiCreateSampler(device, &def, &sampler) == mrhi_errorInvalid, "a comparison");
    def = mrhiDefaultSamplerDef();
    def.cookie = 0;
    CHECK(mrhiCreateSampler(device, &def, &sampler) == mrhi_errorInvalid, "no cookie");
    def = mrhiDefaultSamplerDef();
    CHECK(mrhiCreateSampler(device, nullptr, &sampler) == mrhi_errorInvalid, "no def");
    CHECK(mrhiCreateSampler(device, &def, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiCreateSampler(nullptr, &def, &sampler) == mrhi_errorInvalid, "no device");
    CHECK(mrhiGetDeviceMisuse(device) == 12, "each counted on the device");
    mrhiChain critical = {.next = nullptr, .type = 0x7000u};
    def.next = &critical;
    CHECK(mrhiCreateSampler(device, &def, &sampler) == mrhi_errorUnsupported, "an extension");
    mrhiChain untyped = {.next = nullptr, .type = mrhi_structNone};
    def.next = &untyped;
    CHECK(mrhiCreateSampler(device, &def, &sampler) == mrhi_errorInvalid, "an untyped node");
    CHECK(mrhiGetDeviceMisuse(device) == 13, "the untyped node counted");
    Close(device);
}

static void TestStateAndLimit(void)
{
    mrhiDevice* device = Open(2, false);
    mrhiSamplerDef def = mrhiDefaultSamplerDef();
    mrhiSamplerId a;
    mrhiSamplerId b;
    mrhiSamplerId c;
    CHECK(mrhiCreateSampler(device, &def, &a) == mrhi_errorState, "not ready yet");
    mrhiInstanceNotification record;
    CHECK(mrhiNextInstanceNotification(s_instance, &record) == mrhi_success, "ready");
    CHECK(mrhiCreateSampler(device, &def, &a) == mrhi_success, "one");
    CHECK(mrhiCreateSampler(device, &def, &b) == mrhi_success, "two");
    CHECK(mrhiCreateSampler(device, &def, &c) == mrhi_errorCapacity, "the limit");
    CHECK(mrhiDestroySampler(device, a) == mrhi_success, "one gone");
    CHECK(mrhiCreateSampler(device, &def, &c) == mrhi_success, "room again");
    CHECK(c.index1 == a.index1 && c.generation != a.generation, "the slot, newer");
    CHECK(mrhiGetDeviceMisuse(device) == 0, "no misuse");
    Close(device);
}

static void TestDriverFailure(void)
{
    s_objectsBeforeFailure = 1;
    mrhiDevice* device = Open(2, true);
    s_objectsBeforeFailure = 0;
    mrhiSamplerDef def = mrhiDefaultSamplerDef();
    mrhiSamplerId a;
    mrhiSamplerId b;
    CHECK(mrhiCreateSampler(device, &def, &a) == mrhi_success, "the one the driver makes");
    CHECK(mrhiCreateSampler(device, &def, &b) == mrhi_errorPlatform, "the driver fails");
    CHECK(mrhiDestroySampler(device, a) == mrhi_success, "one freed");
    CHECK(mrhiCreateSampler(device, &def, &b) == mrhi_errorPlatform, "fails again");
    CHECK(mrhiCreateSampler(device, &def, &b) == mrhi_errorPlatform, "and again, no slot lost");
    Close(device);
}

int main(void)
{
    TestMakeAndDestroy();
    TestInvalidDefs();
    TestStateAndLimit();
    TestDriverFailure();
    return s_failures == 0 ? 0 : 1;
}
