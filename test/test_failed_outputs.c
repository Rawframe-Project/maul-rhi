// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a call on an object leaves in its outputs when it fails: zeros,
// over values the caller filled before the call, whatever the failure.
// A call that takes a record from a queue leaves zeros when it has none
// to take too.

#include "test_device_setup.h"

#include "maul-rhi/d3d12.h"
#include "maul-rhi/metal.h"
#include "maul-rhi/shader.h"
#include "maul-rhi/surface.h"
#include "maul-rhi/vulkan.h"

#include <string.h>

// Fills an output with what a caller's earlier use might have left.
static void Fill(void* out, size_t bytes)
{
    memset(out, 0xA5, bytes);
}

// Whether a call left only zeros in an output.
static bool Zeros(const void* out, size_t bytes)
{
    const uint8_t* at = out;
    bool zeros = true;
    for (size_t i = 0; i < bytes; ++i)
    {
        zeros = zeros && at[i] == 0;
    }
    return zeros;
}

static const mrhiAdapterId kStaleAdapter = {3, 9};

static void TestInstance(void)
{
    mrhiDiagnostic diagnostic;
    Fill(&diagnostic, sizeof(diagnostic));
    CHECK(mrhiInstance_TakeDiagnostic(nullptr, &diagnostic) == mrhi_errorInvalid &&
              Zeros(&diagnostic, sizeof(diagnostic)),
          "a diagnostic, no instance");
    Fill(&diagnostic, sizeof(diagnostic));
    CHECK(mrhiInstance_TakeDiagnostic(s_instance, &diagnostic) == mrhi_empty &&
              Zeros(&diagnostic, sizeof(diagnostic)),
          "a diagnostic, none to take");
    mrhiInstanceNotification notification;
    Fill(&notification, sizeof(notification));
    CHECK(mrhiInstance_TakeNotification(nullptr, &notification) == mrhi_errorInvalid &&
              Zeros(&notification, sizeof(notification)),
          "an instance notification, no instance");
    Fill(&notification, sizeof(notification));
    CHECK(mrhiInstance_TakeNotification(s_instance, &notification) == mrhi_empty &&
              Zeros(&notification, sizeof(notification)),
          "an instance notification, none to take");
}

static void TestAdapter(void)
{
    mrhiAdapterInfo info;
    Fill(&info, sizeof(info));
    CHECK(mrhiAdapter_GetInfo(s_instance, kStaleAdapter, &info) == mrhi_errorStale &&
              Zeros(&info, sizeof(info)),
          "an adapter's info, stale");
    Fill(&info, sizeof(info));
    CHECK(mrhiAdapter_GetInfo(nullptr, kStaleAdapter, &info) == mrhi_errorInvalid &&
              Zeros(&info, sizeof(info)),
          "an adapter's info, no instance");
    mrhiFeatures features;
    Fill(&features, sizeof(features));
    CHECK(mrhiAdapter_GetFeatures(s_instance, kStaleAdapter, &features) == mrhi_errorStale &&
              Zeros(&features, sizeof(features)),
          "an adapter's features");
    mrhiLimits limits;
    Fill(&limits, sizeof(limits));
    CHECK(mrhiAdapter_GetLimits(s_instance, kStaleAdapter, &limits) == mrhi_errorStale &&
              Zeros(&limits, sizeof(limits)),
          "an adapter's limits");
    mrhiFormatCaps caps;
    Fill(&caps, sizeof(caps));
    CHECK(mrhiAdapter_GetFormatCaps(s_instance, kStaleAdapter, mrhi_formatRgba8Unorm, &caps) ==
                  mrhi_errorStale &&
              Zeros(&caps, sizeof(caps)),
          "an adapter's format caps");
    mrhiSurfaceCaps surfaceCaps;
    Fill(&surfaceCaps, sizeof(surfaceCaps));
    CHECK(mrhiSurface_GetCaps(s_instance, (mrhiSurfaceId){3, 9}, kStaleAdapter, &surfaceCaps) ==
                  mrhi_errorStale &&
              Zeros(&surfaceCaps, sizeof(surfaceCaps)),
          "a surface's caps");
}

static void TestDevice(mrhiDevice* device)
{
    mrhiFeatures features;
    Fill(&features, sizeof(features));
    CHECK(mrhiDevice_GetFeatures(nullptr, &features) == mrhi_errorInvalid &&
              Zeros(&features, sizeof(features)),
          "a device's features");
    mrhiLimits limits;
    Fill(&limits, sizeof(limits));
    CHECK(mrhiDevice_GetLimits(nullptr, &limits) == mrhi_errorInvalid &&
              Zeros(&limits, sizeof(limits)),
          "a device's limits");
    mrhiDeviceBudgets budgets;
    Fill(&budgets, sizeof(budgets));
    CHECK(mrhiDevice_GetBudgets(nullptr, &budgets) == mrhi_errorInvalid &&
              Zeros(&budgets, sizeof(budgets)),
          "a device's budgets");
    double period = 0;
    Fill(&period, sizeof(period));
    CHECK(mrhiDevice_GetTimestampPeriod(nullptr, &period) == mrhi_errorInvalid &&
              Zeros(&period, sizeof(period)),
          "a device's timestamp period");
    mrhiDeviceLossReport report;
    Fill(&report, sizeof(report));
    CHECK(mrhiDevice_GetLossReport(nullptr, &report) == mrhi_errorInvalid &&
              Zeros(&report, sizeof(report)),
          "a device's loss report");
    mrhiDiagnostic diagnostic;
    Fill(&diagnostic, sizeof(diagnostic));
    CHECK(mrhiDevice_TakeDiagnostic(nullptr, &diagnostic) == mrhi_errorInvalid &&
              Zeros(&diagnostic, sizeof(diagnostic)),
          "a device's diagnostic, no device");
    Fill(&diagnostic, sizeof(diagnostic));
    CHECK(mrhiDevice_TakeDiagnostic(device, &diagnostic) == mrhi_empty &&
              Zeros(&diagnostic, sizeof(diagnostic)),
          "a device's diagnostic, none to take");
    mrhiDeviceNotification notification;
    Fill(&notification, sizeof(notification));
    CHECK(mrhiDevice_TakeNotification(nullptr, &notification) == mrhi_errorInvalid &&
              Zeros(&notification, sizeof(notification)),
          "a device notification, no device");
    Fill(&notification, sizeof(notification));
    CHECK(mrhiDevice_TakeNotification(device, &notification) == mrhi_empty &&
              Zeros(&notification, sizeof(notification)),
          "a device notification, none to take");
    mrhiShaderInfo info;
    Fill(&info, sizeof(info));
    CHECK(mrhiShader_GetInfo(device, (mrhiShaderId){3, 9}, &info) == mrhi_errorStale &&
              Zeros(&info, sizeof(info)),
          "a shader's info");
}

