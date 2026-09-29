// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The device as the core sees it (mrhi-0004): its instance, what it was
// granted, its state and its driver.

#ifndef MAUL_RHI_SRC_DEVICE_CORE_H
#define MAUL_RHI_SRC_DEVICE_CORE_H

#include "capabilities_core.h"
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

// A texture as its device keeps it: its def (without its chain), its
// driver handle, and the slot of its newest view, 0 for none.
typedef struct mrhiTextureSlot
{
    uint64_t handle;
    mrhiTextureDef def;
    uint32_t firstView;
} mrhiTextureSlot;

// A view as its device keeps it: its resolved def (without its chain),
// its driver handle, its texture's slot, and the slots of its texture's
// views before and after it, 0 for none.
typedef struct mrhiViewSlot
{
    uint64_t handle;
    mrhiViewDef def;
    uint32_t texture;
    uint32_t previous;
    uint32_t next;
} mrhiViewSlot;

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
    mrhiPool textures;
    mrhiTextureSlot* textureSlots;
    mrhiPool views;
    mrhiViewSlot* viewSlots;
    // What each known format can do on this device: the adapter's
    // capabilities, with the compressed families the device was not
    // granted cleared.
    mrhiFormatCaps formatCaps[MRHI_KNOWN_FORMATS];
};

// mrhi_success for a ready device, mrhi_errorState for one that is not.
mrhiResult mrhiDeviceUsable(const mrhiDevice* device);

// Counts one misuse on the device and returns mrhi_errorInvalid.
mrhiResult mrhiDeviceMisuse(mrhiDevice* device);

// The head every object def opens with.
typedef struct mrhiDefHead
{
    uint32_t cookie;
    const mrhiChain* next;
    const char* label;
    size_t labelLength;
} mrhiDefHead;

// The head of a def pointer.
#define MRHI_DEF_HEAD(def)                                                                         \
    ((mrhiDefHead){(def)->cookie, (def)->next, (def)->label, (def)->labelLength})

// Checks an object def's cookie, extension chain and label on a live
// device: success, or the refusal (invalid input counted as misuse).
mrhiResult mrhiCheckObjectDef(mrhiDevice* device, mrhiDefHead head, uint32_t expected);

// Whether a format can take the usages on the device.
bool mrhiFormatTakes(const mrhiDevice* device, mrhiFormat format, mrhiTextureUsage usage);

// Destroys a texture's views and ends their ids.
void mrhiDestroyViewsOf(mrhiDevice* device, mrhiTextureSlot* texture);

#endif // MAUL_RHI_SRC_DEVICE_CORE_H
