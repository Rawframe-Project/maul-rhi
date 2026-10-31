// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Vulkan surfaces' sRGB images: twin images where the surface lists the
// sRGB format of every 8-bit unorm color it reports, as Android's
// swapchain does, and not where one is missing. Then what a surface can
// do from what it lists: its colors, present modes, alpha modes and
// usages, and each floor it needs to present; and the format a color
// takes. Last, both readers of a surface's formats on a fake Vulkan
// whose list runs past the bound they read.

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
    CHECK(caps.presentModes ==
              (mrhi_presentFifoBit | mrhi_presentMailboxBit | mrhi_presentImmediateBit),
          "three present modes, FIFO relaxed not one");
    CHECK(caps.alphaModes == (mrhi_alphaOpaqueBit | mrhi_alphaPremultipliedBit),
          "both alpha modes");
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
    CHECK(caps.presentable &&
              caps.presentModes == (mrhi_presentFifoBit | mrhi_presentImmediateBit) &&
              caps.alphaModes == mrhi_alphaOpaqueBit && caps.usages == mrhi_textureRenderTarget,
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
    // 8-bit, but in Display P3: no 8-bit sRGB in Rec. 709.
    const VkSurfaceFormatKHR p3[] = {
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT},
        {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT},
    };
    mrhiVulkanSurfaceFacts facts[6] = {Full(), Full(), Full(), Full(), Full(), Full()};
    facts[0].formatCount = 0;
    facts[1].modes = mailbox;
    facts[1].modeCount = 1;
    facts[2].alpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    facts[3].usages = ALL_USAGES & ~(VkImageUsageFlags)VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    facts[4].formatCount = 1;
    facts[5].formats = p3;
    static const char* const floors[6] = {"no color",          "no FIFO",        "no opaque alpha",
                                          "no render targets", "no way to sRGB", "no 8-bit sRGB"};
    for (int i = 0; i < 6; ++i)
    {
        mrhiSurfaceCaps caps = CapsOf(facts[i]);
        CHECK(!caps.presentable && caps.colorCount == 0 && caps.presentModes == 0, floors[i]);
    }
}

static bool IsColor(mrhiSurfaceColor color, mrhiFormat format, mrhiColorPrimaries primaries,
                    mrhiTransfer transfer, mrhiColorRange range)
{
    return color.format == format && color.primaries == primaries && color.transfer == transfer &&
           color.range == range;
}

// More known colors than the caps hold listed before the 8-bit sRGB pair
// in Rec. 709, as VK_EXT_swapchain_colorspace may list them, in a list
// as long as the driver reads: the pair keeps a slot, the first colors
// fill the rest in order, and the surface presents, whether the pair is
// listed last or in the middle. Listed first, the pair takes its own
// place and the next colors fill the rest. Without it the surface does
// not present.
static void TestManyColors(void)
{
    static const VkFormat others[] = {VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                                      VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_B8G8R8A8_UNORM};
    static const VkColorSpaceKHR spaces[] = {
        VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT,
        VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,
        VK_COLOR_SPACE_BT709_LINEAR_EXT,
        VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT,
        VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT,
        VK_COLOR_SPACE_HDR10_ST2084_EXT,
        VK_COLOR_SPACE_BT2020_LINEAR_EXT,
    };
    enum
    {
        DISTINCT = 3 * 7
    };
    static VkSurfaceFormatKHR formats[MRHI_VULKAN_SURFACE_FORMATS];
    for (uint32_t i = 0; i < MRHI_VULKAN_SURFACE_FORMATS - 1; ++i)
    {
        uint32_t pair = i % DISTINCT;
        formats[i] = (VkSurfaceFormatKHR){others[pair / 7], spaces[pair % 7]};
    }
    const VkSurfaceFormatKHR srgb = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    formats[MRHI_VULKAN_SURFACE_FORMATS - 1] = srgb;
    mrhiVulkanSurfaceFacts facts = Full();
    facts.formats = formats;
    facts.formatCount = MRHI_VULKAN_SURFACE_FORMATS;
    facts.mutableFormat = true;
    mrhiSurfaceCaps caps = CapsOf(facts);
    CHECK(caps.presentable && caps.colorCount == MRHI_SURFACE_COLORS &&
              IsColor(caps.colors[0], mrhi_formatRgb10a2Unorm, mrhi_primariesBt709,
                      mrhi_transferSrgb, mrhi_rangeExtended) &&
              IsColor(caps.colors[MRHI_SURFACE_COLORS - 2], mrhi_formatBgra8Unorm,
                      mrhi_primariesBt709, mrhi_transferSrgb, mrhi_rangeExtended) &&
              IsColor(caps.colors[MRHI_SURFACE_COLORS - 1], mrhi_formatBgra8Unorm,
                      mrhi_primariesBt709, mrhi_transferSrgb, mrhi_rangeStandard),
          "the 8-bit sRGB pair kept after the first colors, listed last");
    // Listed in the middle, other colors after it.
    formats[MRHI_VULKAN_SURFACE_FORMATS - 1] = formats[0];
    formats[100] = srgb;
    caps = CapsOf(facts);
    CHECK(caps.presentable && caps.colorCount == MRHI_SURFACE_COLORS &&
              IsColor(caps.colors[MRHI_SURFACE_COLORS - 1], mrhi_formatBgra8Unorm,
                      mrhi_primariesBt709, mrhi_transferSrgb, mrhi_rangeStandard),
          "the 8-bit sRGB pair kept, listed in the middle");
    formats[100] = formats[100 % DISTINCT];
    // Without the pair: more distinct colors than the caps hold, none of
    // them a floor, and no slot kept past MRHI_SURFACE_COLORS.
    facts.formatCount = DISTINCT;
    caps = CapsOf(facts);
    CHECK(!caps.presentable && caps.colorCount == 0, "no 8-bit sRGB pair, no presenting");
    facts.formatCount = MRHI_VULKAN_SURFACE_FORMATS;
    formats[0] = srgb;
    caps = CapsOf(facts);
    CHECK(caps.presentable && caps.colorCount == MRHI_SURFACE_COLORS &&
              IsColor(caps.colors[0], mrhi_formatBgra8Unorm, mrhi_primariesBt709, mrhi_transferSrgb,
                      mrhi_rangeStandard) &&
              IsColor(caps.colors[1], mrhi_formatRgb10a2Unorm, mrhi_primariesBt709,
                      mrhi_transferLinear, mrhi_rangeExtended) &&
              IsColor(caps.colors[MRHI_SURFACE_COLORS - 1], mrhi_formatBgra8Unorm,
                      mrhi_primariesBt709, mrhi_transferLinear, mrhi_rangeExtended),
          "listed first, in its place, the next colors after it");
}

