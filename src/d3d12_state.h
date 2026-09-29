// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The state of a D3D12 frame's recording (mrhi-0003): its command list,
// the frame's objects by slot with each buffer's D3D12 state, the
// staging and readback buffers copies name as object 0, the descriptor
// rings the pass's targets take, and what the pass has set. Included by
// the driver's files only.

#ifndef MAUL_RHI_SRC_D3D12_STATE_H
#define MAUL_RHI_SRC_D3D12_STATE_H

#include "d3d12_resource.h"
#include "driver.h"

// The resource barriers gathered before they are recorded together.
#define MRHI_D3D12_BARRIERS 64
// The debug groups a pass holds open at once.
#define MRHI_D3D12_LABELS 32

// A frame's object: its resource, whether it is a texture (with its def)
// or a buffer, and a buffer's D3D12 state. Every buffer starts a frame
// in the common state, since D3D12 decays buffers to it when a command
// list finishes.
typedef struct mrhiD3d12Object
{
    ID3D12Resource* resource;
    const mrhiTextureDef* texture;
    D3D12_RESOURCE_STATES state;
} mrhiD3d12Object;

// A range of the readback ring a frame fills.
typedef struct mrhiD3d12Range
{
    uint64_t offset;
    uint64_t size;
} mrhiD3d12Range;

// A CPU-only descriptor ring of a frame slot: its heap's start, step,
// the descriptors it holds and those taken.
typedef struct mrhiD3d12Ring
{
    D3D12_CPU_DESCRIPTOR_HANDLE start;
    UINT step;
    uint32_t capacity;
    uint32_t taken;
} mrhiD3d12Ring;

typedef struct mrhiD3d12Recorder
{
    ID3D12Device* device;
    ID3D12GraphicsCommandList* list;
    const mrhiD3d12Objects* objects;
    const mrhiDriverFrame* frame;
    const mrhiDriverPass* pass;
    // The frame's objects by slot less one; a null resource for one no
    // pass uses.
    mrhiD3d12Object* table;
    ID3D12Resource* staging;
    ID3D12Resource* readback;
    mrhiD3d12Ring targets;
    mrhiD3d12Ring depths;
    D3D12_RESOURCE_BARRIER barriers[MRHI_D3D12_BARRIERS];
    uint32_t barrierCount;
    // The frame's next barrier to record.
    size_t barrierAt;
    uint32_t labelCount;
    // The ranges of the readback ring the frame fills.
    mrhiD3d12Range* readbacks;
    uint32_t readbackCount;
    uint32_t readbackLimit;
} mrhiD3d12Recorder;

#endif // MAUL_RHI_SRC_D3D12_STATE_H
