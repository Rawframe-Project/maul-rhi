// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Metal driver's devices (mrhi-0003): a retained Metal device and
// its command queue, released when the device is destroyed, and its
// objects (metal_resource.m). Shaders, pipelines, frames, surfaces and
// heaps are not made yet: those calls answer mrhi_errorUnsupported, and
// the conformance suite checks only the objects on Metal devices.

#include "metal_device.h"

#include "allocator.h"
#include "invariant.h"
#include "metal_resource.h"

#include <stdalign.h>
#include <string.h>

typedef struct MetalDevice
{
    mrhiAllocator allocator;
    id<MTLDevice> device;
    id<MTLCommandQueue> queue;
} MetalDevice;

static void Destroy(void* self)
{
    MetalDevice* device = self;
    [device->queue release];
    [device->device release];
    mrhiAllocator allocator = device->allocator;
    mrhiRelease(&allocator, device, sizeof(MetalDevice), alignof(MetalDevice));
}

static mrhiResult CreateSampler(void* self, const mrhiSamplerDef* def, uint64_t* handleOut)
{
    const MetalDevice* device = self;
    return mrhiMetalCreateSampler(device->device, def, handleOut);
}

static mrhiResult CreateBuffer(void* self, const mrhiBufferDef* def, uint64_t* handleOut)
{
    const MetalDevice* device = self;
    return mrhiMetalCreateBuffer(device->device, def, handleOut);
}

static mrhiResult CreateTexture(void* self, const mrhiTextureDef* def, uint64_t* handleOut)
{
    const MetalDevice* device = self;
    return mrhiMetalCreateTexture(device->device, def, handleOut);
}

static mrhiResult CreateView(void* self, uint64_t texture, const mrhiViewDef* def,
                             uint64_t* handleOut)
{
    (void)self;
    return mrhiMetalCreateView(texture, def, handleOut);
}

static mrhiResult CreateQuerySet(void* self, const mrhiQuerySetDef* def, uint64_t* handleOut)
{
    const MetalDevice* device = self;
    return mrhiMetalCreateQuerySet(device->device, def, handleOut);
}

// Every object is released at once: the command buffers using it hold
// their own references.
static void DestroyObject(void* self, uint64_t handle)
{
    (void)self;
    mrhiMetalRelease(handle);
}

// Shaders, pipelines, swapchains and heaps are never made, so never
// destroyed.
static void Never(void* self, uint64_t handle)
{
    (void)self;
    (void)handle;
    MRHI_ASSERT(false);
}

static mrhiResult ConfigureSurface(void* self, uint64_t surface, const mrhiSurfaceConfig* config,
                                   uint64_t oldSwapchain, uint64_t* swapchainOut)
{
    (void)self;
    (void)surface;
    (void)config;
    (void)oldSwapchain;
    *swapchainOut = 0;
    return mrhi_errorUnsupported;
}

static mrhiResult CreateShader(void* self, const mrhiShaderDef* def, const mrhiContainer* container,
                               uint64_t* handleOut)
{
    (void)self;
    (void)def;
    (void)container;
    *handleOut = 0;
    return mrhi_errorUnsupported;
}

static mrhiResult CreateComputePipeline(void* self, const mrhiDriverComputePipeline* pipeline,
                                        uint64_t tag, uint64_t* handleOut)
{
    (void)self;
    (void)pipeline;
    (void)tag;
    *handleOut = 0;
    return mrhi_errorUnsupported;
}

static mrhiResult CreateGraphicsPipeline(void* self, const mrhiDriverGraphicsPipeline* pipeline,
                                         uint64_t tag, uint64_t* handleOut)
{
    (void)self;
    (void)pipeline;
    (void)tag;
    *handleOut = 0;
    return mrhi_errorUnsupported;
}

static void LossReport(void* self, mrhiDeviceLossReport* reportOut)
{
    (void)self;
    *reportOut = (mrhiDeviceLossReport){.reason = mrhi_lossUnknown};
}

static mrhiResult AcquireImage(void* self, uint64_t swapchain, uint64_t* imageOut)
{
    (void)self;
    (void)swapchain;
    *imageOut = 0;
    return mrhi_errorUnsupported;
}

static void ReleaseImage(void* self, uint64_t swapchain, uint64_t image)
{
    (void)self;
    (void)swapchain;
    (void)image;
    MRHI_ASSERT(false);
}

// Timestamps are not granted yet, so the core never asks.
static double TimestampPeriod(void* self)
{
    (void)self;
    return 0.0;
}

// Metal keeps its own cache of compiled functions: the driver's is
// empty, so it takes an empty one and declines any other.
static bool ImportPipelineCache(void* self, const void* bytes, size_t size)
{
    (void)self;
    (void)bytes;
    return size == 0;
}

