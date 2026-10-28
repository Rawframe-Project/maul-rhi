// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Vulkan swapchains' sizes and shapes: a size the window takes, its own
// where the surface fixes the extent (on Android any within the bounds,
// which its compositor scales), else one within the bounds, both bounds
// taken; at least three images within the surface's counts; the
// present mode and alpha asked, opaque or inherited alpha otherwise,
// and the surface's transform. A reconfiguration retires the old
// swapchain only once vkCreateSwapchainKHR has been called with it,
// whatever it answers; a refusal before that keeps the old one.

#include "test_harness.h"
#include "vulkan_swapchain.h"

#include <string.h>

static void TestSizes(void)
{
    // A window that fixes its extent, within wide bounds.
    VkSurfaceCapabilitiesKHR caps = {
        .currentExtent = {100, 80},
        .minImageExtent = {1, 1},
        .maxImageExtent = {4096, 4096},
    };
    CHECK(mrhiVulkanTakesSize(&caps, (VkExtent2D){100, 80}), "the window's own size");
#ifdef __ANDROID__
    CHECK(mrhiVulkanTakesSize(&caps, (VkExtent2D){50, 50}),
          "on Android, another within the bounds");
#else
    CHECK(!mrhiVulkanTakesSize(&caps, (VkExtent2D){101, 80}) &&
              !mrhiVulkanTakesSize(&caps, (VkExtent2D){100, 79}) &&
              !mrhiVulkanTakesSize(&caps, (VkExtent2D){50, 50}),
          "no other where it fixes the extent, though within the bounds");
#endif
    // A surface that leaves the extent to the swapchain.
    caps.currentExtent = (VkExtent2D){UINT32_MAX, UINT32_MAX};
    caps.minImageExtent = (VkExtent2D){2, 3};
    caps.maxImageExtent = (VkExtent2D){200, 150};
    CHECK(mrhiVulkanTakesSize(&caps, (VkExtent2D){2, 3}) &&
              mrhiVulkanTakesSize(&caps, (VkExtent2D){200, 150}) &&
              mrhiVulkanTakesSize(&caps, (VkExtent2D){50, 50}),
          "any size within the bounds, both taken");
    CHECK(!mrhiVulkanTakesSize(&caps, (VkExtent2D){1, 3}) &&
              !mrhiVulkanTakesSize(&caps, (VkExtent2D){2, 2}) &&
              !mrhiVulkanTakesSize(&caps, (VkExtent2D){201, 150}) &&
              !mrhiVulkanTakesSize(&caps, (VkExtent2D){200, 151}),
          "none past them");
}

static VkSwapchainCreateInfoKHR Shape(const VkSurfaceCapabilitiesKHR* caps,
                                      mrhiPresentModes presentMode, mrhiAlphaModes alphaMode)
{
    const mrhiSurfaceConfig config = {.presentMode = presentMode, .alphaMode = alphaMode};
    VkSwapchainCreateInfoKHR info = {0};
    mrhiVulkanSwapchainShape(caps, &config, &info);
    return info;
}

static void TestShapes(void)
{
    VkSurfaceCapabilitiesKHR caps = {
        .minImageCount = 2,
        .supportedCompositeAlpha =
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR | VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        .currentTransform = VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR,
    };
    VkSwapchainCreateInfoKHR info = Shape(&caps, mrhi_presentFifo, mrhi_alphaOpaque);
    CHECK(info.minImageCount == 3 && info.presentMode == VK_PRESENT_MODE_FIFO_KHR &&
              info.compositeAlpha == VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR &&
              info.preTransform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR,
          "three images, FIFO, opaque, the surface's transform");
    caps.minImageCount = 4;
    info = Shape(&caps, mrhi_presentMailbox, mrhi_alphaPremultiplied);
    CHECK(info.minImageCount == 4 && info.presentMode == VK_PRESENT_MODE_MAILBOX_KHR &&
              info.compositeAlpha == VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
          "the surface's own least count, mailbox, premultiplied");
    caps.minImageCount = 1;
    caps.maxImageCount = 2;
    caps.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    info = Shape(&caps, mrhi_presentImmediate, mrhi_alphaOpaque);
    CHECK(info.minImageCount == 2 && info.presentMode == VK_PRESENT_MODE_IMMEDIATE_KHR &&
              info.compositeAlpha == VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
          "no more than its most, immediate, inherited alpha for opaque");
}

