// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Surface configuration on test driver devices: what the surface reports
// and the device takes, one device at a time, reconfiguring, and the
// ends of surfaces and devices. The test driver traps when a device goes
// before its configurations.

#include "test_device_setup.h"

// A test surface on the instance, presenting on the adapters in the mask,
// in BGRA8 sRGB and RGBA16F extended linear, with copies.
static mrhiSurfaceId MakeSurface(uint32_t presenting)
{
    mrhiSurfaceSourceTest source = {
        .chain = {.next = nullptr, .type = mrhi_structSurfaceSourceTest},
        .caps =
            {
                .colors = {{.format = mrhi_formatBgra8Unorm},
                           {.format = mrhi_formatRgba16Float,
                            .transfer = mrhi_transferLinear,
                            .range = mrhi_rangeExtended}},
                .colorCount = 2,
                .usages = mrhi_textureCopySource | mrhi_textureStorage,
            },
        .presentingAdapters = presenting,
    };
    mrhiSurfaceDef def = mrhiDefaultSurfaceDef();
    def.next = &source.chain;
    mrhiSurfaceId surface = {0};
    CHECK(mrhiCreateSurface(s_instance, &def, &surface) == mrhi_success, "the surface");
    return surface;
}

static mrhiSurfaceConfig Config(mrhiSurfaceId surface)
{
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    config.surface = surface;
    config.color = (mrhiSurfaceColor){.format = mrhi_formatBgra8Unorm};
    config.viewFormats[0] = mrhi_formatBgra8UnormSrgb;
    config.width = 1280;
    config.height = 720;
    return config;
}

// A second ready device on s_instance's adapter.
static mrhiDevice* OpenSecond(void)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    size_t count = 0;
    CHECK(mrhiGetAdapters(s_instance, &def.adapter, 1, &count) == mrhi_success, "the adapter");
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    CHECK(mrhiCreateDevice(s_instance, &def, &device, &request) == mrhi_success, "a second");
    CHECK(mrhiNextInstanceNotification(s_instance, &record) == mrhi_success, "ready");
    return device;
}

static void TestConfigure(void)
{
    mrhiDevice* device = OpenWith(mrhiDefaultDeviceDef(), true);
    mrhiSurfaceId surface = MakeSurface(1);
    mrhiSurfaceConfig config = Config(surface);
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_success, "configured");
    config.width = 1920;
    config.height = 1080;
    config.usage |= mrhi_textureCopySource;
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_success, "reconfigured");
    config.color = (mrhiSurfaceColor){
        .format = mrhi_formatRgba16Float,
        .transfer = mrhi_transferLinear,
        .range = mrhi_rangeExtended,
    };
    config.viewFormats[0] = mrhi_formatNone;
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_success, "for HDR");
    CHECK(mrhiUnconfigureSurface(device, surface) == mrhi_success, "unconfigured");
    CHECK(mrhiUnconfigureSurface(device, surface) == mrhi_errorState, "not configured");
    CHECK(mrhiUnconfigureSurface(nullptr, surface) == mrhi_errorInvalid, "no device");
    CHECK(mrhiUnconfigureSurface(device, (mrhiSurfaceId){0, 0}) == mrhi_errorStale, "null");
    CHECK(mrhiGetDeviceMisuse(device) == 0, "no misuse");
    Close(device);
}

// A config changed by one field is refused as invalid input.
static void TestInvalid(void)
{
    mrhiDevice* device = OpenWith(mrhiDefaultDeviceDef(), true);
    mrhiSurfaceId surface = MakeSurface(1);
    mrhiSurfaceConfig good = Config(surface);
    mrhiSurfaceConfig bad[20];
    size_t count = 0;
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        bad[i] = good;
    }
    bad[count++].cookie = 0;
    bad[count].viewFormats[0] = mrhi_formatNone;
    bad[count++].color.format = 200;
    bad[count++].color.primaries = 3;
    bad[count++].color.transfer = 3;
    bad[count++].color.range = 2;
    bad[count++].usage = 0;
    bad[count++].usage = 0x40;
    bad[count++].usage = mrhi_textureTransient | mrhi_textureRenderTarget;
    bad[count++].viewFormats[3] = mrhi_formatRgba8Unorm;
    bad[count++].width = 0;
    bad[count++].height = 0;
    bad[count++].presentMode = 0;
    bad[count++].presentMode = mrhi_presentFifo | mrhi_presentMailbox;
    bad[count++].presentMode = 8;
    bad[count++].alphaMode = 0;
    bad[count++].alphaMode = mrhi_alphaOpaque | mrhi_alphaPremultiplied;
    bad[count++].alphaMode = 4;
    for (size_t i = 0; i < count; ++i)
    {
        CHECK(mrhiConfigureSurface(device, &bad[i]) == mrhi_errorInvalid, "invalid");
    }
    CHECK(mrhiConfigureSurface(device, nullptr) == mrhi_errorInvalid, "no config");
    CHECK(mrhiConfigureSurface(nullptr, &good) == mrhi_errorInvalid, "no device");
    CHECK(mrhiGetDeviceMisuse(device) == count + 1, "each counted");
    Close(device);
}

