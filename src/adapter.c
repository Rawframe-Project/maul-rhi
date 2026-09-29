// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Adapter requests (record R17): each is answered by one record, the
// adapter table keeps the ids of adapters found again and retires the
// others, and the listing orders them by the request's preference.

#include "capabilities_core.h"
#include "chain.h"
#include "instance_core.h"

#define ADAPTER_REQUEST_DEF_COOKIE 0x6D726172u
// Driver events a poll moves at a time.
#define POLL_BATCH 8

mrhiAdapterRequestDef mrhiDefaultAdapterRequestDef(void)
{
    mrhiAdapterRequestDef def = {0};
    def.cookie = ADAPTER_REQUEST_DEF_COOKIE;
    def.preference = mrhi_powerDefault;
    def.allowSoftware = true;
    return def;
}

void mrhiPushInstanceNotification(mrhiInstance* instance, mrhiInstanceNotification notification)
{
    uint32_t tail = (instance->queueHead + instance->queueCount) % instance->limits.notifications;
    instance->queue[tail] = notification;
    ++instance->queueCount;
}

// Where an adapter kind ranks under a preference; lower is better.
static int Rank(mrhiAdapterKind kind, mrhiPowerPreference preference)
{
    static const int high[] = {3, 0, 1, 2, 4};
    static const int low[] = {3, 1, 0, 2, 4};
    if (kind > mrhi_adapterSoftware || preference == mrhi_powerDefault)
    {
        return 0;
    }
    return preference == mrhi_powerHigh ? high[kind] : low[kind];
}

static void SortListing(mrhiInstance* instance, mrhiPowerPreference preference)
{
    uint32_t* listing = instance->listing;
    for (uint32_t i = 1; i < instance->listed; ++i)
    {
        uint32_t slot = listing[i];
        int rank = Rank(instance->slots[slot].adapter.info.kind, preference);
        uint32_t j = i;
        while (j > 0 && Rank(instance->slots[listing[j - 1]].adapter.info.kind, preference) > rank)
        {
            listing[j] = listing[j - 1];
            --j;
        }
        listing[j] = slot;
    }
}

static uint32_t SlotFor(mrhiInstance* instance, uint64_t handle)
{
    uint32_t free = instance->limits.adapters;
    for (uint32_t i = 0; i < instance->limits.adapters; ++i)
    {
        if (instance->slots[i].inUse && instance->slots[i].adapter.handle == handle)
        {
            return i;
        }
        if (!instance->slots[i].inUse && free == instance->limits.adapters)
        {
            free = i;
        }
    }
    return free;
}

// Rebuilds the table and the listing from what the driver found. An
// adapter below the floor limits is left out, and its features are
// masked to what its API can grant. The driver's order is kept for
// equal ranks; more adapters than the limit is a capacity outcome, with
// the first ones kept.
static mrhiResult Refresh(mrhiInstance* instance, const mrhiAdapterQuery* query)
{
    size_t total = 0;
    if (instance->driver.vtable != nullptr)
    {
        total = instance->driver.vtable->getAdapters(instance->driver.self, instance->found,
                                                     instance->limits.adapters);
    }
    size_t count = total < instance->limits.adapters ? total : instance->limits.adapters;
    mrhiLimits floor = mrhiDefaultLimits();
    for (uint32_t i = 0; i < instance->limits.adapters; ++i)
    {
        instance->slots[i].seen = false;
    }
    instance->listed = 0;
    for (size_t i = 0; i < count; ++i)
    {
        mrhiDriverAdapter* adapter = &instance->found[i];
        if ((!query->allowSoftware && adapter->info.kind == mrhi_adapterSoftware) ||
            !mrhiLimitsWithin(&floor, &adapter->limits))
        {
            continue;
        }
        mrhiMaskFeatures(&adapter->features, adapter->info.driver);
        uint32_t slot = SlotFor(instance, adapter->handle);
        instance->slots[slot].inUse = true;
        instance->slots[slot].seen = true;
        instance->slots[slot].adapter = *adapter;
        instance->listing[instance->listed++] = slot;
    }
    for (uint32_t i = 0; i < instance->limits.adapters; ++i)
    {
        if (instance->slots[i].inUse && !instance->slots[i].seen)
        {
            instance->slots[i].inUse = false;
            ++instance->slots[i].generation;
        }
    }
    SortListing(instance, query->preference);
    return total > count ? mrhi_errorCapacity : mrhi_success;
}

static void Answer(mrhiInstance* instance, uint32_t queryIndex, mrhiResult outcome)
{
    mrhiAdapterQuery query = instance->queries[queryIndex];
    instance->queries[queryIndex] = instance->queries[--instance->queryCount];
    if (outcome == mrhi_success)
    {
        outcome = Refresh(instance, &query);
    }
    mrhiPushInstanceNotification(instance, (mrhiInstanceNotification){
                                               .kind = mrhi_instanceAdaptersFound,
                                               .requestId = {query.request, 1},
                                               .outcome = outcome,
                                           });
}

