// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The test driver: a copy of the def's adapters, and a queue of the
// requests the next poll answers.

#include "driver_test.h"

#include "allocator.h"

#include <stdalign.h>
#include <string.h>

typedef struct TestDriver
{
    mrhiAllocator allocator;
    size_t bytes;
    mrhiAdapterInfo* adapters;
    uint32_t adapterCount;
    uint64_t* pending;
    uint32_t pendingCount;
    uint32_t pendingLimit;
} TestDriver;

static mrhiResult RequestAdapters(void* self, uint64_t tag)
{
    TestDriver* driver = self;
    if (driver->pendingCount == driver->pendingLimit)
    {
        return mrhi_errorCapacity;
    }
    driver->pending[driver->pendingCount++] = tag;
    return mrhi_success;
}

static size_t Poll(void* self, mrhiDriverEvent* events, size_t capacity)
{
    TestDriver* driver = self;
    size_t moved = driver->pendingCount < capacity ? driver->pendingCount : capacity;
    for (size_t i = 0; i < moved; ++i)
    {
        events[i] = (mrhiDriverEvent){.tag = driver->pending[i], .outcome = mrhi_success};
    }
    memmove(driver->pending, driver->pending + moved,
            (driver->pendingCount - moved) * sizeof(uint64_t));
    driver->pendingCount -= (uint32_t)moved;
    return moved;
}

static size_t GetAdapters(const void* self, mrhiDriverAdapter* adapters, size_t capacity)
{
    const TestDriver* driver = self;
    for (size_t i = 0; i < driver->adapterCount && i < capacity; ++i)
    {
        adapters[i] = (mrhiDriverAdapter){.handle = i + 1, .info = driver->adapters[i]};
    }
    return driver->adapterCount;
}

static void Destroy(void* self)
{
    TestDriver* driver = self;
    mrhiAllocator allocator = driver->allocator;
    mrhiRelease(&allocator, driver, driver->bytes, alignof(TestDriver));
}

static const mrhiInstanceDriverVtable s_vtable = {
    .spiVersion = MRHI_SPI_VERSION,
    .size = sizeof(mrhiInstanceDriverVtable),
    .requestAdapters = RequestAdapters,
    .poll = Poll,
    .getAdapters = GetAdapters,
    .destroy = Destroy,
};

mrhiResult mrhiCreateTestDriver(const mrhiAllocator* allocator, const mrhiTestDriverDef* def,
                                uint32_t pendingLimit, mrhiInstanceDriver* driverOut)
{
    if (def->adapterCount > 0 && def->adapters == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiLayout layout = {.size = sizeof(TestDriver)};
    size_t adaptersAt = mrhiLayoutAdd(&layout, def->adapterCount, sizeof(mrhiAdapterInfo),
                                      alignof(mrhiAdapterInfo));
    size_t pendingAt = mrhiLayoutAdd(&layout, pendingLimit, sizeof(uint64_t), alignof(uint64_t));
    TestDriver* driver =
        layout.overflow ? nullptr : mrhiAllocate(allocator, layout.size, alignof(TestDriver));
    if (driver == nullptr)
    {
        return mrhi_errorCapacity;
    }
    unsigned char* block = (unsigned char*)driver;
    *driver = (TestDriver){
        .allocator = *allocator,
        .bytes = layout.size,
        .adapters = (mrhiAdapterInfo*)(block + adaptersAt),
        .adapterCount = def->adapterCount,
        .pending = (uint64_t*)(block + pendingAt),
        .pendingLimit = pendingLimit,
    };
    size_t adapterBytes = def->adapterCount * sizeof(mrhiAdapterInfo);
    if (adapterBytes > 0)
    {
        memcpy(driver->adapters, def->adapters, adapterBytes);
    }
    *driverOut = (mrhiInstanceDriver){.vtable = &s_vtable, .self = driver};
    return mrhi_success;
}
