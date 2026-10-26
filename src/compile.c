// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The frame's compile: passes are culled from the frame's outputs
// backwards, at the granularity of whole resources, the kept ones are
// planned, and the usages they make of each declared texture are
// checked against its format. Declaration order stays execution
// order. The frame's first pass is the library's own: it clears each
// buffer no frame has run with yet, the first time a frame imports it,
// so that every backend's new buffer reads as zeros.

#include "device_core.h"
#include "encoder_core.h"
#include "invariant.h"
#include "label.h"

#include <string.h>

// The library's first pass's label, which debuggers show.
static const char s_zeroLabel[] = "maul-rhi: zero new buffers";

void mrhiBeginZeroPass(mrhiDevice* device)
{
    device->framePasses[0] = (mrhiFramePass){
        .passClass = mrhi_passTransfer,
        .labelLength = MAUL_RHI_LABELS ? (uint32_t)(sizeof(s_zeroLabel) - 1) : 0,
    };
    if (MAUL_RHI_LABELS)
    {
        memcpy(device->frameLabels, s_zeroLabel, sizeof(s_zeroLabel) - 1);
    }
    device->framePassCount = MRHI_LIBRARY_PASSES;
    device->frameZeroUses = 0;
    device->frameZeroDeclared = false;
}

// Gives the first pass a whole-buffer copy destination use of each fresh
// buffer the frame imports, in the accesses its imports kept free.
static void DeclareZeroes(mrhiDevice* device)
{
    if (device->frameZeroDeclared)
    {
        return;
    }
    mrhiFramePass* pass = &device->framePasses[0];
    pass->firstUse = device->frameUseCount;
    for (uint32_t i = 0; i < device->frameResourceCount; ++i)
    {
        const mrhiFrameResource* resource = &device->frameResources[i];
        if (resource->kind != mrhiImportedBuffer ||
            !device->bufferSlots[resource->index1 - 1].fresh)
        {
            continue;
        }
        device->frameUses[device->frameUseCount++] = (mrhiFrameUse){
            .resource = i + 1,
            .use = mrhi_accessCopyDestination,
            .state = mrhi_stateCopyDestination,
            .planes = 1,
            .writes = true,
            .mipCount = 1,
            .layerCount = 1,
        };
        ++pass->useCount;
    }
    // The imports counted the fresh buffers and kept their accesses free.
    MRHI_ASSERT(pass->useCount == device->frameZeroUses);
    device->frameZeroDeclared = true;
}

// Records the first pass's clears, once the compile kept it: success, or
// mrhi_errorCapacity when the command arena is full.
static mrhiResult RecordZeroes(mrhiDevice* device)
{
    mrhiFramePass* pass = &device->framePasses[0];
    for (uint32_t i = 0; pass->kept && pass->firstChunk == 0 && i < pass->useCount; ++i)
    {
        uint32_t object = device->frameUses[pass->firstUse + i].resource;
        mrhiCommand* record = mrhiTakeCommands(device, pass, 1);
        if (record == nullptr)
        {
            return mrhi_errorCapacity;
        }
        record[0] = (mrhiCommand){
            .type = mrhiCommandClearBuffer,
            .a = object,
            .d = device->frameResources[object - 1].size,
        };
    }
    return mrhi_success;
}

void mrhiMarkZeroed(mrhiDevice* device)
{
    const mrhiFramePass* pass = &device->framePasses[0];
    for (uint32_t i = 0; pass->kept && i < pass->useCount; ++i)
    {
        const mrhiFrameResource* resource =
            &device->frameResources[device->frameUses[pass->firstUse + i].resource - 1];
        device->bufferSlots[resource->index1 - 1].fresh = false;
    }
}

// Culls the passes from the last: a pass is kept when it never culls, or
// writes an imported resource or one a kept pass after it reads. A kept
// pass needs what it reads, and gives each resource its usages.
static void Cull(mrhiDevice* device)
{
    for (uint32_t i = 0; i < device->frameResourceCount; ++i)
    {
        device->frameResources[i].needed = false;
        device->frameResources[i].usage = 0;
    }
    for (uint32_t p = device->framePassCount; p-- > 0;)
    {
        mrhiFramePass* pass = &device->framePasses[p];
        const mrhiFrameUse* uses = &device->frameUses[pass->firstUse];
        bool kept = pass->neverCull;
        for (uint32_t i = 0; i < pass->useCount; ++i)
        {
            const mrhiFrameResource* resource = &device->frameResources[uses[i].resource - 1];
            kept = kept || (uses[i].writes && (mrhiOutlivesFrame(resource) || resource->needed));
        }
        pass->kept = kept;
        for (uint32_t i = 0; i < pass->useCount && kept; ++i)
        {
            mrhiFrameResource* resource = &device->frameResources[uses[i].resource - 1];
            resource->needed = resource->needed || uses[i].reads;
            resource->usage |= mrhiUsageOf(resource, uses[i].use);
        }
    }
}

mrhiResult mrhiCompile(mrhiDevice* device)
{
    DeclareZeroes(device);
    Cull(device);
    mrhiResult status = mrhiPlan(device);
    if (status != mrhi_success)
    {
        return status;
    }
    for (uint32_t i = 0; i < device->frameResourceCount; ++i)
    {
        mrhiFrameResource* resource = &device->frameResources[i];
        if (resource->kind != mrhiFrameTexture || resource->usage == 0)
        {
            continue;
        }
        resource->usage |= resource->transient ? mrhi_textureTransient : 0u;
        mrhiTextureDef def = resource->texture;
        def.usage = resource->usage;
        status = mrhiCheckTextureUsage(device, &def);
        if (status != mrhi_success)
        {
            return status;
        }
    }
    status = mrhiPlace(device);
    if (status == mrhi_success)
    {
        status = mrhiPlanAliasing(device);
    }
    if (status != mrhi_success)
    {
        return status;
    }
    mrhiOrderPasses(device);
    mrhiDeriveStores(device);
    status = RecordZeroes(device);
    device->frameCompiled = status == mrhi_success;
    return status;
}

mrhiResult mrhiCompileFrame(mrhiDevice* device)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (!device->frameOpen || device->frameCompiled)
    {
        return mrhi_errorState;
    }
    return mrhiCompile(device);
}

mrhiResult mrhiIsPassKept(mrhiDevice* device, mrhiPassId pass, bool* keptOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (keptOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    if (!device->frameOpen || !device->frameCompiled)
    {
        return mrhi_errorState;
    }
    if (pass.generation != device->frameSerial || pass.index1 <= MRHI_LIBRARY_PASSES ||
        pass.index1 > device->framePassCount)
    {
        return mrhi_errorStale;
    }
    *keptOut = device->framePasses[pass.index1 - 1].kept;
    return mrhi_success;
}
