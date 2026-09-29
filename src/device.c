// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Devices (mrhi-0004): made at once and opened by their driver, with
// the features and limits asked for checked against the adapter and
// the floor.

#include "allocator.h"
#include "capabilities_core.h"
#include "chain.h"
#include "device_core.h"
#include "instance_core.h"
#include "invariant.h"
#include "label.h"

#include <stdalign.h>

#define DEVICE_DEF_COOKIE 0x6D726476u

mrhiDeviceDef mrhiDefaultDeviceDef(void)
{
    mrhiDeviceDef def = {0};
    def.cookie = DEVICE_DEF_COOKIE;
    def.limits = mrhiDefaultLimits();
    def.deviceLimits.notifications = 256;
    def.deviceLimits.samplers = 256;
    def.deviceLimits.buffers = 4096;
    def.deviceLimits.textures = 4096;
    def.deviceLimits.views = 8192;
    def.deviceLimits.surfaces = 8;
    def.deviceLimits.frameResources = 1024;
    def.deviceLimits.framePasses = 256;
    def.deviceLimits.frameAccesses = 4096;
    def.deviceLimits.frameBarriers = 4096;
    return def;
}

// Checks a def against the instance, the adapter and the floor, and
// returns the adapter it names; NULL with the refusal in statusOut.
static const mrhiDriverAdapter* CheckDef(mrhiInstance* instance, const mrhiDeviceDef* def,
                                         mrhiResult* statusOut)
{
    mrhiLimits floor = mrhiDefaultLimits();
    mrhiResult chain = mrhiCheckChain(def->next, nullptr, 0, instance->limits.chainDepth);
    if (def->cookie != DEVICE_DEF_COOKIE || def->deviceLimits.notifications == 0 ||
        def->deviceLimits.samplers == 0 || def->deviceLimits.buffers == 0 ||
        def->deviceLimits.textures == 0 || def->deviceLimits.views == 0 ||
        def->deviceLimits.surfaces == 0 || def->deviceLimits.frameResources == 0 ||
        def->deviceLimits.framePasses == 0 || def->deviceLimits.frameAccesses == 0 ||
        def->deviceLimits.frameBarriers == 0 || !mrhiIsLabelValid(def->label, def->labelLength) ||
        !mrhiIsAllocatorValid(&def->allocator) || !mrhiLimitsWithin(&floor, &def->limits) ||
        chain == mrhi_errorInvalid)
    {
        *statusOut = mrhiMisuse(instance);
        return nullptr;
    }
    const mrhiDriverAdapter* adapter = mrhiFindAdapter(instance, def->adapter);
    if (chain != mrhi_success)
    {
        *statusOut = chain;
    }
    else if (adapter == nullptr)
    {
        *statusOut = mrhi_errorStale;
    }
    else if (!mrhiFeaturesWithin(&def->features, &adapter->features) ||
             !mrhiLimitsWithin(&def->limits, &adapter->limits))
    {
        *statusOut = mrhi_errorUnsupported;
    }
    else if (!mrhiHasRoomForAnswer(instance))
    {
        *statusOut = mrhi_errorCapacity;
    }
    else
    {
        return adapter;
    }
    return nullptr;
}

// Where one table of the device's block starts: its pool's two arrays
// and its payload.
typedef struct TableParts
{
    size_t generations;
    size_t nextFree;
    size_t payload;
} TableParts;

static TableParts AddTable(mrhiLayout* layout, uint32_t count, size_t payloadSize,
                           size_t payloadAlignment)
{
    TableParts parts;
    parts.generations = mrhiLayoutAdd(layout, count, sizeof(uint32_t), alignof(uint32_t));
    parts.nextFree = mrhiLayoutAdd(layout, count, sizeof(uint32_t), alignof(uint32_t));
    parts.payload = mrhiLayoutAdd(layout, count, payloadSize, payloadAlignment);
    return parts;
}

// Starts a table's pool in the block and returns its payload.
static void* InitTable(unsigned char* block, TableParts parts, mrhiPool* pool, uint32_t count)
{
    mrhiPoolInit(pool, count, (uint32_t*)(block + parts.generations),
                 (uint32_t*)(block + parts.nextFree));
    return block + parts.payload;
}

