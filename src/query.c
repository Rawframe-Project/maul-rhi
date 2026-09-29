// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Query sets (mrhi-0012): device objects of occlusion or timestamp
// queries, each holding a run of the device's query marks, which record
// the frame that last wrote each query.

#include "device_core.h"

#define QUERY_SET_DEF_COOKIE 0x6D727173u
// WebGPU's bound on a set's queries.
#define MOST_QUERIES 4096

mrhiQuerySetDef mrhiDefaultQuerySetDef(void)
{
    mrhiQuerySetDef def = {0};
    def.cookie = QUERY_SET_DEF_COOKIE;
    def.type = mrhi_queryOcclusion;
    def.count = 1;
    return def;
}

// The first run of count marks no live set holds, first fit: its start,
// or false when the device's marks lack one.
static bool FindRun(const mrhiDevice* device, uint32_t count, uint32_t* firstOut)
{
    uint32_t sets = device->deviceLimits.querySets;
    uint32_t start = 0;
    bool moved = true;
    while (moved)
    {
        moved = false;
        for (uint32_t i = 0; i < sets; ++i)
        {
            // A free slot is zeroed: its empty run overlaps nothing.
            const mrhiQuerySetSlot* slot = &device->querySetSlots[i];
            if (slot->first < start + count && start < slot->first + slot->count)
            {
                start = slot->first + slot->count;
                moved = true;
            }
        }
        if (start > device->deviceLimits.queries - count)
        {
            return false;
        }
    }
    *firstOut = start;
    return true;
}

mrhiResult mrhiCreateQuerySet(mrhiDevice* device, const mrhiQuerySetDef* def,
                              mrhiQuerySetId* setOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || setOut == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiResult status = mrhiCheckObjectDef(device, MRHI_DEF_HEAD(def), QUERY_SET_DEF_COOKIE);
    if (status != mrhi_success)
    {
        return status;
    }
    if (def->type > mrhi_queryTimestamp || def->count == 0 || def->count > MOST_QUERIES)
    {
        return mrhiDeviceMisuse(device);
    }
    if (def->type == mrhi_queryTimestamp && !device->features.timestampQuery)
    {
        return mrhi_errorUnsupported;
    }
    status = mrhiDeviceUsable(device);
    if (status != mrhi_success)
    {
        return status;
    }
    uint32_t first = 0;
    uint32_t index1 = 0;
    uint32_t generation = 0;
    if (def->count > device->deviceLimits.queries || !FindRun(device, def->count, &first) ||
        !mrhiPoolAcquire(&device->querySets, &index1, &generation))
    {
        return mrhi_errorCapacity;
    }
    uint64_t handle = 0;
    status = device->driver.vtable->createQuerySet(device->driver.self, def, &handle);
    if (status != mrhi_success)
    {
        mrhiPoolRelease(&device->querySets, index1);
        return status;
    }
    device->querySetSlots[index1 - 1] = (mrhiQuerySetSlot){
        .handle = handle,
        .first = first,
        .count = def->count,
        .type = def->type,
    };
    // No frame has serial 0, so the new set's queries are unwritten.
    for (uint32_t i = 0; i < def->count; ++i)
    {
        atomic_init(&device->queryMarks[first + i], 0);
    }
    *setOut = (mrhiQuerySetId){index1, generation};
    return mrhi_success;
}

mrhiResult mrhiDestroyQuerySet(mrhiDevice* device, mrhiQuerySetId set)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (!mrhiPoolIsLive(&device->querySets, set.index1, set.generation))
    {
        return mrhi_errorStale;
    }
    mrhiQuerySetSlot* slot = &device->querySetSlots[set.index1 - 1];
    device->driver.vtable->destroyQuerySet(device->driver.self, slot->handle);
    *slot = (mrhiQuerySetSlot){0};
    mrhiPoolRelease(&device->querySets, set.index1);
    return mrhi_success;
}
