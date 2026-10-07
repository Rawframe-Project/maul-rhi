// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Vulkan swapchains' sizes and shapes: a size the window takes, its own
// where the surface fixes the extent (on Android any within the bounds,
// which its compositor scales), else one within the bounds, both bounds
// taken; at least three images within the surface's counts; the
// present mode and alpha asked, opaque or inherited alpha otherwise,
// and the surface's transform.

#include "test_harness.h"
#include "vulkan_swapchain.h"

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

int main(void)
{
    TestSizes();
    TestShapes();
    return s_failures == 0 ? 0 : 1;
}