static size_t ExportPipelineCache(void* self, void* bytes, size_t capacity)
{
    (void)self;
    (void)bytes;
    (void)capacity;
    return 0;
}

static void TextureMemory(const void* self, const mrhiTextureDef* def, uint64_t* bytesOut,
                          uint64_t* alignmentOut)
{
    const MetalDevice* device = self;
    mrhiMetalTextureMemory(device->device, def, bytesOut, alignmentOut);
}

static void BufferMemory(const void* self, const mrhiBufferDef* def, uint64_t* bytesOut,
                         uint64_t* alignmentOut)
{
    const MetalDevice* device = self;
    mrhiMetalBufferMemory(device->device, def, bytesOut, alignmentOut);
}

static mrhiResult SubmitFrame(void* self, const mrhiDriverFrame* frame, uint64_t tag)
{
    (void)self;
    (void)frame;
    (void)tag;
    return mrhi_errorUnsupported;
}

static size_t Poll(void* self, mrhiDriverEvent* events, size_t capacity)
{
    (void)self;
    (void)events;
    (void)capacity;
    return 0;
}

static bool WaitFrame(void* self, uint64_t tag, uint64_t timeoutNs)
{
    (void)self;
    (void)tag;
    (void)timeoutNs;
    return true;
}

static mrhiResult CreateHeap(void* self, const mrhiHeapDef* def, uint64_t* handleOut)
{
    (void)self;
    (void)def;
    *handleOut = 0;
    return mrhi_errorUnsupported;
}

static void WriteHeapEntry(void* self, uint64_t heap, uint32_t index,
                           const mrhiDriverHeapEntry* entry)
{
    (void)self;
    (void)heap;
    (void)index;
    (void)entry;
    MRHI_ASSERT(false);
}

static void WriteHeapSampler(void* self, uint64_t heap, uint32_t index, uint64_t sampler)
{
    (void)self;
    (void)heap;
    (void)index;
    (void)sampler;
    MRHI_ASSERT(false);
}

static const mrhiDeviceDriverVtable s_vtable = {
    .spiVersion = MRHI_SPI_VERSION,
    .size = sizeof(mrhiDeviceDriverVtable),
    .destroy = Destroy,
    .createSampler = CreateSampler,
    .destroySampler = DestroyObject,
    .createBuffer = CreateBuffer,
    .destroyBuffer = DestroyObject,
    .createTexture = CreateTexture,
    .destroyTexture = DestroyObject,
    .createView = CreateView,
    .destroyView = DestroyObject,
    .configureSurface = ConfigureSurface,
    .unconfigureSurface = Never,
    .createShader = CreateShader,
    .destroyShader = Never,
    .createComputePipeline = CreateComputePipeline,
    .createGraphicsPipeline = CreateGraphicsPipeline,
    .destroyPipeline = Never,
    .lossReport = LossReport,
    .acquireImage = AcquireImage,
    .releaseImage = ReleaseImage,
    .createQuerySet = CreateQuerySet,
    .destroyQuerySet = DestroyObject,
    .timestampPeriod = TimestampPeriod,
    .importPipelineCache = ImportPipelineCache,
    .exportPipelineCache = ExportPipelineCache,
    .textureMemory = TextureMemory,
    .bufferMemory = BufferMemory,
    .submitFrame = SubmitFrame,
    .poll = Poll,
    .waitFrame = WaitFrame,
    .createHeap = CreateHeap,
    .destroyHeap = Never,
    .writeHeapEntry = WriteHeapEntry,
    .writeHeapSampler = WriteHeapSampler,
};

mrhiResult mrhiCreateMetalDevice(const mrhiAllocator* allocator, id<MTLDevice> device,
                                 const mrhiDeviceDef* def, mrhiDeviceDriver* deviceOut)
{
    MetalDevice* made = mrhiAllocate(allocator, sizeof(MetalDevice), alignof(MetalDevice));
    if (made == nullptr)
    {
        return mrhi_errorCapacity;
    }
    @autoreleasepool
    {
        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (queue == nil)
        {
            mrhiRelease(allocator, made, sizeof(MetalDevice), alignof(MetalDevice));
            return mrhi_errorPlatform;
        }
        if (def->labelLength > 0)
        {
            queue.label = [[[NSString alloc] initWithBytes:def->label
                                                    length:def->labelLength
                                                  encoding:NSUTF8StringEncoding] autorelease];
        }
        *made = (MetalDevice){
            .allocator = *allocator,
            .device = [device retain],
            .queue = queue,
        };
    }
    *deviceOut = (mrhiDeviceDriver){.vtable = &s_vtable, .self = made};
    return mrhi_success;
}
