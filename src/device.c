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

#include <stdalign.h>

#define DEVICE_DEF_COOKIE 0x6D726476u

mrhiDeviceDef mrhiDefaultDeviceDef(void)
{
    mrhiDeviceDef def = {0};
    def.cookie = DEVICE_DEF_COOKIE;
    def.limits = mrhiDefaultLimits();
    def.deviceLimits.notifications = 256;
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

// Starts the device's driver side; without an instance driver, the
// device opens at once.
static mrhiResult OpenDriver(mrhiInstance* instance, mrhiDevice* device, uint64_t adapter)
{
    if (instance->driver.vtable == nullptr)
    {
        return mrhi_success;
    }
    return instance->driver.vtable->createDevice(instance->driver.self, adapter, &device->features,
                                                 &device->limits, device->request, &device->driver);
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
    mrhiDevice* device = mrhiAllocate(&def->allocator, sizeof(mrhiDevice), alignof(mrhiDevice));
    if (device == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *device = (mrhiDevice){
        .instance = instance,
        .allocator = def->allocator,
        .features = def->features,
        .limits = def->limits,
        .deviceLimits = def->deviceLimits,
        .state = mrhi_deviceOpening,
        .request = mrhiNextRequest(instance),
    };
    status = OpenDriver(instance, device, adapter->handle);
    if (status != mrhi_success)
    {
        mrhiRelease(&device->allocator, device, sizeof(mrhiDevice), alignof(mrhiDevice));
        return status;
    }
    ++instance->deviceCount;
    mrhiAddPending(instance, (mrhiPending){
                                 .request = device->request,
                                 .kind = mrhiPendingDevice,
                                 .device = device,
                             });
    if (instance->driver.vtable == nullptr)
    {
        mrhiAnswerNow(instance, device->request, mrhi_success);
    }
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
    mrhiRelease(&allocator, device, sizeof(mrhiDevice), alignof(mrhiDevice));
}

mrhiDeviceState mrhiGetDeviceState(mrhiDevice* device)
{
    return device == nullptr ? mrhi_deviceFailed : device->state;
}

// Counts one misuse and returns mrhi_errorInvalid.
static mrhiResult Misuse(mrhiDevice* device)
{
    ++device->misuse;
    return mrhi_errorInvalid;
}

mrhiResult mrhiGetDeviceFeatures(mrhiDevice* device, mrhiFeatures* featuresOut)
{
    if (device == nullptr || featuresOut == nullptr)
    {
        return device == nullptr ? mrhi_errorInvalid : Misuse(device);
    }
    *featuresOut = device->features;
    return mrhi_success;
}

mrhiResult mrhiGetDeviceLimits(mrhiDevice* device, mrhiLimits* limitsOut)
{
    if (device == nullptr || limitsOut == nullptr)
    {
        return device == nullptr ? mrhi_errorInvalid : Misuse(device);
    }
    *limitsOut = device->limits;
    return mrhi_success;
}

uint64_t mrhiGetDeviceMisuse(mrhiDevice* device)
{
    return device == nullptr ? 0 : device->misuse;
}
