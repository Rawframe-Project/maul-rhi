// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The test driver: a copy of the def's adapters, devices that hold
// nothing, and a queue of the requests the next poll answers.

#include "driver_test.h"

#include "allocator.h"
#include "capabilities_core.h"
#include "invariant.h"

#include <stdalign.h>
#include <string.h>

typedef struct TestDriver
{
    mrhiAllocator allocator;
    size_t bytes;
    mrhiTestAdapter* adapters;
    uint32_t adapterCount;
    mrhiDriverEvent* pending;
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
    driver->pending[driver->pendingCount++] =
        (mrhiDriverEvent){.tag = tag, .outcome = mrhi_success};
    return mrhi_success;
}

static size_t Poll(void* self, mrhiDriverEvent* events, size_t capacity)
{
    TestDriver* driver = self;
    size_t moved = driver->pendingCount < capacity ? driver->pendingCount : capacity;
    memcpy(events, driver->pending, moved * sizeof(mrhiDriverEvent));
    memmove(driver->pending, driver->pending + moved,
            (driver->pendingCount - moved) * sizeof(mrhiDriverEvent));
    driver->pendingCount -= (uint32_t)moved;
    return moved;
}

static size_t GetAdapters(const void* self, mrhiDriverAdapter* adapters, size_t capacity)
{
    const TestDriver* driver = self;
    for (size_t i = 0; i < driver->adapterCount && i < capacity; ++i)
    {
        const mrhiTestAdapter* adapter = &driver->adapters[i];
        adapters[i] = (mrhiDriverAdapter){
            .handle = i + 1,
            .info = adapter->info,
            .features = adapter->features,
            .limits = adapter->limits,
        };
    }
    return driver->adapterCount;
}

typedef struct TestDevice
{
    mrhiAllocator allocator;
    uint64_t nextHandle;
    // Objects made so far, and the count after which making fails; 0
    // for no failure.
    uint32_t made;
    uint32_t madeBeforeFailure;
    uint32_t samplers;
    uint32_t buffers;
    uint64_t bufferBytes;
    uint32_t textures;
} TestDevice;

// A new object's handle, or mrhi_errorPlatform once the adapter's
// object budget is spent.
static mrhiResult MakeObject(TestDevice* device, uint64_t* handleOut)
{
    if (device->madeBeforeFailure != 0 && device->made == device->madeBeforeFailure)
    {
        return mrhi_errorPlatform;
    }
    ++device->made;
    *handleOut = ++device->nextHandle;
    return mrhi_success;
}

static mrhiResult CreateSampler(void* self, const mrhiSamplerDef* def, uint64_t* handleOut)
{
    (void)def;
    TestDevice* device = self;
    mrhiResult status = MakeObject(device, handleOut);
    device->samplers += status == mrhi_success ? 1 : 0;
    return status;
}

static mrhiResult CreateBuffer(void* self, const mrhiBufferDef* def, uint64_t* handleOut)
{
    TestDevice* device = self;
    mrhiResult status = MakeObject(device, handleOut);
    if (status == mrhi_success)
    {
        ++device->buffers;
        device->bufferBytes += def->size;
    }
    return status;
}

static void DestroyBuffer(void* self, uint64_t handle)
{
    TestDevice* device = self;
    MRHI_ASSERT(handle != 0 && handle <= device->nextHandle && device->buffers > 0);
    --device->buffers;
}

static mrhiResult CreateTexture(void* self, const mrhiTextureDef* def, uint64_t* handleOut)
{
    (void)def;
    TestDevice* device = self;
    mrhiResult status = MakeObject(device, handleOut);
    device->textures += status == mrhi_success ? 1 : 0;
    return status;
}

static void DestroyTexture(void* self, uint64_t handle)
{
    TestDevice* device = self;
    MRHI_ASSERT(handle != 0 && handle <= device->nextHandle && device->textures > 0);
    --device->textures;
}

static void DestroySampler(void* self, uint64_t handle)
{
    TestDevice* device = self;
    MRHI_ASSERT(handle != 0 && handle <= device->nextHandle && device->samplers > 0);
    --device->samplers;
}