// The device's block: the struct, then its tables, sized by its limits.
static mrhiDevice* Allocate(const mrhiDeviceDef* def)
{
    const mrhiDeviceLimits* limits = &def->deviceLimits;
    mrhiLayout layout = {.size = sizeof(mrhiDevice)};
    TableParts samplers = AddTable(&layout, limits->samplers, sizeof(uint64_t), alignof(uint64_t));
    TableParts buffers =
        AddTable(&layout, limits->buffers, sizeof(mrhiBufferSlot), alignof(mrhiBufferSlot));
    TableParts textures =
        AddTable(&layout, limits->textures, sizeof(mrhiTextureSlot), alignof(mrhiTextureSlot));
    TableParts views =
        AddTable(&layout, limits->views, sizeof(mrhiViewSlot), alignof(mrhiViewSlot));
    TableParts swapchains =
        AddTable(&layout, limits->surfaces, sizeof(mrhiSwapchainSlot), alignof(mrhiSwapchainSlot));
    size_t runningAt =
        mrhiLayoutAdd(&layout, def->limits.framesInFlight, sizeof(uint32_t), alignof(uint32_t));
    size_t resourcesAt = mrhiLayoutAdd(&layout, limits->frameResources, sizeof(mrhiFrameResource),
                                       alignof(mrhiFrameResource));
    size_t passesAt =
        mrhiLayoutAdd(&layout, limits->framePasses, sizeof(mrhiFramePass), alignof(mrhiFramePass));
    size_t usesAt =
        mrhiLayoutAdd(&layout, limits->frameAccesses, sizeof(mrhiFrameUse), alignof(mrhiFrameUse));
    size_t barriersAt =
        mrhiLayoutAdd(&layout, limits->frameBarriers, sizeof(mrhiBarrier), alignof(mrhiBarrier));
    size_t scratchAt =
        mrhiLayoutAdd(&layout, limits->frameBarriers, sizeof(mrhiBarrier), alignof(mrhiBarrier));
    size_t countsAt = mrhiLayoutAdd(&layout, (size_t)limits->framePasses + 2, sizeof(uint32_t),
                                    alignof(uint32_t));
    size_t boxLimit = (size_t)limits->frameAccesses * 4 + 16;
    size_t boxesAt = mrhiLayoutAdd(&layout, boxLimit, sizeof(mrhiBox), alignof(mrhiBox));
    size_t queueAt = mrhiLayoutAdd(&layout, limits->notifications, sizeof(mrhiDeviceNotification),
                                   alignof(mrhiDeviceNotification));
    unsigned char* block =
        layout.overflow ? nullptr : mrhiAllocate(&def->allocator, layout.size, alignof(mrhiDevice));
    if (block == nullptr)
    {
        return nullptr;
    }
    mrhiDevice* device = (mrhiDevice*)block;
    *device = (mrhiDevice){.bytes = layout.size};
    device->samplerHandles = InitTable(block, samplers, &device->samplers, limits->samplers);
    device->bufferSlots = InitTable(block, buffers, &device->buffers, limits->buffers);
    device->textureSlots = InitTable(block, textures, &device->textures, limits->textures);
    device->viewSlots = InitTable(block, views, &device->views, limits->views);
    device->swapchainSlots = InitTable(block, swapchains, &device->swapchains, limits->surfaces);
    device->running = (uint32_t*)(block + runningAt);
    device->queue = (mrhiDeviceNotification*)(block + queueAt);
    device->frameResources = (mrhiFrameResource*)(block + resourcesAt);
    device->framePasses = (mrhiFramePass*)(block + passesAt);
    device->frameUses = (mrhiFrameUse*)(block + usesAt);
    device->frameBarriers = (mrhiBarrier*)(block + barriersAt);
    device->frameBarrierScratch = (mrhiBarrier*)(block + scratchAt);
    device->frameCounts = (uint32_t*)(block + countsAt);
    device->frameBoxes = (mrhiBox*)(block + boxesAt);
    device->frameBoxLimit = (uint32_t)boxLimit;
    return device;
}

