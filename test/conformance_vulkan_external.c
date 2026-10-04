// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The build's Vulkan driver handed to the conformance suite as a driver
// from outside the tree (mrhi-0024), so that the external path runs the
// whole suite on a driver that does GPU work. A driver built outside
// the tree defines the same function against the installed SPI headers.

#include "driver_vulkan.h"

#include "maul-rhi/instance.h"

mrhiResult mrhiConformanceDriver(mrhiExternalDriverDef* driverOut);

mrhiResult mrhiConformanceDriver(mrhiExternalDriverDef* driverOut)
{
    mrhiInstanceLimits limits = mrhiDefaultInstanceDef().limits;
    mrhiAllocator allocator = {0};
    mrhiInstanceDriver driver = {0};
    mrhiResult status =
        mrhiCreateVulkanDriver(&allocator, nullptr, limits.notifications, limits.adapters, &driver);
    if (status != mrhi_success || driver.vtable == nullptr)
    {
        return status != mrhi_success ? status : mrhi_errorUnsupported;
    }
    *driverOut = (mrhiExternalDriverDef){
        .chain = {.next = nullptr, .type = mrhi_structExternalDriver},
        .vtable = driver.vtable,
        .driver = driver.self,
    };
    return mrhi_success;
}