// A surface's formats from a fake Vulkan: LISTED of them, read from the
// start, VK_INCOMPLETE when the caller's room holds fewer; the 8-bit sRGB
// pair at s_pairAt, the others of TestManyColors' kinds.
#define LISTED 300

static uint32_t s_pairAt;
static uint32_t s_asked;

static VkSurfaceFormatKHR Listed(uint32_t i)
{
    static const VkFormat others[] = {VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                                      VK_FORMAT_R16G16B16A16_SFLOAT};
    static const VkColorSpaceKHR spaces[] = {VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT,
                                             VK_COLOR_SPACE_HDR10_ST2084_EXT};
    if (i == s_pairAt)
    {
        return (VkSurfaceFormatKHR){VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }
    return (VkSurfaceFormatKHR){others[i % 2], spaces[(i / 2) % 2]};
}

static VkResult VKAPI_CALL Formats(VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t* count,
                                   VkSurfaceFormatKHR* formats)
{
    (void)device;
    (void)surface;
    s_asked = *count;
    uint32_t written = *count < LISTED ? *count : LISTED;
    for (uint32_t i = 0; i < written; ++i)
    {
        formats[i] = Listed(i);
    }
    *count = written;
    return written < LISTED ? VK_INCOMPLETE : VK_SUCCESS;
}

static void VKAPI_CALL Families(VkPhysicalDevice device, uint32_t* count,
                                VkQueueFamilyProperties* families)
{
    (void)device;
    families[0] = (VkQueueFamilyProperties){
        .queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
        .queueCount = 1,
    };
    *count = 1;
}

static VkResult VKAPI_CALL Support(VkPhysicalDevice device, uint32_t family, VkSurfaceKHR surface,
                                   VkBool32* supported)
{
    (void)device;
    (void)family;
    (void)surface;
    *supported = VK_TRUE;
    return VK_SUCCESS;
}

static VkResult VKAPI_CALL SurfaceCaps(VkPhysicalDevice device, VkSurfaceKHR surface,
                                       VkSurfaceCapabilitiesKHR* caps)
{
    (void)device;
    (void)surface;
    *caps = (VkSurfaceCapabilitiesKHR){
        .supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    };
    return VK_SUCCESS;
}

static VkResult VKAPI_CALL Modes(VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t* count,
                                 VkPresentModeKHR* modes)
{
    (void)device;
    (void)surface;
    modes[0] = VK_PRESENT_MODE_FIFO_KHR;
    *count = 1;
    return VK_SUCCESS;
}

// A surface listing more formats than the driver reads: both readers take
// the first 256 of the incomplete list, the documented bound, so the
// 8-bit sRGB pair as the last of them is found, by the caps and by the
// format a configuration picks, and as the first past them is not.
static void TestIncompleteList(void)
{
    const mrhiVulkan vulkan = {
        .vkGetPhysicalDeviceQueueFamilyProperties = Families,
        .vkGetPhysicalDeviceSurfaceSupportKHR = Support,
        .vkGetPhysicalDeviceSurfaceCapabilitiesKHR = SurfaceCaps,
        .vkGetPhysicalDeviceSurfaceFormatsKHR = Formats,
        .vkGetPhysicalDeviceSurfacePresentModesKHR = Modes,
    };
    const uint32_t at[] = {255, 256};
    for (size_t i = 0; i < 2; ++i)
    {
        s_pairAt = at[i];
        bool read = i == 0;
        mrhiSurfaceCaps caps;
        mrhiVulkanSurfaceCaps(&vulkan, VK_NULL_HANDLE, true, true, VK_NULL_HANDLE, &caps);
        CHECK(s_asked == 256 && caps.presentable == read &&
                  (!read || IsColor(caps.colors[caps.colorCount - 1], mrhi_formatBgra8Unorm,
                                    mrhi_primariesBt709, mrhi_transferSrgb, mrhi_rangeStandard)),
              "the caps from the list's first formats");
        VkSurfaceFormatKHR format = {0};
        s_asked = 0;
        CHECK(mrhiVulkanSurfaceFormat(&vulkan, VK_NULL_HANDLE, VK_NULL_HANDLE, s_bgra, &format) ==
                      read &&
                  s_asked == 256 && (!read || format.format == VK_FORMAT_B8G8R8A8_UNORM),
              "the format a configuration picks from them");
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
    TestManyColors();
    TestIncompleteList();
    TestPick();
    return s_failures == 0 ? 0 : 1;
}