mrhiResult mrhiCreateDevice(mrhiInstance* instance, const mrhiDeviceDef* def,
                            mrhiDevice** deviceOut, mrhiRequestId* requestOut)
{
    if (instance == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (deviceOut == nullptr || requestOut == nullptr || def == nullptr)
    {
        if (deviceOut != nullptr)
        {
            *deviceOut = nullptr;
        }
        return mrhiMisuse(instance);
    }
    *deviceOut = nullptr;
    mrhiResult status = mrhi_success;
    const mrhiDriverAdapter* adapter = CheckDef(instance, def, &status);
    if (adapter == nullptr)
    {
        return status;
    }
    mrhiDevice* device = Allocate(def);
    if (device == nullptr)
    {
        return mrhi_errorCapacity;
    }
    device->instance = instance;
    device->adapter = adapter->handle;
    device->allocator = def->allocator;
    device->features = def->features;
    device->limits = def->limits;
    device->deviceLimits = def->deviceLimits;
    device->state = mrhi_deviceOpening;
    device->request = mrhiNextRequest(instance);
    for (uint32_t i = 0; i < MRHI_KNOWN_FORMATS; ++i)
    {
        mrhiFormat format = mrhiKnownFormats[i];
        device->formatCaps[i] = mrhiFormatFamilyGranted(format, &def->features)
                                    ? mrhiAdapterFormatCaps(instance, adapter, format)
                                    : (mrhiFormatCaps){0};
    }
    // An adapter was found, so the instance has a driver.
    MRHI_ASSERT(instance->driver.vtable != nullptr);
    status = instance->driver.vtable->createDevice(instance->driver.self, adapter->handle, def,
                                                   device->request, &device->driver);
    if (status != mrhi_success)
    {
        mrhiRelease(&device->allocator, device, device->bytes, alignof(mrhiDevice));
        return status;
    }
    ++instance->deviceCount;
    mrhiAddPending(instance, (mrhiPending){
                                 .request = device->request,
                                 .kind = mrhiPendingDevice,
                                 .device = device,
                             });
    *deviceOut = device;
    *requestOut = (mrhiRequestId){device->request, 1};
    return mrhi_success;
}

mrhiResult mrhiFinishOpening(mrhiDevice* device, mrhiResult outcome)
{
    device->state = outcome == mrhi_success ? mrhi_deviceReady : mrhi_deviceFailed;
    return outcome;
}

void mrhiDestroyDevice(mrhiDevice* device)
{
    if (device == nullptr)
    {
        return;
    }
    mrhiInstance* instance = device->instance;
    if (device->state == mrhi_deviceOpening)
    {
        mrhiAnswerNow(instance, device->request, mrhi_errorStale);
    }
    if (device->driver.vtable != nullptr)
    {
        mrhiEndConfigurations(device);
        device->driver.vtable->destroy(device->driver.self);
    }
    --instance->deviceCount;
    mrhiAllocator allocator = device->allocator;
    mrhiRelease(&allocator, device, device->bytes, alignof(mrhiDevice));
}

mrhiDeviceState mrhiGetDeviceState(mrhiDevice* device)
{
    return device == nullptr ? mrhi_deviceFailed : device->state;
}

mrhiResult mrhiDeviceMisuse(mrhiDevice* device)
{
    ++device->misuse;
    return mrhi_errorInvalid;
}

mrhiResult mrhiDeviceUsable(const mrhiDevice* device)
{
    return device->state == mrhi_deviceReady ? mrhi_success : mrhi_errorState;
}

mrhiResult mrhiGetDeviceFeatures(mrhiDevice* device, mrhiFeatures* featuresOut)
{
    if (device == nullptr || featuresOut == nullptr)
    {
        return device == nullptr ? mrhi_errorInvalid : mrhiDeviceMisuse(device);
    }
    *featuresOut = device->features;
    return mrhi_success;
}

mrhiResult mrhiGetDeviceLimits(mrhiDevice* device, mrhiLimits* limitsOut)
{
    if (device == nullptr || limitsOut == nullptr)
    {
        return device == nullptr ? mrhi_errorInvalid : mrhiDeviceMisuse(device);
    }
    *limitsOut = device->limits;
    return mrhi_success;
}

uint64_t mrhiGetDeviceMisuse(mrhiDevice* device)
{
    return device == nullptr ? 0 : device->misuse;
}

mrhiResult mrhiCheckObjectDef(mrhiDevice* device, mrhiDefHead head, uint32_t expected)
{
    mrhiResult chain = mrhiCheckChain(head.next, nullptr, 0, device->instance->limits.chainDepth);
    if (head.cookie != expected || chain == mrhi_errorInvalid ||
        !mrhiIsLabelValid(head.label, head.labelLength))
    {
        return mrhiDeviceMisuse(device);
    }
    return chain;
}
