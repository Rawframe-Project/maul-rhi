// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The WebGPU driver's instance (mrhi-0003): the browser's navigator.gpu,
// reached through EM_JS functions. Each instance has a state on the
// JavaScript side, where requests wait as promises and settle into a
// queue of request slots; a poll drains it. A search asks for the
// browser's adapter and lists it under one handle, so an adapter found
// again keeps its id; devices ask for a fresh adapter, since a WebGPU
// adapter makes one device. Features and limits follow the contract's
// WebGPU rows; an adapter without core features falls below the floor.

#include "driver_webgpu.h"

#include "allocator.h"
#include "format_caps.h"
#include "invariant.h"

#include <emscripten/em_js.h>
#include <stdalign.h>
#include <stddef.h>
#include <string.h>

// The handle of the browser's adapter.
#define ADAPTER_HANDLE 1

// The frames a device lets run at once, each a settled promise.
#define WEBGPU_FRAMES_IN_FLIGHT 3

typedef struct WebGpuDriver
{
    mrhiAllocator allocator;
    size_t bytes;
    // The instance's state on the JavaScript side.
    int state;
    // Each request slot's tag, 0 for a free slot.
    uint64_t* tags;
    uint32_t slotCount;
    // What the last finished search found.
    bool found;
    mrhiDriverAdapter adapter;
} WebGpuDriver;

// clang-format off
EM_JS(int, JsCreateState, (void), {
    const gpu = Module.mrhiGpu || (Module.mrhiGpu = {states: [null]});
    gpu.states.push({settled: [], adapter: null});
    return gpu.states.length - 1;
});

EM_JS(void, JsDestroyState, (int state), {
    Module.mrhiGpu.states[state] = null;
});

// Asks for the browser's adapter; the slot settles either way.
EM_JS(void, JsRequestAdapter, (int state, uint32_t slot), {
    const self = Module.mrhiGpu.states[state];
    const settle = adapter => {
        if (Module.mrhiGpu.states[state] === self) {
            self.adapter = adapter || null;
            self.settled.push([slot, 0]);
        }
    };
    if (typeof navigator === 'undefined' || !navigator.gpu) {
        settle(null);
        return;
    }
    navigator.gpu.requestAdapter().then(settle, () => settle(null));
});

// The next settled slot and its outcome, or -1.
EM_JS(int, JsTakeSettled, (int state, int32_t* outcomeOut), {
    const next = Module.mrhiGpu.states[state].settled.shift();
    if (!next) {
        return -1;
    }
    HEAP32[outcomeOut >> 2] = next[1];
    return next[0];
});

EM_JS(bool, JsHasAdapter, (int state), {
    return Module.mrhiGpu.states[state].adapter !== null;
});

EM_JS(bool, JsAdapterHas, (int state, const char* feature), {
    return Module.mrhiGpu.states[state].adapter.features.has(UTF8ToString(feature));
});

// A limit, 0 for one the browser does not report.
EM_JS(double, JsAdapterLimit, (int state, const char* limit), {
    const value = Module.mrhiGpu.states[state].adapter.limits[UTF8ToString(limit)];
    return typeof value === 'number' ? value : 0;
});

EM_JS(bool, JsAdapterFallback, (int state), {
    const info = Module.mrhiGpu.states[state].adapter.info;
    return !!(info && info.isFallbackAdapter);
});

// Writes the adapter's name, its description or its vendor and
// architecture, and returns its bytes.
EM_JS(int, JsAdapterName, (int state, char* out, int capacity), {
    const info = Module.mrhiGpu.states[state].adapter.info || {};
    const parts = [info.vendor, info.architecture].filter(part => part);
    const name = info.description || parts.join(' ') || 'WebGPU';
    return stringToUTF8(name, out, capacity);
});
// clang-format on

EM_JS_DEPS(mrhi_webgpu_driver, "$UTF8ToString,$stringToUTF8");

// A feature as the browser names it, and its flag.
typedef struct Feature
{
    const char* name;
    size_t offset;
} Feature;

