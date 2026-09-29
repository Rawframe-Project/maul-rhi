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

// The Vulkan format a format is on a device: the depth and stencil
// format is the first of D24S8 and D32S8 the device renders to.
VkFormat mrhiVulkanFormat(const mrhiVulkan* vulkan, VkPhysicalDevice device, mrhiFormat format);

// Fills what a format can do on a device.
void mrhiGetVulkanFormatCaps(const mrhiVulkan* vulkan, VkPhysicalDevice device, mrhiFormat format,
                             mrhiFormatCaps* capsOut);

#endif // MAUL_RHI_SRC_VULKAN_ADAPTER_H
