// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The instance as the core sees it: its limits, its driver, the
// notification queue and the adapter table. Everything is laid out in
// one block when the instance is made, so nothing allocates later.

#ifndef MAUL_RHI_SRC_INSTANCE_CORE_H
#define MAUL_RHI_SRC_INSTANCE_CORE_H

#include "driver.h"

#include "maul-rhi/instance.h"

// An adapter id's slot: its generation, and, in use, what the driver
// reported. seen marks the slots a refresh found again.
typedef struct mrhiAdapterSlot
{
    uint32_t generation;
    bool inUse;
    bool seen;
    mrhiDriverAdapter adapter;
} mrhiAdapterSlot;

// An adapter request waiting for its driver's answer. Its request id's
// index is also the tag the driver answers with.
typedef struct mrhiAdapterQuery
{
    uint32_t request;
    mrhiPowerPreference preference;
    bool allowSoftware;
} mrhiAdapterQuery;

struct mrhiInstance
{
    mrhiAllocator allocator;
    mrhiInstanceLimits limits;
    size_t bytes;
    // No driver when its vtable is NULL.
    mrhiInstanceDriver driver;
    uint32_t nextRequest;
    // A ring of limits.notifications records.
    mrhiInstanceNotification* queue;
    uint32_t queueHead;
    uint32_t queueCount;
    // Requests without an answer; with the queue, at most
    // limits.notifications, so every answer has room.
    mrhiAdapterQuery* queries;
    uint32_t queryCount;
    // limits.adapters slots, the listing of their indices in the last
    // answer's order, and room for what the driver reports.
    mrhiAdapterSlot* slots;
    uint32_t* listing;
    uint32_t listed;
    mrhiDriverAdapter* found;
};

// Appends a record; the caller has made sure there is room.
void mrhiPushInstanceNotification(mrhiInstance* instance, mrhiInstanceNotification notification);

#endif // MAUL_RHI_SRC_INSTANCE_CORE_H