// The contract's WebGPU feature rows that are not absent.
static const Feature s_features[] = {
    {"timestamp-query", offsetof(mrhiFeatures, timestampQuery)},
    {"texture-compression-bc", offsetof(mrhiFeatures, textureCompressionBc)},
    {"texture-compression-etc2", offsetof(mrhiFeatures, textureCompressionEtc2)},
    {"texture-compression-astc", offsetof(mrhiFeatures, textureCompressionAstc)},
    {"float32-filterable", offsetof(mrhiFeatures, float32Filterable)},
    {"rg11b10ufloat-renderable", offsetof(mrhiFeatures, rg11b10Renderable)},
    {"dual-source-blending", offsetof(mrhiFeatures, dualSourceBlending)},
    {"depth-clip-control", offsetof(mrhiFeatures, unclippedDepth)},
    {"shader-f16", offsetof(mrhiFeatures, shaderF16)},
    {"subgroups", offsetof(mrhiFeatures, subgroups)},
    {"indirect-first-instance", offsetof(mrhiFeatures, indirectFirstInstance)},
};

// A limit as the browser names it, its field, and whether it is 64-bit.
typedef struct Limit
{
    const char* name;
    size_t offset;
    bool wide;
} Limit;

// The contract's WebGPU limit rows; the root block is immediates.
static const Limit s_limits[] = {
    {"maxTextureDimension2D", offsetof(mrhiLimits, textureDimension2d), false},
    {"maxTextureDimension3D", offsetof(mrhiLimits, textureDimension3d), false},
    {"maxTextureArrayLayers", offsetof(mrhiLimits, textureArrayLayers), false},
    {"maxBindGroups", offsetof(mrhiLimits, bindingTables), false},
    {"maxBindingsPerBindGroup", offsetof(mrhiLimits, bindingsPerTable), false},
    {"maxSampledTexturesPerShaderStage", offsetof(mrhiLimits, sampledTexturesPerStage), false},
    {"maxSamplersPerShaderStage", offsetof(mrhiLimits, samplersPerStage), false},
    {"maxStorageBuffersPerShaderStage", offsetof(mrhiLimits, storageBuffersPerStage), false},
    {"maxStorageTexturesPerShaderStage", offsetof(mrhiLimits, storageTexturesPerStage), false},
    {"maxUniformBuffersPerShaderStage", offsetof(mrhiLimits, uniformBuffersPerStage), false},
    {"maxUniformBufferBindingSize", offsetof(mrhiLimits, uniformBindingBytes), false},
    {"maxStorageBufferBindingSize", offsetof(mrhiLimits, storageBindingBytes), true},
    {"minUniformBufferOffsetAlignment", offsetof(mrhiLimits, uniformOffsetAlignment), false},
    {"minStorageBufferOffsetAlignment", offsetof(mrhiLimits, storageOffsetAlignment), false},
    {"maxVertexBuffers", offsetof(mrhiLimits, vertexBuffers), false},
    {"maxBindGroupsPlusVertexBuffers", offsetof(mrhiLimits, tablesPlusVertexBuffers), false},
    {"maxBufferSize", offsetof(mrhiLimits, bufferBytes), true},
    {"maxVertexAttributes", offsetof(mrhiLimits, vertexAttributes), false},
    {"maxVertexBufferArrayStride", offsetof(mrhiLimits, vertexStride), false},
    {"maxInterStageShaderVariables", offsetof(mrhiLimits, interStageVariables), false},
    {"maxColorAttachments", offsetof(mrhiLimits, colorAttachments), false},
    {"maxColorAttachmentBytesPerSample", offsetof(mrhiLimits, colorBytesPerSample), false},
    {"maxComputeWorkgroupStorageSize", offsetof(mrhiLimits, workgroupStorageBytes), false},
    {"maxComputeInvocationsPerWorkgroup", offsetof(mrhiLimits, workgroupInvocations), false},
    {"maxComputeWorkgroupSizeX", offsetof(mrhiLimits, workgroupSizeX), false},
    {"maxComputeWorkgroupSizeY", offsetof(mrhiLimits, workgroupSizeY), false},
    {"maxComputeWorkgroupSizeZ", offsetof(mrhiLimits, workgroupSizeZ), false},
    {"maxComputeWorkgroupsPerDimension", offsetof(mrhiLimits, workgroupsPerDimension), false},
    {"maxImmediateSize", offsetof(mrhiLimits, rootBlockBytes), false},
};

