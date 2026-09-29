// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device (mrhi-0003): opened with the floor's features and the
// granted ones, one queue, and a timeline semaphore for the frames. The
// memory a declared resource takes is read from the create info alone
// (Vulkan 1.3's device memory requirements). Buffers, textures, views
// and samplers are made in device-local memory; shaders, pipelines,
// queries, frames and surfaces are refused as unsupported until their
// slices land.

#include "vulkan_device.h"

#include "allocator.h"
#include "invariant.h"
#include "vulkan_adapter.h"
#include "vulkan_object.h"
#include "vulkan_resource.h"

#include <stdalign.h>
#include <string.h>

typedef struct VulkanDevice
{
    mrhiAllocator allocator;
    const mrhiVulkan* vulkan;
    mrhiVulkanDevice api;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    // Its value is the last frame finished.
    VkSemaphore timeline;
    VkFormat depthStencil;
    // The alignment every placement in the frame's memory keeps, so that
    // buffers and images may lie side by side.
    VkDeviceSize granularity;
    // The memory types allocated lazily, where a tile GPU keeps
    // transient targets on chip.
    uint32_t lazyTypes;
    double timestampPeriod;
    size_t bytes;
    mrhiVulkanMemory memory;
    mrhiVulkanObjects objects;
} VulkanDevice;

// The features a device enables, chained.
typedef struct Enabled
{
    VkPhysicalDeviceFeatures2 features;
    VkPhysicalDeviceVulkan11Features features11;
    VkPhysicalDeviceVulkan12Features features12;
    VkPhysicalDeviceVulkan13Features features13;
} Enabled;

// The floor's features, and the granted ones as the contract's Vulkan
// rows name them.
static void Enable(const mrhiFeatures* granted, Enabled* enabled)
{
    *enabled = (Enabled){
        .features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2},
        .features11 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES},
        .features12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES},
        .features13 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES},
    };
    enabled->features.pNext = &enabled->features11;
    enabled->features11.pNext = &enabled->features12;
    enabled->features12.pNext = &enabled->features13;
    VkPhysicalDeviceFeatures* core = &enabled->features.features;
    core->fullDrawIndexUint32 = VK_TRUE;
    core->imageCubeArray = VK_TRUE;
    core->independentBlend = VK_TRUE;
    core->sampleRateShading = VK_TRUE;
    core->depthBiasClamp = VK_TRUE;
    core->fragmentStoresAndAtomics = VK_TRUE;
    core->samplerAnisotropy = VK_TRUE;
    core->shaderStorageImageExtendedFormats = VK_TRUE;
    core->pipelineStatisticsQuery = granted->pipelineStatisticsQuery;
    core->textureCompressionBC = granted->textureCompressionBc;
    core->textureCompressionETC2 = granted->textureCompressionEtc2;
    core->textureCompressionASTC_LDR = granted->textureCompressionAstc;
    core->dualSrcBlend = granted->dualSourceBlending;
    core->depthClamp = granted->unclippedDepth;
    core->shaderInt64 = granted->shaderInt64;
    core->drawIndirectFirstInstance = granted->indirectFirstInstance;
    core->multiDrawIndirect = granted->multiDrawIndirectCount;
    enabled->features11.multiview = granted->multiview;
    enabled->features11.storageBuffer16BitAccess = granted->shaderF16;
    enabled->features11.uniformAndStorageBuffer16BitAccess = granted->shaderF16;
    enabled->features12.shaderFloat16 = granted->shaderF16;
    enabled->features12.drawIndirectCount = granted->multiDrawIndirectCount;
    enabled->features12.timelineSemaphore = VK_TRUE;
    enabled->features12.bufferDeviceAddress = VK_TRUE;
    enabled->features12.descriptorIndexing = VK_TRUE;
    enabled->features13.dynamicRendering = VK_TRUE;
    enabled->features13.synchronization2 = VK_TRUE;
}

static mrhiResult StatusOf(VkResult result)
{
    switch (result)
    {
    case VK_SUCCESS:
        return mrhi_success;
    case VK_ERROR_OUT_OF_HOST_MEMORY:
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return mrhi_errorCapacity;
    case VK_ERROR_DEVICE_LOST:
        return mrhi_errorDeviceLost;
    default:
        return mrhi_errorPlatform;
    }
}