// The fake driver's answers and what it was asked.
static VkResult s_capsResult;
static uint32_t s_formatCount;
static VkResult s_createResult;
static VkResult s_imagesResult;
static uint32_t s_created;
static uint32_t s_destroyed;
static uint64_t s_nextHandle;

static uint64_t NextHandle(void)
{
    return ++s_nextHandle;
}

static VKAPI_ATTR VkResult VKAPI_CALL Caps(VkPhysicalDevice physical, VkSurfaceKHR surface,
                                           VkSurfaceCapabilitiesKHR* caps)
{
    (void)physical;
    (void)surface;
    *caps = (VkSurfaceCapabilitiesKHR){
        .minImageCount = 2,
        .maxImageCount = 8,
        .currentExtent = {UINT32_MAX, UINT32_MAX},
        .minImageExtent = {1, 1},
        .maxImageExtent = {4096, 4096},
        .supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
    };
    return s_capsResult;
}

static VKAPI_ATTR VkResult VKAPI_CALL Formats(VkPhysicalDevice physical, VkSurfaceKHR surface,
                                              uint32_t* count, VkSurfaceFormatKHR* formats)
{
    (void)physical;
    (void)surface;
    *count = *count < s_formatCount ? *count : s_formatCount;
    for (uint32_t i = 0; i < *count; ++i)
    {
        formats[i] =
            (VkSurfaceFormatKHR){VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchain(VkDevice device,
                                                      const VkSwapchainCreateInfoKHR* info,
                                                      const VkAllocationCallbacks* callbacks,
                                                      VkSwapchainKHR* swapchain)
{
    (void)device;
    (void)info;
    (void)callbacks;
    ++s_created;
    uint64_t handle = s_createResult == VK_SUCCESS ? NextHandle() : 0;
    memcpy((void*)swapchain, &handle, sizeof(handle));
    return s_createResult;
}

static VKAPI_ATTR void VKAPI_CALL DestroySwapchain(VkDevice device, VkSwapchainKHR swapchain,
                                                   const VkAllocationCallbacks* callbacks)
{
    (void)device;
    (void)callbacks;
    s_destroyed += swapchain != VK_NULL_HANDLE ? 1 : 0;
}

static VKAPI_ATTR VkResult VKAPI_CALL Images(VkDevice device, VkSwapchainKHR swapchain,
                                             uint32_t* count, VkImage* images)
{
    (void)device;
    (void)swapchain;
    *count = 3;
    for (uint32_t i = 0; i < 3; ++i)
    {
        uint64_t handle = NextHandle();
        memcpy((void*)&images[i], &handle, sizeof(handle));
    }
    return s_imagesResult;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateSemaphore(VkDevice device,
                                                      const VkSemaphoreCreateInfo* info,
                                                      const VkAllocationCallbacks* callbacks,
                                                      VkSemaphore* semaphore)
{
    (void)device;
    (void)info;
    (void)callbacks;
    uint64_t handle = NextHandle();
    memcpy((void*)semaphore, &handle, sizeof(handle));
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroySemaphore(VkDevice device, VkSemaphore semaphore,
                                                   const VkAllocationCallbacks* callbacks)
{
    (void)device;
    (void)semaphore;
    (void)callbacks;
}

static VKAPI_ATTR VkResult VKAPI_CALL WaitIdle(VkQueue queue)
{
    (void)queue;
    return VK_SUCCESS;
}

static mrhiVulkan s_vulkan;
static mrhiVulkanDevice s_api;
static mrhiVulkanSwapchain s_slots[2];

// Swapchains of the fake driver in capacity slots, one configured as the
// old one: its handle.
static uint64_t Configured(mrhiVulkanSwapchains* swapchains, uint32_t capacity)
{
    s_vulkan = (mrhiVulkan){
        .vkGetPhysicalDeviceSurfaceCapabilitiesKHR = Caps,
        .vkGetPhysicalDeviceSurfaceFormatsKHR = Formats,
    };
    s_api = (mrhiVulkanDevice){
        .vkCreateSwapchainKHR = CreateSwapchain,
        .vkDestroySwapchainKHR = DestroySwapchain,
        .vkGetSwapchainImagesKHR = Images,
        .vkCreateSemaphore = CreateSemaphore,
        .vkDestroySemaphore = DestroySemaphore,
        .vkQueueWaitIdle = WaitIdle,
    };
    memset(s_slots, 0, sizeof(s_slots));
    *swapchains = (mrhiVulkanSwapchains){
        .vulkan = &s_vulkan,
        .api = &s_api,
        .slots = s_slots,
        .capacity = capacity,
    };
    s_capsResult = VK_SUCCESS;
    s_formatCount = 1;
    s_createResult = VK_SUCCESS;
    s_imagesResult = VK_SUCCESS;
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    config.color = (mrhiSurfaceColor){.format = mrhi_formatBgra8Unorm};
    config.width = 64;
    config.height = 64;
    uint64_t old = 0;
    CHECK(mrhiVulkanConfigure(swapchains, VK_NULL_HANDLE, &config, 0, &old) == mrhi_success &&
              old != 0,
          "the old swapchain");
    s_created = 0;
    s_destroyed = 0;
    return old;
}

// A reconfiguration refused before vkCreateSwapchainKHR: the status, and
// whether it kept the old swapchain, uncalled and undestroyed.
static bool Kept(mrhiVulkanSwapchains* swapchains, uint64_t old, const mrhiSurfaceConfig* config,
                 mrhiResult expected)
{
    uint64_t handle = 1;
    mrhiResult status = mrhiVulkanConfigure(swapchains, VK_NULL_HANDLE, config, old, &handle);
    bool kept = status == expected && handle == old && s_created == 0 && s_destroyed == 0;
    mrhiVulkanSwapchainsDestroy(swapchains);
    return kept;
}

static void TestRefusalsKeep(void)
{
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    config.color = (mrhiSurfaceColor){.format = mrhi_formatBgra8Unorm};
    config.width = 128;
    config.height = 128;
    mrhiVulkanSwapchains swapchains;
    uint64_t old = Configured(&swapchains, 1);
    CHECK(Kept(&swapchains, old, &config, mrhi_errorCapacity), "no free slot");
    old = Configured(&swapchains, 2);
    s_capsResult = VK_ERROR_SURFACE_LOST_KHR;
    CHECK(Kept(&swapchains, old, &config, mrhi_errorOutOfDate), "the caps query fails");
    old = Configured(&swapchains, 2);
    mrhiSurfaceConfig wide = config;
    wide.width = 5000;
    CHECK(Kept(&swapchains, old, &wide, mrhi_errorOutOfDate), "a size the window does not take");
    old = Configured(&swapchains, 2);
    s_formatCount = 0;
    CHECK(Kept(&swapchains, old, &config, mrhi_errorUnsupported), "no surface format");
    old = Configured(&swapchains, 2);
    mrhiSurfaceConfig twin = config;
    twin.viewFormats[0] = mrhi_formatBgra8UnormSrgb;
    CHECK(Kept(&swapchains, old, &twin, mrhi_errorUnsupported),
          "a twin view without a mutable format");
}

// Once vkCreateSwapchainKHR has been called with the old swapchain, it
// is retired: destroyed after the new one is made, and when the call or
// the new one's images fail.
static void TestRetired(void)
{
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    config.color = (mrhiSurfaceColor){.format = mrhi_formatBgra8Unorm};
    config.width = 128;
    config.height = 128;
    mrhiVulkanSwapchains swapchains;
    uint64_t old = Configured(&swapchains, 2);
    uint64_t handle = 0;
    CHECK(mrhiVulkanConfigure(&swapchains, VK_NULL_HANDLE, &config, old, &handle) == mrhi_success &&
              handle != 0 && handle != old && s_created == 1 && s_destroyed == 1,
          "replaced");
    mrhiVulkanSwapchainsDestroy(&swapchains);
    old = Configured(&swapchains, 2);
    s_createResult = VK_ERROR_OUT_OF_HOST_MEMORY;
    handle = 1;
    CHECK(mrhiVulkanConfigure(&swapchains, VK_NULL_HANDLE, &config, old, &handle) ==
                  mrhi_errorCapacity &&
              handle == 0 && s_created == 1 && s_destroyed == 1,
          "retired by a failed creation");
    mrhiVulkanSwapchainsDestroy(&swapchains);
    old = Configured(&swapchains, 2);
    s_imagesResult = VK_ERROR_OUT_OF_DEVICE_MEMORY;
    handle = 1;
    CHECK(mrhiVulkanConfigure(&swapchains, VK_NULL_HANDLE, &config, old, &handle) ==
                  mrhi_errorCapacity &&
              handle == 0 && s_created == 1 && s_destroyed == 2,
          "retired, the new one's images failing");
    mrhiVulkanSwapchainsDestroy(&swapchains);
}

int main(void)
{
    TestSizes();
    TestShapes();
    TestRefusalsKeep();
    TestRetired();
    return s_failures == 0 ? 0 : 1;
}
