// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Vulkan surfaces' sRGB images (mrhi-0017): twin images where the surface
// lists the sRGB format of every 8-bit unorm color it reports, as
// Android's swapchain does, and not where one is missing. Then what a
// surface can do from what it lists: its colors, present modes, alpha
// modes and usages, and each floor it needs to present; and the format
// a color takes.

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

// A surface that lists everything the caps read, and twin images.
static const VkSurfaceFormatKHR s_listed[] = {
    {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
};
static const VkPresentModeKHR s_modes[] = {
    VK_PRESENT_MODE_FIFO_KHR,
    VK_PRESENT_MODE_FIFO_RELAXED_KHR,
    VK_PRESENT_MODE_MAILBOX_KHR,
    VK_PRESENT_MODE_IMMEDIATE_KHR,
};
#define ALL_USAGES                                                                                 \
    (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |                            \
     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |                                \
     VK_IMAGE_USAGE_TRANSFER_DST_BIT)

static mrhiVulkanSurfaceFacts Full(void)
{
    return (mrhiVulkanSurfaceFacts){
        .formats = s_listed,
        .formatCount = 2,
        .modes = s_modes,
        .modeCount = 4,
        .alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR | VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        .usages = ALL_USAGES,
    };
}

static mrhiSurfaceCaps CapsOf(mrhiVulkanSurfaceFacts facts)
{
    mrhiSurfaceCaps caps;
    mrhiVulkanCapsOf(&facts, &caps);
    return caps;
}

static void TestCaps(void)
{
    mrhiSurfaceCaps caps = CapsOf(Full());
    CHECK(caps.presentable && caps.colorCount == 1 &&
              caps.colors[0].format == mrhi_formatBgra8Unorm && caps.twinImages && !caps.twinViews,
          "one color from a format and its sRGB twin, twin images");
    CHECK(caps.presentModes == (mrhi_presentFifo | mrhi_presentMailbox | mrhi_presentImmediate),
          "three present modes, FIFO relaxed not one");
    CHECK(caps.alphaModes == (mrhi_alphaOpaque | mrhi_alphaPremultiplied), "both alpha modes");
    CHECK(caps.usages == (mrhi_textureRenderTarget | mrhi_textureSampled | mrhi_textureStorage |
                          mrhi_textureCopySource | mrhi_textureCopyDestination),
          "every usage");
    const VkPresentModeKHR immediate[] = {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_FIFO_KHR};
    mrhiVulkanSurfaceFacts facts = Full();
    facts.modes = immediate;
    facts.modeCount = 2;
    facts.alpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    facts.usages = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    caps = CapsOf(facts);
    CHECK(caps.presentable && caps.presentModes == (mrhi_presentFifo | mrhi_presentImmediate) &&
              caps.alphaModes == mrhi_alphaOpaque && caps.usages == mrhi_textureRenderTarget,
          "FIFO and immediate, inherited alpha as opaque, render targets only");
    facts = Full();
    facts.formatCount = 1;
    facts.mutableFormat = true;
    caps = CapsOf(facts);
    CHECK(caps.presentable && caps.twinViews && !caps.twinImages, "sRGB through twin views");
}

static void TestFloors(void)
{
    const VkPresentModeKHR mailbox[] = {VK_PRESENT_MODE_MAILBOX_KHR};
    mrhiVulkanSurfaceFacts facts[5] = {Full(), Full(), Full(), Full(), Full()};
    facts[0].formatCount = 0;
    facts[1].modes = mailbox;
    facts[1].modeCount = 1;
    facts[2].alpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    facts[3].usages = ALL_USAGES & ~(VkImageUsageFlags)VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    facts[4].formatCount = 1;
    static const char* const floors[5] = {"no color", "no FIFO", "no opaque alpha",
                                          "no render targets", "no way to sRGB"};
    for (int i = 0; i < 5; ++i)
    {
        mrhiSurfaceCaps caps = CapsOf(facts[i]);
        CHECK(!caps.presentable && caps.colorCount == 0 && caps.presentModes == 0, floors[i]);
    }
}

static void TestPick(void)
{
    const VkSurfaceFormatKHR formats[] = {
        {VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_R16G16B16A16_SFLOAT, VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT},
    };
    VkSurfaceFormatKHR picked = {0};
    CHECK(mrhiVulkanPickFormat(formats, 3, s_rgba, &picked) &&
              picked.format == VK_FORMAT_R8G8B8A8_UNORM,
          "a unorm color its own format, over its sRGB twin listed first");
    mrhiSurfaceColor srgb = s_rgba;
    srgb.format = mrhi_formatRgba8UnormSrgb;
    CHECK(mrhiVulkanPickFormat(formats, 3, srgb, &picked) &&
              picked.format == VK_FORMAT_R8G8B8A8_SRGB,
          "an sRGB color its sRGB format");
    CHECK(mrhiVulkanPickFormat(formats, 1, s_rgba, &picked) &&
              picked.format == VK_FORMAT_R8G8B8A8_SRGB,
          "a unorm color its sRGB twin where only that is listed");
    CHECK(!mrhiVulkanPickFormat(formats, 3, s_bgra, &picked), "a color not listed");
}

int main(void)
{
    TestAndroid();
    TestMissing();
    TestCaps();
    TestFloors();
    TestPick();
    return s_failures == 0 ? 0 : 1;
}
