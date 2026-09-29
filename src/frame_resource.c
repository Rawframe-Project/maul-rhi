// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The open frame's resources (mrhi-0008): textures and buffers the graph
// makes, whose usages its passes will decide, and device textures and
// buffers imported once per frame. Their ids carry the frame's serial, so
// they end with it.

#include "device_core.h"

// Adds a resource to the open frame: success with its id, or the refusal.
static mrhiResult Add(mrhiDevice* device, mrhiFrameResource resource, mrhiResourceId* resourceOut)
{
    if (!device->frameOpen || device->frameCompiled)
    {
        return mrhi_errorState;
    }
    if (device->frameResourceCount == device->deviceLimits.frameResources)
    {
        return mrhi_errorCapacity;
    }
    device->frameResources[device->frameResourceCount++] = resource;
    *resourceOut = (mrhiResourceId){device->frameResourceCount, device->frameSerial};
    return mrhi_success;
}

mrhiResult mrhiDeclareTexture(mrhiDevice* device, const mrhiTextureDef* def,
                              mrhiResourceId* resourceOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || resourceOut == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiResult status = mrhiCheckTextureShape(device, def);
    if (status != mrhi_success)
    {
        return status;
    }
    if (def->usage != 0)
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiFrameResource resource = {.kind = mrhiFrameTexture, .texture = *def};
    resource.texture.next = nullptr;
    resource.texture.label = nullptr;
    resource.texture.labelLength = 0;
    return Add(device, resource, resourceOut);
}

mrhiResult mrhiDeclareBuffer(mrhiDevice* device, const mrhiBufferDef* def,
                             mrhiResourceId* resourceOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || resourceOut == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiResult status = mrhiCheckBufferShape(device, def);
    if (status != mrhi_success)
    {
        return status;
    }
    if (def->usage != 0)
    {
        return mrhiDeviceMisuse(device);
    }
    return Add(device, (mrhiFrameResource){.kind = mrhiFrameBuffer, .size = def->size},
               resourceOut);
}

// Imports a live device object once per frame: success with the frame's
// id for it, or the refusal.
static mrhiResult Import(mrhiDevice* device, mrhiImport* import, mrhiFrameResource resource,
                         mrhiResourceId* resourceOut)
{
    if (device->frameOpen && import->frame == device->frameSerial)
    {
        *resourceOut = (mrhiResourceId){import->resource, device->frameSerial};
        return mrhi_success;
    }
    mrhiResult status = Add(device, resource, resourceOut);
    if (status == mrhi_success)
    {
        *import = (mrhiImport){device->frameSerial, resourceOut->index1};
    }
    return status;
}

mrhiResult mrhiImportTexture(mrhiDevice* device, mrhiTextureId texture, mrhiResourceId* resourceOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (resourceOut == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    if (!mrhiPoolIsLive(&device->textures, texture.index1, texture.generation))
    {
        return mrhi_errorStale;
    }
    const mrhiTextureSlot* slot = &device->textureSlots[texture.index1 - 1];
    mrhiFrameResource resource = {
        .kind = mrhiImportedTexture,
        .texture = slot->def,
        .handle = slot->handle,
        .index1 = texture.index1,
        .generation = texture.generation,
        .initialState = slot->state,
    };
    return Import(device, &device->textureSlots[texture.index1 - 1].import, resource, resourceOut);
}

mrhiResult mrhiImportBuffer(mrhiDevice* device, mrhiBufferId buffer, mrhiResourceId* resourceOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (resourceOut == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    if (!mrhiPoolIsLive(&device->buffers, buffer.index1, buffer.generation))
    {
        return mrhi_errorStale;
    }
    const mrhiBufferSlot* slot = &device->bufferSlots[buffer.index1 - 1];
    mrhiFrameResource resource = {
        .kind = mrhiImportedBuffer,
        .size = slot->size,
        .handle = slot->handle,
        .index1 = buffer.index1,
        .generation = buffer.generation,
        .initialState = slot->state,
    };
    return Import(device, &device->bufferSlots[buffer.index1 - 1].import, resource, resourceOut);
}
