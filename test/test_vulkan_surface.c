// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Vulkan surfaces' sRGB images (mrhi-0017): twin images where the surface
// lists the sRGB format of every 8-bit unorm color it reports, as
// Android's swapchain does, and not where one is missing.

#include "test_harness.h"
#include "vulkan_surface.h"

static const mrhiSurfaceColor s_rgba = {.format = mrhi_formatRgba8Unorm,
                                        .primaries = mrhi_primariesBt709,
                                        .transfer = mrhi_transferSrgb,
                                        .range = mrhi_rangeStandard};
static const mrhiSurfaceColor s_bgra = {.format = mrhi_formatBgra8Unorm,
                                        .primaries = mrhi_primariesBt709,
                                        .transfer = mrhi_transferSrgb,
                                        .range = mrhi_rangeStandard};

static void TestAndroid(void)
{
    // Android 15's emulator: RGBA8 unorm and sRGB, half floats, 10 bits.
    const VkSurfaceFormatKHR formats[] = {
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_A2B10G10R10_UNORM_PACK32, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    mrhiSurfaceCaps caps = {.colors = {s_rgba}, .colorCount = 1};
    CHECK(mrhiVulkanTwinImages(formats, 4, &caps), "Android's sRGB images");
}

static void TestMissing(void)
{
    const VkSurfaceFormatKHR formats[] = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    mrhiSurfaceCaps caps = {.colors = {s_bgra, s_rgba}, .colorCount = 2};
    CHECK(!mrhiVulkanTwinImages(formats, 3, &caps), "RGBA8's sRGB format missing");
    caps.colorCount = 1;
    CHECK(mrhiVulkanTwinImages(formats, 3, &caps), "BGRA8's listed");
    const VkSurfaceFormatKHR unorm[] = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    CHECK(!mrhiVulkanTwinImages(unorm, 1, &caps), "unorm only");
    const VkSurfaceFormatKHR elsewhere[] = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT},
    };
    CHECK(!mrhiVulkanTwinImages(elsewhere, 2, &caps), "an sRGB format in another space");
    mrhiSurfaceCaps half = {.colors = {{.format = mrhi_formatRgba16Float}}, .colorCount = 1};
    CHECK(!mrhiVulkanTwinImages(formats, 3, &half), "no 8-bit color to twin");
}

int main(void)
{
    TestAndroid();
    TestMissing();
    return s_failures == 0 ? 0 : 1;
}