// A well-formed config the surface or the device cannot take.
static void TestUnsupported(void)
{
    mrhiDevice* device = OpenWith(mrhiDefaultDeviceDef(), true);
    mrhiSurfaceId surface = MakeSurface(1);
    mrhiSurfaceConfig config = Config(surface);
    config.color.format = mrhi_formatRgba8Unorm;
    config.viewFormats[0] = mrhi_formatRgba8UnormSrgb;
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_errorUnsupported, "a format not reported");
    config = Config(surface);
    config.color.range = mrhi_rangeExtended;
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_errorUnsupported, "a range not reported");
    config = Config(surface);
    config.color.primaries = mrhi_primariesDisplayP3;
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_errorUnsupported, "primaries not reported");
    config = Config(surface);
    config.color.transfer = mrhi_transferLinear;
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_errorUnsupported, "linear BGRA8");
    config = Config(surface);
    config.presentMode = mrhi_presentMailbox;
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_errorUnsupported, "mailbox");
    config = Config(surface);
    config.alphaMode = mrhi_alphaPremultiplied;
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_errorUnsupported, "premultiplied");
    config = Config(surface);
    config.usage |= mrhi_textureSampled;
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_errorUnsupported, "a use not reported");
    config = Config(surface);
    config.usage |= mrhi_textureStorage;
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_errorUnsupported,
          "storage the surface reports but BGRA8 cannot take");
    config = Config(surface);
    config.width = 8193;
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_errorUnsupported, "too wide");
    config = Config(surface);
    config.height = 8193;
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_errorUnsupported, "too tall");
    config = Config(surface);
    config.width = 8192;
    config.height = 8192;
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_success, "the largest");
    mrhiChain critical = {.next = nullptr, .type = 0x7000u};
    config.next = &critical;
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_errorUnsupported,
          "an unknown critical extension");
    mrhiSurfaceId elsewhere = MakeSurface(2);
    config = Config(elsewhere);
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_errorUnsupported,
          "a surface the adapter does not present to");
    config = Config((mrhiSurfaceId){0, 0});
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_errorStale, "no surface");
    CHECK(mrhiGetDeviceMisuse(device) == 0, "none counted");
    Close(device);
}

// One device at a time, states and the device's limit.
static void TestOwnership(void)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.deviceLimits.surfaces = 1;
    mrhiDevice* device = OpenWith(def, false);
    mrhiSurfaceId surface = MakeSurface(1);
    mrhiSurfaceConfig config = Config(surface);
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_errorState, "a device still opening");
    mrhiInstanceNotification record;
    CHECK(mrhiNextInstanceNotification(s_instance, &record) == mrhi_success, "ready");
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_success, "configured");
    mrhiSurfaceId other = MakeSurface(1);
    mrhiSurfaceConfig otherConfig = Config(other);
    CHECK(mrhiConfigureSurface(device, &otherConfig) == mrhi_errorCapacity, "the device's limit");
    mrhiDevice* second = OpenSecond();
    CHECK(mrhiConfigureSurface(second, &config) == mrhi_errorState, "held by another device");
    CHECK(mrhiUnconfigureSurface(second, surface) == mrhi_errorState, "not the second's");
    CHECK(mrhiConfigureSurface(second, &otherConfig) == mrhi_success, "another surface there");
    CHECK(mrhiDestroySurface(s_instance, surface) == mrhi_success, "ended while configured");
    CHECK(mrhiConfigureSurface(device, &otherConfig) == mrhi_errorState, "still the second's");
    mrhiDestroyDevice(second);
    CHECK(mrhiConfigureSurface(device, &otherConfig) == mrhi_success,
          "free once the second device is gone");
    Close(device);
}

// A failing driver leaves the surface unconfigured, and loses no slot.
static void TestFailures(void)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.deviceLimits.surfaces = 1;
    s_adapter.objectsBeforeFailure = 1;
    mrhiDevice* device = OpenWith(def, true);
    mrhiSurfaceId surface = MakeSurface(1);
    mrhiSurfaceConfig config = Config(surface);
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_success, "the one object");
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_errorPlatform, "reconfiguring fails");
    CHECK(mrhiUnconfigureSurface(device, surface) == mrhi_errorState, "left unconfigured");
    CHECK(mrhiConfigureSurface(device, &config) == mrhi_errorPlatform, "the driver fails");
    CHECK(mrhiUnconfigureSurface(device, surface) == mrhi_errorState, "still unconfigured");
    Close(device);
}

int main(void)
{
    ResetAdapter();
    TestConfigure();
    TestInvalid();
    TestUnsupported();
    TestOwnership();
    TestFailures();
    return s_failures == 0 ? 0 : 1;
}
