// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The D3D12 driver's devices (mrhi-0003): a D3D12 device, its direct
// command queue and its objects (d3d12_resource.c), released when the
// device is destroyed, and its shaders and pipelines (d3d12_pipeline.c).
// A destroyed object or pipeline waits until no frame can name it;
// until frames run, that is the device's end. Frames land in the
// driver's later slices; until then submitting one answers
// mrhi_errorUnsupported.

#include "d3d12_device.h"

#include "allocator.h"
#include "d3d12_names.h"
#include "d3d12_pipeline.h"
#include "d3d12_resource.h"
#include "invariant.h"

#include <stdalign.h>

// A destroyed object or pipeline waiting until no frame can name it.
typedef struct Retiree
{
    uint64_t handle;
    mrhiD3d12Kind kind;
    bool pipeline;
} Retiree;

typedef struct D3d12Device
{
    mrhiAllocator allocator;
    size_t bytes;
    mrhiD3d12Api api;
    ID3D12Device* device;
    ID3D12CommandQueue* queue;
    mrhiD3d12Objects objects;
    mrhiD3d12Pipelines pipelines;
    Retiree* retirees;
    uint32_t retireeCount;
    uint32_t retireeLimit;
} D3d12Device;

static void Retire(D3d12Device* device, uint64_t handle, mrhiD3d12Kind kind)
{
    // One entry per object the device holds at most.
    MRHI_ASSERT(device->retireeCount < device->retireeLimit);
    device->retirees[device->retireeCount++] = (Retiree){.handle = handle, .kind = kind};
}

static void RetirePipeline(D3d12Device* device, uint64_t handle)
{
    MRHI_ASSERT(device->retireeCount < device->retireeLimit);
    device->retirees[device->retireeCount++] = (Retiree){.handle = handle, .pipeline = true};
}

static void ReleaseRetirees(D3d12Device* device)
{
    for (uint32_t i = 0; i < device->retireeCount; ++i)
    {
        const Retiree* retiree = &device->retirees[i];
        if (retiree->pipeline)
        {
            mrhiD3d12ReleasePipeline(&device->pipelines, retiree->handle);
        }
        else
        {
            mrhiD3d12ReleaseObject(&device->objects, retiree->kind, retiree->handle);
        }
    }
    device->retireeCount = 0;
}

static void Destroy(void* self)
{
    D3d12Device* device = self;
    ReleaseRetirees(device);
    mrhiD3d12CloseObjects(&device->objects);
    ID3D12CommandQueue_Release(device->queue);
    ID3D12Device_Release(device->device);
    mrhiAllocator allocator = device->allocator;
    mrhiRelease(&allocator, device, device->bytes, alignof(D3d12Device));
}

static mrhiResult CreateSampler(void* self, const mrhiSamplerDef* def, uint64_t* handleOut)
{
    D3d12Device* device = self;
    return mrhiD3d12CreateSampler(&device->objects, def, handleOut);
}

static mrhiResult CreateBuffer(void* self, const mrhiBufferDef* def, uint64_t* handleOut)
{
    D3d12Device* device = self;
    return mrhiD3d12CreateBuffer(&device->objects, def, handleOut);
}

static mrhiResult CreateTexture(void* self, const mrhiTextureDef* def, uint64_t* handleOut)
{
    D3d12Device* device = self;
    return mrhiD3d12CreateTexture(&device->objects, def, handleOut);
}

static mrhiResult CreateView(void* self, uint64_t texture, const mrhiViewDef* def,
                             uint64_t* handleOut)
{
    D3d12Device* device = self;
    return mrhiD3d12CreateView(&device->objects, texture, def, handleOut);
}

static void DestroySampler(void* self, uint64_t handle)
{
    Retire(self, handle, mrhiD3d12KindSampler);
}

static void DestroyBuffer(void* self, uint64_t handle)
{
    Retire(self, handle, mrhiD3d12KindBuffer);
}

static void DestroyTexture(void* self, uint64_t handle)
{
    Retire(self, handle, mrhiD3d12KindTexture);
}

static void DestroyView(void* self, uint64_t handle)
{
    Retire(self, handle, mrhiD3d12KindView);
}

static void DestroyQuerySet(void* self, uint64_t handle)
{
    Retire(self, handle, mrhiD3d12KindQuerySet);
}

// What is not made yet is never destroyed.
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
    D3d12Device* device = self;
    return mrhiD3d12CreateShader(&device->pipelines, def, container, handleOut);
}

static void DestroyShader(void* self, uint64_t handle)
{
    D3d12Device* device = self;
    mrhiD3d12DestroyShader(&device->pipelines, handle);
}

static mrhiResult CreateComputePipeline(void* self, const mrhiDriverComputePipeline* pipeline,
                                        uint64_t tag, uint64_t* handleOut)
{
    D3d12Device* device = self;
    return mrhiD3d12CreateCompute(&device->pipelines, pipeline, tag, handleOut);
}

static mrhiResult CreateGraphicsPipeline(void* self, const mrhiDriverGraphicsPipeline* pipeline,
                                         uint64_t tag, uint64_t* handleOut)
{
    D3d12Device* device = self;
    return mrhiD3d12CreateGraphics(&device->pipelines, pipeline, tag, handleOut);
}