// Destroys the objects the core leaves to the device's end.
static void DestroyObjects(mrhiVulkanObjects* objects)
{
    for (uint32_t i = 0; i < objects->viewSlots.capacity; ++i)
    {
        objects->api->vkDestroyImageView(objects->device, objects->views[i], nullptr);
    }
    for (uint32_t i = 0; i < objects->samplerSlots.capacity; ++i)
    {
        objects->api->vkDestroySampler(objects->device, objects->samplers[i], nullptr);
    }
    for (uint32_t i = 0; i < objects->bufferSlots.capacity; ++i)
    {
        objects->api->vkDestroyBuffer(objects->device, objects->buffers[i].buffer, nullptr);
    }
    for (uint32_t i = 0; i < objects->textureSlots.capacity; ++i)
    {
        objects->api->vkDestroyImage(objects->device, objects->textures[i].image, nullptr);
    }
}

static void Destroy(void* self)
{
    VulkanDevice* device = self;
    // Nothing runs once a device is destroyed; a lost device answers at
    // once.
    (void)device->api.vkDeviceWaitIdle(device->device);
    DestroyObjects(&device->objects);
    mrhiVulkanMemoryDestroy(&device->memory);
    device->api.vkDestroySemaphore(device->device, device->timeline, nullptr);
    device->api.vkDestroyDevice(device->device, nullptr);
    mrhiAllocator allocator = device->allocator;
    mrhiRelease(&allocator, device, device->bytes, alignof(VulkanDevice));
}

static mrhiResult CreateSampler(void* self, const mrhiSamplerDef* def, uint64_t* handleOut)
{
    VulkanDevice* device = self;
    return mrhiVulkanCreateSampler(&device->objects, def, handleOut);
}

static void DestroySampler(void* self, uint64_t handle)
{
    VulkanDevice* device = self;
    mrhiVulkanDestroySampler(&device->objects, handle);
}

static mrhiResult CreateBuffer(void* self, const mrhiBufferDef* def, uint64_t* handleOut)
{
    VulkanDevice* device = self;
    return mrhiVulkanCreateBuffer(&device->objects, def, handleOut);
}

static void DestroyBuffer(void* self, uint64_t handle)
{
    VulkanDevice* device = self;
    mrhiVulkanDestroyBuffer(&device->objects, handle);
}

static mrhiResult CreateTexture(void* self, const mrhiTextureDef* def, uint64_t* handleOut)
{
    VulkanDevice* device = self;
    return mrhiVulkanCreateTexture(&device->objects, def, handleOut);
}

static void DestroyTexture(void* self, uint64_t handle)
{
    VulkanDevice* device = self;
    mrhiVulkanDestroyTexture(&device->objects, handle);
}

static mrhiResult CreateView(void* self, uint64_t texture, const mrhiViewDef* def,
                             uint64_t* handleOut)
{
    VulkanDevice* device = self;
    return mrhiVulkanCreateView(&device->objects, texture, def, handleOut);
}

static void DestroyView(void* self, uint64_t handle)
{
    VulkanDevice* device = self;
    mrhiVulkanDestroyView(&device->objects, handle);
}

static void TextureMemory(const void* self, const mrhiTextureDef* def, uint64_t* bytesOut,
                          uint64_t* alignmentOut)
{
    const VulkanDevice* device = self;
    mrhiVulkanImage image;
    mrhiVulkanImageOf(def, device->depthStencil, &image);
    const VkDeviceImageMemoryRequirements query = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS,
        .pCreateInfo = &image.info,
    };
    VkMemoryRequirements2 needs = {.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    device->api.vkGetDeviceImageMemoryRequirements(device->device, &query, &needs);
    bool onChip = (def->usage & mrhi_textureTransient) != 0 &&
                  (needs.memoryRequirements.memoryTypeBits & device->lazyTypes) != 0;
    VkDeviceSize alignment = needs.memoryRequirements.alignment;
    *bytesOut = onChip ? 0 : needs.memoryRequirements.size;
    *alignmentOut = alignment > device->granularity ? alignment : device->granularity;
}

