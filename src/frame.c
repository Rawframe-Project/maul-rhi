// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Frames: one open at a time on a device, submitted to its driver with a
// token, and answered once in the device's notification queue (family
// record 0018) when the GPU finishes. Taking in finished frames polls the
// driver; nothing here blocks unless the program waits.

#include "device_core.h"

#define FRAME_DEF_COOKIE 0x6D726672u

// Driver events a poll moves at a time.
#define POLL_BATCH 8

mrhiFrameDef mrhiDefaultFrameDef(void)
{
    mrhiFrameDef def = {0};
    def.cookie = FRAME_DEF_COOKIE;
    return def;
}

// Takes a finished frame off the running list and queues its answer;
// submission made sure there is room.
static void Finish(mrhiDevice* device, uint64_t tag, mrhiResult outcome)
{
    for (uint32_t i = 0; i < device->runningCount; ++i)
    {
        if (device->running[i] == tag)
        {
            device->running[i] = device->running[--device->runningCount];
            uint32_t tail =
                (device->queueHead + device->queueCount) % device->deviceLimits.notifications;
            device->queue[tail] = (mrhiDeviceNotification){
                .kind = mrhi_deviceFrameDone,
                .requestId = {(uint32_t)tag, 1},
                .outcome = outcome,
            };
            ++device->queueCount;
            return;
        }
    }
}

// Takes in the frames the driver has finished.
static void TakeFinished(mrhiDevice* device)
{
    mrhiDriverEvent events[POLL_BATCH];
    size_t moved;
    while ((moved = device->driver.vtable->poll(device->driver.self, events, POLL_BATCH)) > 0)
    {
        for (size_t i = 0; i < moved; ++i)
        {
            Finish(device, events[i].tag, events[i].outcome);
        }
    }
}

mrhiResult mrhiBeginFrame(mrhiDevice* device, const mrhiFrameDef* def)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiDefHead head = {def->cookie, def->next, nullptr, 0};
    mrhiResult status = mrhiCheckObjectDef(device, head, FRAME_DEF_COOKIE);
    if (status != mrhi_success)
    {
        return status;
    }
    status = mrhiDeviceUsable(device);
    if (status != mrhi_success)
    {
        return status;
    }
    if (device->frameOpen)
    {
        return mrhi_errorState;
    }
    TakeFinished(device);
    if (device->runningCount == device->limits.framesInFlight)
    {
        return mrhi_errorCapacity;
    }
    device->frameOpen = true;
    device->frameSerial = device->frameSerial == UINT32_MAX ? 1 : device->frameSerial + 1;
    device->frameCompiled = false;
    device->frameResourceCount = 0;
    device->framePassCount = 0;
    device->frameUseCount = 0;
    return mrhi_success;
}

mrhiResult mrhiDropFrame(mrhiDevice* device)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (!device->frameOpen)
    {
        return mrhi_errorState;
    }
    device->frameOpen = false;
    return mrhi_success;
}

mrhiResult mrhiSubmitFrame(mrhiDevice* device, mrhiRequestId* tokenOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (tokenOut == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    if (!device->frameOpen)
    {
        return mrhi_errorState;
    }
    mrhiResult compiled = device->frameCompiled ? mrhi_success : mrhiCompile(device);
    if (compiled != mrhi_success)
    {
        return compiled;
    }
    if (device->queueCount + device->runningCount >= device->deviceLimits.notifications)
    {
        return mrhi_errorCapacity;
    }
    device->frameOpen = false;
    uint32_t token = device->lastToken + 1;
    mrhiResult status = device->driver.vtable->submitFrame(device->driver.self, token);
    if (status != mrhi_success)
    {
        return status;
    }
    device->lastToken = token;
    device->running[device->runningCount++] = token;
    *tokenOut = (mrhiRequestId){token, 1};
    return mrhi_success;
}

mrhiResult mrhiWaitFrame(mrhiDevice* device, mrhiRequestId token, uint64_t timeoutNs)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (token.index1 == 0 || token.index1 > device->lastToken || token.generation != 1)
    {
        return mrhiDeviceMisuse(device);
    }
    bool running = false;
    for (uint32_t i = 0; i < device->runningCount; ++i)
    {
        running = running || device->running[i] == token.index1;
    }
    if (!running)
    {
        return mrhi_success;
    }
    // A finished frame is answered at the next poll.
    return device->driver.vtable->waitFrame(device->driver.self, token.index1, timeoutNs)
               ? mrhi_success
               : mrhi_timeout;
}

mrhiResult mrhiNextDeviceNotification(mrhiDevice* device, mrhiDeviceNotification* notificationOut)
{
    if (device == nullptr || notificationOut == nullptr)
    {
        return device == nullptr ? mrhi_errorInvalid : mrhiDeviceMisuse(device);
    }
    if (device->queueCount == 0)
    {
        TakeFinished(device);
    }
    if (device->queueCount == 0)
    {
        return mrhi_empty;
    }
    *notificationOut = device->queue[device->queueHead];
    device->queueHead = (device->queueHead + 1) % device->deviceLimits.notifications;
    --device->queueCount;
    return mrhi_success;
}
