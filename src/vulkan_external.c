// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Vulkan objects made outside the library (maul-rhi/vulkan.h, mrhi-0018): the
// functions an OpenXR program calls, answered by the Vulkan driver and
// unsupported on every other.

#include "device_core.h"
#include "instance_core.h"

#include "maul-rhi/vulkan.h"

#ifdef MAUL_RHI_VULKAN_DRIVER
#include "driver_vulkan.h"
#include "vulkan_device.h"
#endif

mrhiResult mrhiDescribeVulkanDevice(mrhiInstance* instance, const mrhiDeviceDef* def,
                                    void** createInfoOut, void** physicalDeviceOut)
{
    if (instance == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || createInfoOut == nullptr || physicalDeviceOut == nullptr)
    {
        return mrhiMisuse(instance);
    }
    *createInfoOut = nullptr;
    *physicalDeviceOut = nullptr;
#ifdef MAUL_RHI_VULKAN_DRIVER
    if (!mrhiIsVulkanDriver(&instance->driver))
    {
        return mrhi_errorUnsupported;
    }
    // Each description ends the one before, even one refused.
    mrhiForgetVulkanDevice(&instance->driver);
    mrhiResult status = mrhi_success;
    const mrhiDriverAdapter* adapter = mrhiCheckDeviceDef(instance, def, &status);
    if (adapter == nullptr)
    {
        return status;
    }
    void* info = nullptr;
    status = mrhiDescribeVulkanDriverDevice(&instance->driver, adapter->handle, def, &info);
    if (status == mrhi_success)
    {
        *createInfoOut = info;
        *physicalDeviceOut = mrhiVulkanPhysicalDevice(adapter->handle);
    }
    return status;
#else
    return mrhi_errorUnsupported;
#endif
}

mrhiResult mrhiGetVulkanPhysicalDevice(mrhiInstance* instance, mrhiAdapterId adapter,
                                       void** physicalDeviceOut)
{
    if (instance == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (physicalDeviceOut == nullptr)
    {
        return mrhiMisuse(instance);
    }
    *physicalDeviceOut = nullptr;
#ifdef MAUL_RHI_VULKAN_DRIVER
    if (!mrhiIsVulkanDriver(&instance->driver))
    {
        return mrhi_errorUnsupported;
    }
    const mrhiDriverAdapter* found = mrhiFindAdapter(instance, adapter);
    if (found == nullptr)
    {
        return mrhi_errorStale;
    }
    *physicalDeviceOut = mrhiVulkanPhysicalDevice(found->handle);
    return mrhi_success;
#else
    (void)adapter;
    return mrhi_errorUnsupported;
#endif
}

mrhiResult mrhiGetVulkanQueue(mrhiDevice* device, uint32_t* familyOut, uint32_t* indexOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (familyOut == nullptr || indexOut == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
#ifdef MAUL_RHI_VULKAN_DRIVER
    return mrhiVulkanDeviceQueue(&device->driver, familyOut, indexOut) ? mrhi_success
                                                                       : mrhi_errorUnsupported;
#else
    return mrhi_errorUnsupported;
#endif
}