static void BufferMemory(const void* self, const mrhiBufferDef* def, uint64_t* bytesOut,
                         uint64_t* alignmentOut)
{
    const VulkanDevice* device = self;
    const VkBufferCreateInfo info = mrhiVulkanBufferOf(def);
    const VkDeviceBufferMemoryRequirements query = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS,
        .pCreateInfo = &info,
    };
    VkMemoryRequirements2 needs = {.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    device->api.vkGetDeviceBufferMemoryRequirements(device->device, &query, &needs);
    VkDeviceSize alignment = needs.memoryRequirements.alignment;
    *bytesOut = needs.memoryRequirements.size;
    *alignmentOut = alignment > device->granularity ? alignment : device->granularity;
}

static double TimestampPeriod(void* self)
{
    const VulkanDevice* device = self;
    return device->timestampPeriod;
}

static void LossReport(void* self, mrhiDeviceLossReport* reportOut)
{
    (void)self;
    static const char message[] = "Vulkan reported VK_ERROR_DEVICE_LOST";
    *reportOut = (mrhiDeviceLossReport){.messageLength = sizeof(message) - 1};
    memcpy(reportOut->message, message, sizeof(message) - 1);
}

// Finished frames and pipelines come with their slices.
static size_t Poll(void* self, mrhiDriverEvent* events, size_t capacity)
{
    (void)self;
    (void)events;
    (void)capacity;
    return 0;
}

static bool ImportPipelineCache(void* self, const void* bytes, size_t size)
{
    (void)self;
    (void)bytes;
    (void)size;
    return false;
}

static size_t ExportPipelineCache(void* self, void* bytes, size_t capacity)
{
    (void)self;
    (void)bytes;
    (void)capacity;
    return 0;
}

// The entries of later slices: making refuses as unsupported, so the
// core never destroys, acquires or waits on anything of theirs.
static mrhiResult RefuseSurface(void* self, uint64_t surface, const mrhiSurfaceConfig* config,
                                uint64_t oldSwapchain, uint64_t* swapchainOut)
{
    (void)self;
    (void)surface;
    (void)config;
    (void)swapchainOut;
    MRHI_ASSERT(oldSwapchain == 0);
    return mrhi_errorUnsupported;
}

static mrhiResult RefuseShader(void* self, const mrhiShaderDef* def, const mrhiContainer* container,
                               uint64_t* handleOut)
{
    (void)self;
    (void)def;
    (void)container;
    (void)handleOut;
    return mrhi_errorUnsupported;
}

static mrhiResult RefuseCompute(void* self, const mrhiDriverComputePipeline* pipeline, uint64_t tag,
                                uint64_t* handleOut)
{
    (void)self;
    (void)pipeline;
    (void)tag;
    (void)handleOut;
    return mrhi_errorUnsupported;
}

static mrhiResult RefuseGraphics(void* self, const mrhiDriverGraphicsPipeline* pipeline,
                                 uint64_t tag, uint64_t* handleOut)
{
    (void)self;
    (void)pipeline;
    (void)tag;
    (void)handleOut;
    return mrhi_errorUnsupported;
}

static mrhiResult RefuseQuerySet(void* self, const mrhiQuerySetDef* def, uint64_t* handleOut)
{
    (void)self;
    (void)def;
    (void)handleOut;
    return mrhi_errorUnsupported;
}

static mrhiResult RefuseFrame(void* self, const mrhiDriverFrame* frame, uint64_t tag)
{
    (void)self;
    (void)frame;
    (void)tag;
    return mrhi_errorUnsupported;
}

static void NeverDestroyed(void* self, uint64_t handle)
{
    (void)self;
    (void)handle;
    MRHI_ASSERT(false);
}

static mrhiResult NeverAcquired(void* self, uint64_t swapchain, uint64_t* imageOut)
{
    (void)self;
    (void)swapchain;
    (void)imageOut;
    MRHI_ASSERT(false);
    return mrhi_errorOutOfDate;
}

static void NeverReleased(void* self, uint64_t swapchain, uint64_t image)
{
    (void)self;
    (void)swapchain;
    (void)image;
    MRHI_ASSERT(false);
}