// A limit's value in its field's width, clamped.
static void SetLimit(mrhiLimits* limits, const Limit* limit, double value)
{
    unsigned char* field = (unsigned char*)limits + limit->offset;
    if (limit->wide)
    {
        uint64_t wide = value < 18446744073709551615.0 ? (uint64_t)value : UINT64_MAX;
        memcpy(field, &wide, sizeof(wide));
        return;
    }
    uint32_t narrow = value < 4294967295.0 ? (uint32_t)value : UINT32_MAX;
    memcpy(field, &narrow, sizeof(narrow));
}

// Reads the adapter the last search found, when there is one with
// WebGPU's core features: false otherwise.
static bool Describe(const WebGpuDriver* driver, mrhiDriverAdapter* adapterOut)
{
    if (!JsHasAdapter(driver->state) || !JsAdapterHas(driver->state, "core-features-and-limits"))
    {
        return false;
    }
    *adapterOut = (mrhiDriverAdapter){.handle = ADAPTER_HANDLE};
    mrhiAdapterInfo* info = &adapterOut->info;
    info->driver = mrhi_driverWebGpu;
    info->kind = JsAdapterFallback(driver->state) ? mrhi_adapterSoftware : mrhi_adapterUnknown;
    // Room for the terminating NUL stringToUTF8 writes.
    char name[MRHI_ADAPTER_NAME_BYTES + 1];
    int length = JsAdapterName(driver->state, name, (int)sizeof(name));
    memcpy(info->name, name, (size_t)length);
    info->nameLength = (uint32_t)length;
    for (size_t i = 0; i < sizeof(s_features) / sizeof(s_features[0]); ++i)
    {
        bool has = JsAdapterHas(driver->state, s_features[i].name);
        memcpy((unsigned char*)&adapterOut->features + s_features[i].offset, &has, sizeof(has));
    }
    for (size_t i = 0; i < sizeof(s_limits) / sizeof(s_limits[0]); ++i)
    {
        SetLimit(&adapterOut->limits, &s_limits[i],
                 JsAdapterLimit(driver->state, s_limits[i].name));
    }
    adapterOut->limits.framesInFlight = WEBGPU_FRAMES_IN_FLIGHT;
    return true;
}

// Takes a free request slot for a tag: its index, or slotCount when all
// are waiting.
static uint32_t TakeSlot(WebGpuDriver* driver, uint64_t tag)
{
    for (uint32_t i = 0; i < driver->slotCount; ++i)
    {
        if (driver->tags[i] == 0)
        {
            driver->tags[i] = tag;
            return i;
        }
    }
    return driver->slotCount;
}

static mrhiResult RequestAdapters(void* self, uint64_t tag)
{
    WebGpuDriver* driver = self;
    MRHI_ASSERT(tag != 0);
    uint32_t slot = TakeSlot(driver, tag);
    if (slot == driver->slotCount)
    {
        return mrhi_errorCapacity;
    }
    JsRequestAdapter(driver->state, slot);
    return mrhi_success;
}

static size_t Poll(void* self, mrhiDriverEvent* events, size_t capacity)
{
    WebGpuDriver* driver = self;
    size_t moved = 0;
    int32_t outcome = 0;
    int slot = moved < capacity ? JsTakeSettled(driver->state, &outcome) : -1;
    while (slot >= 0)
    {
        MRHI_ASSERT((uint32_t)slot < driver->slotCount && driver->tags[slot] != 0);
        // A search reads what it found as it settles.
        driver->found = Describe(driver, &driver->adapter);
        events[moved++] = (mrhiDriverEvent){.tag = driver->tags[slot], .outcome = outcome};
        driver->tags[slot] = 0;
        slot = moved < capacity ? JsTakeSettled(driver->state, &outcome) : -1;
    }
    return moved;
}