// The plan's getters, with no frame open.
static void TestFrame(mrhiDevice* device)
{
    bool kept = true;
    Fill(&kept, sizeof(kept));
    CHECK(mrhiPass_IsKept(device, (mrhiPassId){1, 1}, &kept) == mrhi_errorState &&
              Zeros(&kept, sizeof(kept)),
          "whether a pass is kept");
    mrhiResourcePlan resourcePlan;
    Fill(&resourcePlan, sizeof(resourcePlan));
    CHECK(mrhiResource_GetPlan(device, (mrhiResourceId){1, 1}, &resourcePlan) == mrhi_errorState &&
              Zeros(&resourcePlan, sizeof(resourcePlan)),
          "a resource's plan");
    mrhiPassPlan passPlan;
    Fill(&passPlan, sizeof(passPlan));
    CHECK(mrhiPass_GetPlan(device, (mrhiPassId){1, 1}, &passPlan) == mrhi_errorState &&
              Zeros(&passPlan, sizeof(passPlan)),
          "a pass's plan");
    mrhiFrameUsage usage;
    Fill(&usage, sizeof(usage));
    CHECK(mrhiDevice_GetFrameUsage(nullptr, &usage) == mrhi_errorInvalid &&
              Zeros(&usage, sizeof(usage)),
          "a frame's usage");
}

// The native objects, which a device on the test driver has none of: a
// failure of whichever kind the build gives.
static void TestInterop(mrhiDevice* device)
{
    void* first = nullptr;
    void* second = nullptr;
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    Fill(&first, sizeof(first));
    Fill(&second, sizeof(second));
    CHECK(mrhiInstance_DescribeVulkanDevice(nullptr, &def, &first, &second) != mrhi_success &&
              first == nullptr && second == nullptr,
          "a Vulkan device's description");
    Fill(&first, sizeof(first));
    CHECK(mrhiAdapter_GetVulkanPhysicalDevice(nullptr, kStaleAdapter, &first) != mrhi_success &&
              first == nullptr,
          "a Vulkan physical device");
    uint32_t family = 0;
    uint32_t index = 0;
    Fill(&family, sizeof(family));
    Fill(&index, sizeof(index));
    CHECK(mrhiDevice_GetVulkanQueue(device, &family, &index) != mrhi_success && family == 0 &&
              index == 0,
          "a Vulkan queue");
    void* handles[5];
    Fill(handles, sizeof(handles));
    CHECK(mrhiDevice_GetVulkanDevice(device, &handles[0], &handles[1], &handles[2], &handles[3],
                                     &handles[4]) != mrhi_success &&
              Zeros(handles, sizeof(handles)),
          "a Vulkan device");
    mrhiVulkanTextureInfo texture;
    Fill(&texture, sizeof(texture));
    CHECK(mrhiTexture_GetVulkanTexture(device, (mrhiTextureId){3, 9}, &texture) != mrhi_success &&
              Zeros(&texture, sizeof(texture)),
          "a Vulkan texture");
    Fill(&first, sizeof(first));
    Fill(&second, sizeof(second));
    CHECK(mrhiDevice_GetD3d12Device(device, &first, &second) != mrhi_success && first == nullptr &&
              second == nullptr,
          "a D3D12 device");
    Fill(&first, sizeof(first));
    CHECK(mrhiTexture_GetD3d12Texture(device, (mrhiTextureId){3, 9}, &first) != mrhi_success &&
              first == nullptr,
          "a D3D12 texture");
    Fill(&first, sizeof(first));
    Fill(&second, sizeof(second));
    CHECK(mrhiDevice_GetMetalDevice(device, &first, &second) != mrhi_success && first == nullptr &&
              second == nullptr,
          "a Metal device");
    Fill(&first, sizeof(first));
    CHECK(mrhiTexture_GetMetalTexture(device, (mrhiTextureId){3, 9}, &first) != mrhi_success &&
              first == nullptr,
          "a Metal texture");
}

int main(void)
{
    ResetAdapter();
    mrhiDevice* device = OpenWith(mrhiDefaultDeviceDef(), true);
    TestInstance();
    TestAdapter();
    TestDevice(device);
    TestFrame(device);
    TestInterop(device);
    Close(device);
    return s_failures == 0 ? 0 : 1;
}
