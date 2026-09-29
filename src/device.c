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

#include <stdalign.h>

#define DEVICE_DEF_COOKIE 0x6D726476u

mrhiDeviceDef mrhiDefaultDeviceDef(void)
{
    mrhiDeviceDef def = {0};
    def.cookie = DEVICE_DEF_COOKIE;
    def.limits = mrhiDefaultLimits();
    def.deviceLimits.notifications = 256;
    def.deviceLimits.samplers = 256;
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
        def->deviceLimits.samplers == 0 || !mrhiIsAllocatorValid(&def->allocator) ||
        !mrhiLimitsWithin(&floor, &def->limits) || chain == mrhi_errorInvalid)
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

// The device's block: the struct, then its tables, sized by its limits.
static mrhiDevice* Allocate(const mrhiDeviceDef* def)
{
    uint32_t samplers = def->deviceLimits.samplers;
    mrhiLayout layout = {.size = sizeof(mrhiDevice)};
    size_t generationsAt = mrhiLayoutAdd(&layout, samplers, sizeof(uint32_t), alignof(uint32_t));
    size_t nextAt = mrhiLayoutAdd(&layout, samplers, sizeof(uint32_t), alignof(uint32_t));
    size_t handlesAt = mrhiLayoutAdd(&layout, samplers, sizeof(uint64_t), alignof(uint64_t));
    unsigned char* block =
        layout.overflow ? nullptr : mrhiAllocate(&def->allocator, layout.size, alignof(mrhiDevice));
    if (block == nullptr)
    {
        return nullptr;
    }
    mrhiDevice* device = (mrhiDevice*)block;
    *device = (mrhiDevice){.bytes = layout.size};
    mrhiPoolInit(&device->samplers, samplers, (uint32_t*)(block + generationsAt),
                 (uint32_t*)(block + nextAt));
    device->samplerHandles = (uint64_t*)(block + handlesAt);
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
    device->allocator = def->allocator;
    device->features = def->features;
    device->limits = def->limits;
    device->deviceLimits = def->deviceLimits;
    device->state = mrhi_deviceOpening;
    device->request = mrhiNextRequest(instance);
    // An adapter was found, so the instance has a driver.
    MRHI_ASSERT(instance->driver.vtable != nullptr);
    status = instance->driver.vtable->createDevice(instance->driver.self, adapter->handle,
                                                   &device->features, &device->limits,
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
