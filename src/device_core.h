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

// Where a device texture or buffer was imported: the frame's serial and
// the frame's slot for it.
typedef struct mrhiImport
{
    uint32_t frame;
    uint32_t resource;
} mrhiImport;

// A buffer as its device keeps it.
typedef struct mrhiBufferSlot
{
    uint64_t handle;
    uint64_t size;
    mrhiBufferUsage usage;
    mrhiImport import;
    // The state frames leave it in.
    mrhiResourceState state;
} mrhiBufferSlot;

// A texture as its device keeps it: its def (without its chain), its
// driver handle, and the slot of its newest view, 0 for none.
typedef struct mrhiTextureSlot
{
    uint64_t handle;
    mrhiTextureDef def;
    uint32_t firstView;
    mrhiImport import;
    // The state frames leave it in.
    mrhiResourceState state;
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

// What a frame resource is.
typedef enum mrhiFrameResourceKind
{
    mrhiFrameTexture,
    mrhiFrameBuffer,
    mrhiImportedTexture,
    mrhiImportedBuffer,
} mrhiFrameResourceKind;

// A resource of the open frame: a declared texture's def or buffer's
// size (without their chains and labels), or the id of an imported
// device texture or buffer.
typedef struct mrhiFrameResource
{
    mrhiFrameResourceKind kind;
    mrhiTextureDef texture;
    uint64_t size;
    uint32_t index1;
    uint32_t generation;
    // Whether a pass declared so far writes it; imports count as written.
    bool written;
    // Whether a kept pass needs it, and the usages kept passes make of
    // it, found by the compile, with the plan's transience, its first
    // and last kept passes (0 for none) and the state it ends in.
    bool needed;
    uint32_t usage;
    bool transient;
    uint32_t firstPass;
    uint32_t lastPass;
    mrhiResourceState finalState;
    // Where the compile placed a declared resource in the frame's memory.
    bool placed;
    uint64_t memoryOffset;
    uint64_t memoryBytes;
} mrhiFrameResource;

// A part of a resource in one state, in the compile's map of it.
typedef struct mrhiBox
{
    uint32_t baseMip;
    uint32_t mipCount;
    uint32_t baseLayer;
    uint32_t layerCount;
    uint8_t planes;
    mrhiResourceState state;
} mrhiBox;

// The uses a pass makes of a resource beyond the access kinds.
enum
{
    mrhiUseColorTarget = 16,
    mrhiUseResolve = 17,
    mrhiUseDepthTarget = 18,
};

// A use a pass makes of a resource: its slot in the frame, the access
// kind or target use, whether it reads or writes what is there, and the
// part of a texture it covers.
typedef struct mrhiFrameUse
{
    uint32_t resource;
    uint8_t use;
    // The state it leaves its part in, and the planes of that part: 1
    // for color or depth, 2 for stencil.
    mrhiResourceState state;
    uint8_t planes;
    bool reads;
    bool writes;
    uint32_t baseMip;
    uint32_t mipCount;
    uint32_t baseLayer;
    uint32_t layerCount;
} mrhiFrameUse;

// A pass of the open frame: its class, its uses in the frame's use
// table, its targets as declared, and whether the compile kept it.
typedef struct mrhiFramePass
{
    mrhiPassClass passClass;
    bool neverCull;
    bool kept;
    uint32_t firstUse;
    uint32_t useCount;
    mrhiColorTarget colorTargets[MRHI_COLOR_TARGETS];
    uint32_t colorTargetCount;
    mrhiDepthTarget depthTarget;
    // The stores the compile derived for its targets.
    mrhiStoreOp colorStores[MRHI_COLOR_TARGETS];
    mrhiStoreOp depthStore;
    mrhiStoreOp stencilStore;
} mrhiFramePass;

// A surface as the device that configured it keeps it: the surface, the
// driver's swapchain handle, and the configuration (without its chain).
typedef struct mrhiSwapchainSlot
{
    mrhiSurfaceId surface;
    uint64_t handle;
    mrhiSurfaceConfig config;
} mrhiSwapchainSlot;

struct mrhiDevice
{
    mrhiInstance* instance;
    // The driver handle of the adapter it was opened on.
    uint64_t adapter;
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
    // The surfaces it configured.
    mrhiPool swapchains;
    mrhiSwapchainSlot* swapchainSlots;
    // Frames: whether one is open and its serial, never 0, its resources,
    // the last token given, and the tokens of the frames the GPU has not
    // finished, at most framesInFlight.
    bool frameOpen;
    bool frameCompiled;
    uint32_t frameSerial;
    mrhiFrameResource* frameResources;
    uint32_t frameResourceCount;
    mrhiFramePass* framePasses;
    uint32_t framePassCount;
    mrhiFrameUse* frameUses;
    uint32_t frameUseCount;
    // The compile's plan: the barriers in the order they run, and room to
    // sort them and to map one resource's states.
    mrhiBarrier* frameBarriers;
    mrhiBarrier* frameBarrierScratch;
    uint32_t frameBarrierCount;
    uint32_t* frameCounts;
    mrhiBox* frameBoxes;
    uint32_t frameBoxLimit;
    // The bytes the placed resources take together, and room to order
    // the placed resources a new one meets.
    uint64_t frameMemory;
    uint32_t* frameOrder;
    uint32_t lastToken;
    uint32_t* running;
    uint32_t runningCount;
    // A ring of deviceLimits.notifications records.
    mrhiDeviceNotification* queue;
    uint32_t queueHead;
    uint32_t queueCount;
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

// Checks what a texture def says of its shape on a live device (its
// cookie, chain and label, format, size, layers, mips, samples and view
// formats): success, or the refusal, invalid input counted as misuse.
mrhiResult mrhiCheckTextureShape(mrhiDevice* device, const mrhiTextureDef* def);

// Checks what a buffer def says of its shape on a live device (its
// cookie, chain and label, and size): success, or the refusal.
mrhiResult mrhiCheckBufferShape(mrhiDevice* device, const mrhiBufferDef* def);

// Checks a texture def's usages, and that its format takes them and its
// sample count on the device: success, or the refusal.
mrhiResult mrhiCheckTextureUsage(mrhiDevice* device, const mrhiTextureDef* def);

// Whether a format has an aspect; every format has mrhi_aspectAll.
bool mrhiFormatHasAspect(mrhiFormat format, mrhiTextureAspect aspect);

// A count, with MRHI_REMAINING resolved to what follows the base.
uint32_t mrhiResolveCount(uint32_t count, uint32_t base, uint32_t total);

// Whether a range of at least one fits in the total.
bool mrhiIsRangeValid(uint32_t base, uint32_t count, uint32_t total);

// Whether a frame resource is an imported device object.
bool mrhiIsImported(const mrhiFrameResource* resource);

// The usage bit of a texture or buffer a use needs.
uint32_t mrhiUsageOf(const mrhiFrameResource* resource, uint8_t use);

// Whether a state writes what is there.
bool mrhiStateWrites(mrhiResourceState state);

// The planes of a format: 3 with stencil, else 1.
uint8_t mrhiFormatPlanes(mrhiFormat format);

// Plans the kept passes' barriers and each resource's lifetime,
// transience and final state: success, or mrhi_errorCapacity.
mrhiResult mrhiPlan(mrhiDevice* device);

// Derives the stores of the kept passes' targets.
void mrhiDeriveStores(mrhiDevice* device);

// Places the declared resources the kept passes use in the frame's
// memory by lifetime: success, or mrhi_errorCapacity when the offsets
// overflow.
mrhiResult mrhiPlace(mrhiDevice* device);

// Leaves each imported object in the state the submitted frame left it.
void mrhiApplyFinalStates(mrhiDevice* device);

// Compiles the open frame: culls, derives usages and checks them.
mrhiResult mrhiCompile(mrhiDevice* device);

// Whether a format can take the usages on the device.
bool mrhiFormatTakes(const mrhiDevice* device, mrhiFormat format, mrhiTextureUsage usage);

// Destroys a texture's views and ends their ids.
void mrhiDestroyViewsOf(mrhiDevice* device, mrhiTextureSlot* texture);

// Ends the configuration in a live swapchain slot: the driver's
// swapchain, the surface's record of it, and the slot.
void mrhiEndConfiguration(mrhiDevice* device, uint32_t swapchain);

// Ends every configuration the device holds.
void mrhiEndConfigurations(mrhiDevice* device);

#endif // MAUL_RHI_SRC_DEVICE_CORE_H
