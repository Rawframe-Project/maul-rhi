// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Samplers on a test driver device: the def's checks, the device's state
// and limit, and ids that end with their sampler.

#include "test_device_setup.h"

#include "maul-rhi/resources.h"

#include <math.h>

// A ready device holding at most count samplers, or an opening one; the
// driver fails after s_adapter's objectsBeforeFailure objects.
static mrhiDevice* Open(uint32_t count, bool ready)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.deviceLimits.samplers = count;
    return OpenWith(def, ready);
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
    def.labelLength = 1;
    CHECK(mrhiCreateSampler(device, &def, &sampler) == mrhi_errorInvalid, "a length, no label");
    def = mrhiDefaultSamplerDef();
    CHECK(mrhiCreateSampler(device, nullptr, &sampler) == mrhi_errorInvalid, "no def");
    CHECK(mrhiCreateSampler(device, &def, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiCreateSampler(nullptr, &def, &sampler) == mrhi_errorInvalid, "no device");
    CHECK(mrhiGetDeviceMisuse(device) == 13, "each counted on the device");
    mrhiChain critical = {.next = nullptr, .type = 0x7000u};
    def.next = &critical;
    CHECK(mrhiCreateSampler(device, &def, &sampler) == mrhi_errorUnsupported, "an extension");
    mrhiChain untyped = {.next = nullptr, .type = mrhi_structNone};
    def.next = &untyped;
    CHECK(mrhiCreateSampler(device, &def, &sampler) == mrhi_errorInvalid, "an untyped node");
    CHECK(mrhiGetDeviceMisuse(device) == 14, "the untyped node counted");
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
    s_adapter.objectsBeforeFailure = 1;
    mrhiDevice* device = Open(2, true);
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
    ResetAdapter();
    TestMakeAndDestroy();
    TestInvalidDefs();
    TestStateAndLimit();
    TestDriverFailure();
    return s_failures == 0 ? 0 : 1;
}