static void PollDriver(mrhiInstance* instance)
{
    if (instance->driver.vtable == nullptr)
    {
        return;
    }
    mrhiDriverEvent events[POLL_BATCH];
    size_t moved;
    while ((moved = instance->driver.vtable->poll(instance->driver.self, events, POLL_BATCH)) > 0)
    {
        for (size_t i = 0; i < moved; ++i)
        {
            for (uint32_t q = 0; q < instance->queryCount; ++q)
            {
                if (instance->queries[q].request == events[i].tag)
                {
                    Answer(instance, q, events[i].outcome);
                    break;
                }
            }
        }
    }
}

mrhiResult mrhiRequestAdapters(mrhiInstance* instance, const mrhiAdapterRequestDef* def,
                               mrhiRequestId* requestOut)
{
    if (instance == nullptr || def == nullptr || requestOut == nullptr ||
        def->cookie != ADAPTER_REQUEST_DEF_COOKIE || def->preference > mrhi_powerHigh)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult chain = mrhiCheckChain(def->next, nullptr, 0, instance->limits.chainDepth);
    if (chain != mrhi_success)
    {
        return chain;
    }
    if (instance->queueCount + instance->queryCount == instance->limits.notifications)
    {
        return mrhi_errorCapacity;
    }
    uint32_t request = ++instance->nextRequest;
    if (instance->driver.vtable != nullptr)
    {
        mrhiResult status =
            instance->driver.vtable->requestAdapters(instance->driver.self, request);
        if (status != mrhi_success)
        {
            return status;
        }
    }
    instance->queries[instance->queryCount++] = (mrhiAdapterQuery){
        .request = request, .preference = def->preference, .allowSoftware = def->allowSoftware};
    if (instance->driver.vtable == nullptr)
    {
        Answer(instance, instance->queryCount - 1, mrhi_success);
    }
    *requestOut = (mrhiRequestId){request, 1};
    return mrhi_success;
}

mrhiResult mrhiNextInstanceNotification(mrhiInstance* instance,
                                        mrhiInstanceNotification* notificationOut)
{
    if (instance == nullptr || notificationOut == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (instance->queueCount == 0)
    {
        PollDriver(instance);
    }
    if (instance->queueCount == 0)
    {
        return mrhi_empty;
    }
    *notificationOut = instance->queue[instance->queueHead];
    instance->queueHead = (instance->queueHead + 1) % instance->limits.notifications;
    --instance->queueCount;
    return mrhi_success;
}

mrhiResult mrhiGetAdapters(const mrhiInstance* instance, mrhiAdapterId* adapters, size_t capacity,
                           size_t* countOut)
{
    if (instance == nullptr || countOut == nullptr || (adapters == nullptr && capacity > 0))
    {
        return mrhi_errorInvalid;
    }
    for (uint32_t i = 0; i < instance->listed && i < capacity; ++i)
    {
        uint32_t slot = instance->listing[i];
        adapters[i] = (mrhiAdapterId){slot + 1, instance->slots[slot].generation};
    }
    *countOut = instance->listed;
    return mrhi_success;
}

// The slot an adapter id names, or NULL for a stale or null id.
static const mrhiAdapterSlot* FindSlot(const mrhiInstance* instance, mrhiAdapterId adapter)
{
    uint32_t slot = adapter.index1 - 1;
    if (adapter.index1 == 0 || slot >= instance->limits.adapters || !instance->slots[slot].inUse ||
        instance->slots[slot].generation != adapter.generation)
    {
        return nullptr;
    }
    return &instance->slots[slot];
}

mrhiResult mrhiGetAdapterInfo(const mrhiInstance* instance, mrhiAdapterId adapter,
                              mrhiAdapterInfo* infoOut)
{
    if (instance == nullptr || infoOut == nullptr)
    {
        return mrhi_errorInvalid;
    }
    const mrhiAdapterSlot* slot = FindSlot(instance, adapter);
    if (slot == nullptr)
    {
        return mrhi_errorStale;
    }
    *infoOut = slot->adapter.info;
    return mrhi_success;
}

mrhiResult mrhiGetAdapterFeatures(const mrhiInstance* instance, mrhiAdapterId adapter,
                                  mrhiFeatures* featuresOut)
{
    if (instance == nullptr || featuresOut == nullptr)
    {
        return mrhi_errorInvalid;
    }
    const mrhiAdapterSlot* slot = FindSlot(instance, adapter);
    if (slot == nullptr)
    {
        return mrhi_errorStale;
    }
    *featuresOut = slot->adapter.features;
    return mrhi_success;
}

mrhiResult mrhiGetAdapterLimits(const mrhiInstance* instance, mrhiAdapterId adapter,
                                mrhiLimits* limitsOut)
{
    if (instance == nullptr || limitsOut == nullptr)
    {
        return mrhi_errorInvalid;
    }
    const mrhiAdapterSlot* slot = FindSlot(instance, adapter);
    if (slot == nullptr)
    {
        return mrhi_errorStale;
    }
    *limitsOut = slot->adapter.limits;
    return mrhi_success;
}