static void DestroyPipeline(void* self, uint64_t handle)
{
    D3d12Device* device = self;
    mrhiD3d12ForgetPipeline(&device->pipelines, handle);
    RetirePipeline(device, handle);
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

static mrhiResult CreateQuerySet(void* self, const mrhiQuerySetDef* def, uint64_t* handleOut)
{
    D3d12Device* device = self;
    return mrhiD3d12CreateQuerySet(&device->objects, def, handleOut);
}

// Timestamps are not granted yet, so the core never asks.
static double TimestampPeriod(void* self)
{
    (void)self;
    return 0.0;
}

// No pipeline is made yet, so the driver's cache is empty: it takes an
// empty one and declines any other.
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
    const D3d12Device* device = self;
    mrhiD3d12TextureMemory(&device->objects, def, bytesOut, alignmentOut);
}

static void BufferMemory(const void* self, const mrhiBufferDef* def, uint64_t* bytesOut,
                         uint64_t* alignmentOut)
{
    const D3d12Device* device = self;
    mrhiD3d12BufferMemory(&device->objects, def, bytesOut, alignmentOut);
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
    D3d12Device* device = self;
    return mrhiD3d12PollPipelines(&device->pipelines, events, capacity);
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
    .destroySampler = DestroySampler,
    .createBuffer = CreateBuffer,
    .destroyBuffer = DestroyBuffer,
    .createTexture = CreateTexture,
    .destroyTexture = DestroyTexture,
    .createView = CreateView,
    .destroyView = DestroyView,
    .configureSurface = ConfigureSurface,
    .unconfigureSurface = Never,
    .createShader = CreateShader,
    .destroyShader = DestroyShader,
    .createComputePipeline = CreateComputePipeline,
    .createGraphicsPipeline = CreateGraphicsPipeline,
    .destroyPipeline = DestroyPipeline,
    .lossReport = LossReport,
    .acquireImage = AcquireImage,
    .releaseImage = ReleaseImage,
    .createQuerySet = CreateQuerySet,
    .destroyQuerySet = DestroyQuerySet,
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

// The room a device takes: itself, its object and pipeline tables and
// its retirees.
typedef struct Room
{
    mrhiLayout layout;
    mrhiD3d12ObjectRoom objects;
    mrhiD3d12PipelineRoom pipelines;
    size_t retirees;
    uint32_t retireeLimit;
} Room;

static Room RoomOf(const mrhiDeviceLimits* limits)
{
    Room room = {.layout = {.size = sizeof(D3d12Device)}};
    room.objects = mrhiD3d12PlanObjects(&room.layout, limits);
    room.pipelines = mrhiD3d12PlanPipelines(&room.layout, limits);
    uint64_t retirees = (uint64_t)limits->buffers + limits->textures + limits->views +
                        limits->samplers + limits->querySets + limits->pipelines;
    room.retireeLimit = retirees < UINT32_MAX ? (uint32_t)retirees : UINT32_MAX;
    room.retirees =
        mrhiLayoutAdd(&room.layout, room.retireeLimit, sizeof(Retiree), alignof(Retiree));
    return room;
}

// Lays out a device's parts in its block, around its D3D12 device and
// queue.
static void Lay(D3d12Device* made, const Room* room, const mrhiDeviceLimits* limits)
{
    unsigned char* block = (unsigned char*)made;
    made->objects = (mrhiD3d12Objects){.device = made->device};
    mrhiD3d12LayObjects(&made->objects, block, &room->objects, limits);
    made->pipelines = (mrhiD3d12Pipelines){
        .allocator = &made->allocator,
        .api = &made->api,
        .device = made->device,
    };
    mrhiD3d12LayPipelines(&made->pipelines, block, &room->pipelines, limits);
    made->retirees = (Retiree*)(block + room->retirees);
    made->retireeLimit = room->retireeLimit;
}

mrhiResult mrhiCreateD3d12Device(const mrhiAllocator* allocator, const mrhiD3d12Api* api,
                                 ID3D12Device* device, const mrhiDeviceDef* def,
                                 mrhiDeviceDriver* deviceOut)
{
    Room room = RoomOf(&def->deviceLimits);
    D3d12Device* made = room.layout.overflow
                            ? nullptr
                            : mrhiAllocate(allocator, room.layout.size, alignof(D3d12Device));
    if (made == nullptr)
    {
        ID3D12Device_Release(device);
        return mrhi_errorCapacity;
    }
    D3D12_COMMAND_QUEUE_DESC desc = {.Type = D3D12_COMMAND_LIST_TYPE_DIRECT};
    ID3D12CommandQueue* queue = nullptr;
    if (FAILED(ID3D12Device_CreateCommandQueue(device, &desc, &IID_ID3D12CommandQueue,
                                               (void**)&queue)))
    {
        mrhiRelease(allocator, made, room.layout.size, alignof(D3d12Device));
        ID3D12Device_Release(device);
        return mrhi_errorPlatform;
    }
    mrhiD3d12Label((ID3D12Object*)queue, def->label, def->labelLength);
    *made = (D3d12Device){
        .allocator = *allocator,
        .bytes = room.layout.size,
        .api = *api,
        .device = device,
        .queue = queue,
    };
    Lay(made, &room, &def->deviceLimits);
    if (mrhiD3d12OpenObjects(&made->objects) != mrhi_success)
    {
        Destroy(made);
        return mrhi_errorCapacity;
    }
    *deviceOut = (mrhiDeviceDriver){.vtable = &s_vtable, .self = made};
    return mrhi_success;
}