static size_t GetAdapters(const void* self, mrhiDriverAdapter* adapters, size_t capacity)
{
    const WebGpuDriver* driver = self;
    if (driver->found && capacity > 0)
    {
        adapters[0] = driver->adapter;
    }
    return driver->found ? 1 : 0;
}

static void GetFormatCaps(const void* self, uint64_t adapter, mrhiFormat format,
                          mrhiFormatCaps* capsOut)
{
    const WebGpuDriver* driver = self;
    MRHI_ASSERT(adapter == ADAPTER_HANDLE && driver->found);
    *capsOut = mrhiGrantedFormatCaps(format, &driver->adapter.features);
}

// Canvases come with the presentation slice.
static mrhiResult CreateSurface(void* self, const mrhiChain* source, const mrhiSurfaceDef* def,
                                uint64_t* handleOut)
{
    (void)self;
    (void)source;
    (void)def;
    (void)handleOut;
    return mrhi_errorUnsupported;
}

static void DestroySurface(void* self, uint64_t handle)
{
    (void)self;
    (void)handle;
    MRHI_ASSERT(false);
}

static void GetSurfaceCaps(const void* self, uint64_t surface, uint64_t adapter,
                           mrhiSurfaceCaps* capsOut)
{
    (void)self;
    (void)surface;
    (void)adapter;
    *capsOut = (mrhiSurfaceCaps){0};
}

// Devices come with the device slice.
static mrhiResult CreateDevice(void* self, uint64_t adapter, const mrhiDeviceDef* def, uint64_t tag,
                               mrhiDeviceDriver* deviceOut)
{
    (void)self;
    (void)adapter;
    (void)def;
    (void)tag;
    (void)deviceOut;
    return mrhi_errorUnsupported;
}

static void Destroy(void* self)
{
    WebGpuDriver* driver = self;
    JsDestroyState(driver->state);
    mrhiAllocator allocator = driver->allocator;
    mrhiRelease(&allocator, driver, driver->bytes, alignof(WebGpuDriver));
}

static const mrhiInstanceDriverVtable s_vtable = {
    .spiVersion = MRHI_SPI_VERSION,
    .size = sizeof(mrhiInstanceDriverVtable),
    .requestAdapters = RequestAdapters,
    .poll = Poll,
    .getAdapters = GetAdapters,
    .getFormatCaps = GetFormatCaps,
    .createSurface = CreateSurface,
    .destroySurface = DestroySurface,
    .getSurfaceCaps = GetSurfaceCaps,
    .createDevice = CreateDevice,
    .destroy = Destroy,
};

mrhiResult mrhiCreateWebGpuDriver(const mrhiAllocator* allocator, uint32_t pendingLimit,
                                  mrhiInstanceDriver* driverOut)
{
    *driverOut = (mrhiInstanceDriver){0};
    mrhiLayout layout = {.size = sizeof(WebGpuDriver)};
    size_t tagsAt = mrhiLayoutAdd(&layout, pendingLimit, sizeof(uint64_t), alignof(uint64_t));
    WebGpuDriver* driver =
        layout.overflow ? nullptr : mrhiAllocate(allocator, layout.size, alignof(WebGpuDriver));
    if (driver == nullptr)
    {
        return mrhi_errorCapacity;
    }
    unsigned char* block = (unsigned char*)driver;
    *driver = (WebGpuDriver){
        .allocator = *allocator,
        .bytes = layout.size,
        .state = JsCreateState(),
        .tags = (uint64_t*)(block + tagsAt),
        .slotCount = pendingLimit,
    };
    memset(driver->tags, 0, (size_t)pendingLimit * sizeof(uint64_t));
    *driverOut = (mrhiInstanceDriver){.vtable = &s_vtable, .self = driver};
    return mrhi_success;
}
