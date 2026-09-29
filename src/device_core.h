// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The device as the core sees it (mrhi-0004): its instance, what it was
// granted, its state and its driver.

#ifndef MAUL_RHI_SRC_DEVICE_CORE_H
#define MAUL_RHI_SRC_DEVICE_CORE_H

#include "driver.h"

#include "maul-rhi/device.h"

struct mrhiDevice
{
    mrhiInstance* instance;
    mrhiAllocator allocator;
    mrhiFeatures features;
    mrhiLimits limits;
    mrhiDeviceLimits deviceLimits;
    mrhiDeviceState state;
    // The open request, answered when the device leaves opening.
    uint32_t request;
    // Calls refused as invalid input.
    uint64_t misuse;
    mrhiDeviceDriver driver;
};

#endif // MAUL_RHI_SRC_DEVICE_CORE_H