static bool NeverWaited(void* self, uint64_t tag, uint64_t timeoutNs)
{
    (void)self;
    (void)tag;
    (void)timeoutNs;
    MRHI_ASSERT(false);
    return false;
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
    .configureSurface = RefuseSurface,
    .unconfigureSurface = NeverDestroyed,
    .createShader = RefuseShader,
    .destroyShader = NeverDestroyed,
    .createComputePipeline = RefuseCompute,
    .createGraphicsPipeline = RefuseGraphics,
    .destroyPipeline = NeverDestroyed,
    .lossReport = LossReport,
    .acquireImage = NeverAcquired,
    .releaseImage = NeverReleased,
    .createQuerySet = RefuseQuerySet,
    .destroyQuerySet = NeverDestroyed,
    .timestampPeriod = TimestampPeriod,
    .importPipelineCache = ImportPipelineCache,
    .exportPipelineCache = ExportPipelineCache,
    .textureMemory = TextureMemory,
    .bufferMemory = BufferMemory,
    .submitFrame = RefuseFrame,
    .poll = Poll,
    .waitFrame = NeverWaited,
};

// Reads what the device keeps of its physical device: the timestamp
// period, the granularity, the lazily allocated types, the depth and
// stencil format.
static void ReadPhysical(VulkanDevice* device, VkPhysicalDeviceMemoryProperties* memoryOut)
{
    const mrhiVulkan* vulkan = device->vulkan;
    VkPhysicalDeviceProperties2 properties = {.sType =
                                                  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    vulkan->vkGetPhysicalDeviceProperties2(device->physical, &properties);
    device->timestampPeriod = (double)properties.properties.limits.timestampPeriod;
    device->granularity = properties.properties.limits.bufferImageGranularity;
    device->objects.maxAnisotropy = properties.properties.limits.maxSamplerAnisotropy;
    *memoryOut = (VkPhysicalDeviceMemoryProperties){0};
    vulkan->vkGetPhysicalDeviceMemoryProperties(device->physical, memoryOut);
    for (uint32_t i = 0; i < memoryOut->memoryTypeCount; ++i)
    {
        bool lazy = (memoryOut->memoryTypes[i].propertyFlags &
                     VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) != 0;
        device->lazyTypes |= lazy ? 1u << i : 0u;
    }
    device->depthStencil = mrhiVulkanDepthStencil(vulkan, device->physical);
}

// Makes the device, its queue and its timeline.
static VkResult Open(VulkanDevice* device, const mrhiDeviceDef* def)
{
    uint32_t family = mrhiVulkanQueueFamily(device->vulkan, device->physical);
    const float priority = 1.0f;
    const VkDeviceQueueCreateInfo queue = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = family,
        .queueCount = 1,
        .pQueuePriorities = &priority,
    };
    Enabled enabled;
    Enable(&def->features, &enabled);
    const VkDeviceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabled.features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue,
    };
    VkResult result =
        device->vulkan->vkCreateDevice(device->physical, &info, nullptr, &device->device);
    if (result != VK_SUCCESS)
    {
        return result;
    }
    if (!mrhiLoadVulkanDevice(device->vulkan, device->device, &device->api))
    {
        PFN_vkDestroyDevice destroy = device->api.vkDestroyDevice;
        if (destroy != nullptr)
        {
            destroy(device->device, nullptr);
        }
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    device->api.vkGetDeviceQueue(device->device, family, 0, &device->queue);
    const VkSemaphoreTypeCreateInfo timeline = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
    };
    const VkSemaphoreCreateInfo semaphore = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &timeline,
    };
    result = device->api.vkCreateSemaphore(device->device, &semaphore, nullptr, &device->timeline);
    if (result != VK_SUCCESS)
    {
        device->api.vkDestroyDevice(device->device, nullptr);
    }
    return result;
}

// Where the device's tables lie in its one block.
typedef struct Layout
{
    mrhiLayout layout;
    size_t pools;
    size_t blocks;
    size_t nodes;
    size_t buffers;
    size_t textures;
    size_t views;
    size_t samplers;
    // The four tables' free slot links, one after another.
    size_t slots;
    uint32_t blockCount;
    uint32_t nodeCount;
} Layout;

