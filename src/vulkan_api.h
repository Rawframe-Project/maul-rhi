// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Vulkan loader, opened at run time (mrhi-0003): the Khronos headers with
// no prototypes, and the functions the driver reaches through
// vkGetInstanceProcAddr.

#ifndef MAUL_RHI_SRC_VULKAN_API_H
#define MAUL_RHI_SRC_VULKAN_API_H

#define VK_NO_PROTOTYPES
#include <stdbool.h>
#include <vulkan/vulkan_core.h>

// The functions reached without an instance.
#define MRHI_VULKAN_GLOBAL(X)                                                                      \
    X(vkCreateInstance)                                                                            \
    X(vkEnumerateInstanceVersion)

// The functions reached through an instance.
#define MRHI_VULKAN_INSTANCE(X)                                                                    \
    X(vkDestroyInstance)                                                                           \
    X(vkEnumeratePhysicalDevices)                                                                  \
    X(vkGetPhysicalDeviceProperties2)                                                              \
    X(vkGetPhysicalDeviceFeatures2)                                                                \
    X(vkGetPhysicalDeviceQueueFamilyProperties)                                                    \
    X(vkGetPhysicalDeviceFormatProperties)                                                         \
    X(vkGetPhysicalDeviceImageFormatProperties)                                                    \
    X(vkGetPhysicalDeviceMemoryProperties)                                                         \
    X(vkCreateDevice)                                                                              \
    X(vkGetDeviceProcAddr)

// The functions reached through a device, past the loader's dispatch.
#define MRHI_VULKAN_DEVICE(X)                                                                      \
    X(vkDestroyDevice)                                                                             \
    X(vkDeviceWaitIdle)                                                                            \
    X(vkGetDeviceQueue)                                                                            \
    X(vkCreateSemaphore)                                                                           \
    X(vkDestroySemaphore)                                                                          \
    X(vkGetSemaphoreCounterValue)                                                                  \
    X(vkWaitSemaphores)                                                                            \
    X(vkGetDeviceBufferMemoryRequirements)                                                         \
    X(vkGetDeviceImageMemoryRequirements)

#define MRHI_VULKAN_FIELD(name) PFN_##name name;

// The loader's library and the functions read from it.
typedef struct mrhiVulkan
{
    void* library;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr;
    MRHI_VULKAN_GLOBAL(MRHI_VULKAN_FIELD)
    MRHI_VULKAN_INSTANCE(MRHI_VULKAN_FIELD)
} mrhiVulkan;

// A device's functions.
typedef struct mrhiVulkanDevice
{
    MRHI_VULKAN_DEVICE(MRHI_VULKAN_FIELD)
} mrhiVulkanDevice;

// Opens the loader and reads the global functions: false, with nothing
// open, when there is no loader or it lacks one of them.
bool mrhiOpenVulkan(mrhiVulkan* vulkan);

// Reads the instance's functions: false when one is missing.
bool mrhiLoadVulkanInstance(mrhiVulkan* vulkan, VkInstance instance);

// Reads a device's functions: false when one is missing.
bool mrhiLoadVulkanDevice(const mrhiVulkan* vulkan, VkDevice device, mrhiVulkanDevice* functions);

// Closes the loader.
void mrhiCloseVulkan(mrhiVulkan* vulkan);

#endif // MAUL_RHI_SRC_VULKAN_API_H
