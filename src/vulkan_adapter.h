// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Vulkan physical devices as adapters (mrhi-0003): the floor a device must
// meet to be listed, and its facts, features, limits and format caps,
// as the contract's Vulkan rows map them.

#ifndef MAUL_RHI_SRC_VULKAN_ADAPTER_H
#define MAUL_RHI_SRC_VULKAN_ADAPTER_H

#include "driver.h"
#include "vulkan_api.h"

// Describes a physical device as an adapter whose handle is the device:
// false, with nothing written, for a device below the floor.
bool mrhiDescribeVulkanAdapter(const mrhiVulkan* vulkan, VkPhysicalDevice device,
                               mrhiDriverAdapter* adapterOut);

// The depth and stencil format a device uses: the first of D24S8 and
// D32S8 it renders to.
VkFormat mrhiVulkanDepthStencil(const mrhiVulkan* vulkan, VkPhysicalDevice device);

// The Vulkan format of a format, given the device's depth and stencil
// format; VK_FORMAT_UNDEFINED for one the contract does not list.
VkFormat mrhiVulkanFormat(mrhiFormat format, VkFormat depthStencil);

// The queue family with graphics and compute a listed device has.
uint32_t mrhiVulkanQueueFamily(const mrhiVulkan* vulkan, VkPhysicalDevice device);

// Fills what a format can do on a device.
void mrhiGetVulkanFormatCaps(const mrhiVulkan* vulkan, VkPhysicalDevice device, mrhiFormat format,
                             mrhiFormatCaps* capsOut);

#endif // MAUL_RHI_SRC_VULKAN_ADAPTER_H
