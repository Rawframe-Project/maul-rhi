// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The device as the core sees it (mrhi-0004): its instance, what it was
// granted, its state and its driver.

#ifndef MAUL_RHI_SRC_DEVICE_CORE_H
#define MAUL_RHI_SRC_DEVICE_CORE_H

#include "driver.h"
#include "pool.h"

#include "maul-rhi/device.h"

// A buffer as its device keeps it.
typedef struct mrhiBufferSlot
{
    uint64_t handle;
    uint64_t size;
    mrhiBufferUsage usage;
} mrhiBufferSlot;

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
    // The block the device and its tables live in.
    size_t bytes;
    // Samplers: ids, and each slot's driver handle.
    mrhiPool samplers;
    uint64_t* samplerHandles;
    // Buffers: ids, and each slot's driver handle and def.
    mrhiPool buffers;
    mrhiBufferSlot* bufferSlots;
};

// mrhi_success for a ready device, mrhi_errorState for one that is not.
mrhiResult mrhiDeviceUsable(const mrhiDevice* device);

// Counts one misuse on the device and returns mrhi_errorInvalid.
mrhiResult mrhiDeviceMisuse(mrhiDevice* device);

#endif // MAUL_RHI_SRC_DEVICE_CORE_H