static void DestroyDevice(void* self)
{
    TestDevice* device = self;
    mrhiAllocator allocator = device->allocator;
    mrhiRelease(&allocator, device, sizeof(TestDevice), alignof(TestDevice));
}

static const mrhiDeviceDriverVtable s_deviceVtable = {
    .spiVersion = MRHI_SPI_VERSION,
    .size = sizeof(mrhiDeviceDriverVtable),
    .destroy = DestroyDevice,
    .createSampler = CreateSampler,
    .destroySampler = DestroySampler,
    .createBuffer = CreateBuffer,
    .destroyBuffer = DestroyBuffer,
    .createTexture = CreateTexture,
    .destroyTexture = DestroyTexture,
};

static mrhiResult CreateDevice(void* self, uint64_t adapter, const mrhiFeatures* features,
                               const mrhiLimits* limits, uint64_t tag, mrhiDeviceDriver* deviceOut)
{
    (void)features;
    (void)limits;
    TestDriver* driver = self;
    if (driver->pendingCount == driver->pendingLimit)
    {
        return mrhi_errorCapacity;
    }
    TestDevice* device = mrhiAllocate(&driver->allocator, sizeof(TestDevice), alignof(TestDevice));
    if (device == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *device = (TestDevice){
        .allocator = driver->allocator,
        .madeBeforeFailure = driver->adapters[adapter - 1].objectsBeforeFailure,
    };
    driver->pending[driver->pendingCount++] = (mrhiDriverEvent){
        .tag = tag,
        .outcome = driver->adapters[adapter - 1].openOutcome,
    };
    *deviceOut = (mrhiDeviceDriver){.vtable = &s_deviceVtable, .self = device};
    return mrhi_success;
}

// The floor, and what the adapter's features add: filtering of 32-bit
// floats, rendering to rg11b10, and sampling of a granted compressed
// family.
static void GetFormatCaps(const void* self, uint64_t adapter, mrhiFormat format,
                          mrhiFormatCaps* capsOut)
{
    const TestDriver* driver = self;
    const mrhiTestAdapter* described = &driver->adapters[adapter - 1];
    const mrhiFeatures* features = &described->features;
    if (format == described->limitedFormat)
    {
        *capsOut = described->limitedCaps;
        return;
    }
    mrhiFormatCaps caps = mrhiFloorFormatCaps(format);
    bool float32 = format == mrhi_formatR32Float || format == mrhi_formatRg32Float ||
                   format == mrhi_formatRgba32Float;
    caps.filtering = caps.filtering || (float32 && features->float32Filterable);
    caps.rendering =
        caps.rendering || (format == mrhi_formatRg11b10Ufloat && features->rg11b10Renderable);
    if (!caps.sampling && mrhiIsFormatKnown(format) && mrhiFormatFamilyGranted(format, features))
    {
        caps.sampling = true;
        caps.filtering = true;
        caps.sampleCounts = 1;
    }
    *capsOut = caps;
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
    .getFormatCaps = GetFormatCaps,
    .createDevice = CreateDevice,
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
    size_t adaptersAt = mrhiLayoutAdd(&layout, def->adapterCount, sizeof(mrhiTestAdapter),
                                      alignof(mrhiTestAdapter));
    size_t pendingAt =
        mrhiLayoutAdd(&layout, pendingLimit, sizeof(mrhiDriverEvent), alignof(mrhiDriverEvent));
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
        .adapters = (mrhiTestAdapter*)(block + adaptersAt),
        .adapterCount = def->adapterCount,
        .pending = (mrhiDriverEvent*)(block + pendingAt),
        .pendingLimit = pendingLimit,
    };
    size_t adapterBytes = def->adapterCount * sizeof(mrhiTestAdapter);
    if (adapterBytes > 0)
    {
        memcpy(driver->adapters, def->adapters, adapterBytes);
    }
    *driverOut = (mrhiInstanceDriver){.vtable = &s_vtable, .self = driver};
    return mrhi_success;
}