static Layout LayoutOf(const mrhiDeviceLimits* limits)
{
    Layout at = {.layout = {.size = sizeof(VulkanDevice)}};
    mrhiVulkanMemoryCounts(limits->buffers + limits->textures, &at.blockCount, &at.nodeCount);
    mrhiLayout* layout = &at.layout;
    at.pools = mrhiLayoutAdd(layout, 2 * VK_MAX_MEMORY_TYPES, sizeof(mrhiVulkanPool),
                             alignof(mrhiVulkanPool));
    at.blocks =
        mrhiLayoutAdd(layout, at.blockCount, sizeof(mrhiVulkanBlock), alignof(mrhiVulkanBlock));
    at.nodes = mrhiLayoutAdd(layout, at.nodeCount, sizeof(mrhiTlsfNode), alignof(mrhiTlsfNode));
    at.buffers =
        mrhiLayoutAdd(layout, limits->buffers, sizeof(mrhiVulkanBuffer), alignof(mrhiVulkanBuffer));
    at.textures = mrhiLayoutAdd(layout, limits->textures, sizeof(mrhiVulkanTexture),
                                alignof(mrhiVulkanTexture));
    at.views = mrhiLayoutAdd(layout, limits->views, sizeof(VkImageView), alignof(VkImageView));
    at.samplers = mrhiLayoutAdd(layout, limits->samplers, sizeof(VkSampler), alignof(VkSampler));
    size_t slots = (size_t)limits->buffers + limits->textures + limits->views + limits->samplers;
    at.slots = mrhiLayoutAdd(layout, slots, sizeof(uint32_t), alignof(uint32_t));
    return at;
}

// Sets up the memory and the object tables in the device's block, all
// entries empty.
static void PlaceTables(VulkanDevice* device, const Layout* at, const mrhiDeviceLimits* limits,
                        const VkPhysicalDeviceMemoryProperties* properties)
{
    unsigned char* block = (unsigned char*)device;
    memset(block + sizeof(VulkanDevice), 0, at->layout.size - sizeof(VulkanDevice));
    mrhiVulkanMemoryInit(&device->memory, &device->api, device->device, properties,
                         (mrhiVulkanPool*)(block + at->pools),
                         (mrhiVulkanBlock*)(block + at->blocks), at->blockCount,
                         (mrhiTlsfNode*)(block + at->nodes), at->nodeCount);
    mrhiVulkanObjects* objects = &device->objects;
    objects->api = &device->api;
    objects->device = device->device;
    objects->memory = &device->memory;
    objects->depthStencil = device->depthStencil;
    objects->buffers = (mrhiVulkanBuffer*)(block + at->buffers);
    objects->textures = (mrhiVulkanTexture*)(block + at->textures);
    objects->views = (VkImageView*)(block + at->views);
    objects->samplers = (VkSampler*)(block + at->samplers);
    uint32_t* slots = (uint32_t*)(block + at->slots);
    mrhiVulkanSlotsInit(&objects->bufferSlots, slots, limits->buffers);
    slots += limits->buffers;
    mrhiVulkanSlotsInit(&objects->textureSlots, slots, limits->textures);
    slots += limits->textures;
    mrhiVulkanSlotsInit(&objects->viewSlots, slots, limits->views);
    slots += limits->views;
    mrhiVulkanSlotsInit(&objects->samplerSlots, slots, limits->samplers);
}

mrhiResult mrhiCreateVulkanDevice(const mrhiAllocator* allocator, const mrhiVulkan* vulkan,
                                  VkPhysicalDevice physical, const mrhiDeviceDef* def,
                                  mrhiDeviceDriver* deviceOut)
{
    Layout at = LayoutOf(&def->deviceLimits);
    VulkanDevice* device = at.layout.overflow
                               ? nullptr
                               : mrhiAllocate(allocator, at.layout.size, alignof(VulkanDevice));
    if (device == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *device = (VulkanDevice){
        .allocator = *allocator,
        .vulkan = vulkan,
        .physical = physical,
        .bytes = at.layout.size,
    };
    VkPhysicalDeviceMemoryProperties properties;
    ReadPhysical(device, &properties);
    VkResult result = Open(device, def);
    if (result != VK_SUCCESS)
    {
        mrhiRelease(allocator, device, at.layout.size, alignof(VulkanDevice));
        mrhiResult status = StatusOf(result);
        return status == mrhi_errorDeviceLost ? mrhi_errorPlatform : status;
    }
    PlaceTables(device, &at, &def->deviceLimits, &properties);
    *deviceOut = (mrhiDeviceDriver){.vtable = &s_vtable, .self = device};
    return mrhi_success;
}
