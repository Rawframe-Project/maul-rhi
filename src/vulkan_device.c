// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device (mrhi-0003): opened with the floor's features and the
// granted ones, one queue, and a timeline semaphore for the frames. The
// memory a declared resource takes is read from the create info alone
// (Vulkan 1.3's device memory requirements). Objects, frames and
// surfaces are refused as unsupported until their slices land.

#include "vulkan_device.h"

#include "allocator.h"
#include "invariant.h"
#include "vulkan_adapter.h"
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

static void Destroy(void* self)
{
    VulkanDevice* device = self;
    // Nothing runs once a device is destroyed; a lost device answers at
    // once.
    (void)device->api.vkDeviceWaitIdle(device->device);
    device->api.vkDestroySemaphore(device->device, device->timeline, nullptr);
    device->api.vkDestroyDevice(device->device, nullptr);
    mrhiAllocator allocator = device->allocator;
    mrhiRelease(&allocator, device, sizeof(VulkanDevice), alignof(VulkanDevice));
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
static mrhiResult RefuseSampler(void* self, const mrhiSamplerDef* def, uint64_t* handleOut)
{
    (void)self;
    (void)def;
    (void)handleOut;
    return mrhi_errorUnsupported;
}

static mrhiResult RefuseBuffer(void* self, const mrhiBufferDef* def, uint64_t* handleOut)
{
    (void)self;
    (void)def;
    (void)handleOut;
    return mrhi_errorUnsupported;
}

static mrhiResult RefuseTexture(void* self, const mrhiTextureDef* def, uint64_t* handleOut)
{
    (void)self;
    (void)def;
    (void)handleOut;
    return mrhi_errorUnsupported;
}

static mrhiResult RefuseView(void* self, uint64_t texture, const mrhiViewDef* def,
                             uint64_t* handleOut)
{
    (void)self;
    (void)texture;
    (void)def;
    (void)handleOut;
    return mrhi_errorUnsupported;
}

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
    .createSampler = RefuseSampler,
    .destroySampler = NeverDestroyed,
    .createBuffer = RefuseBuffer,
    .destroyBuffer = NeverDestroyed,
    .createTexture = RefuseTexture,
    .destroyTexture = NeverDestroyed,
    .createView = RefuseView,
    .destroyView = NeverDestroyed,
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
static void ReadPhysical(VulkanDevice* device)
{
    const mrhiVulkan* vulkan = device->vulkan;
    VkPhysicalDeviceProperties2 properties = {.sType =
                                                  VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    vulkan->vkGetPhysicalDeviceProperties2(device->physical, &properties);
    device->timestampPeriod = (double)properties.properties.limits.timestampPeriod;
    device->granularity = properties.properties.limits.bufferImageGranularity;
    VkPhysicalDeviceMemoryProperties memory = {0};
    vulkan->vkGetPhysicalDeviceMemoryProperties(device->physical, &memory);
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
    {
        bool lazy =
            (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT) != 0;
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

mrhiResult mrhiCreateVulkanDevice(const mrhiAllocator* allocator, const mrhiVulkan* vulkan,
                                  VkPhysicalDevice physical, const mrhiDeviceDef* def,
                                  mrhiDeviceDriver* deviceOut)
{
    VulkanDevice* device = mrhiAllocate(allocator, sizeof(VulkanDevice), alignof(VulkanDevice));
    if (device == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *device = (VulkanDevice){.allocator = *allocator, .vulkan = vulkan, .physical = physical};
    ReadPhysical(device);
    VkResult result = Open(device, def);
    if (result != VK_SUCCESS)
    {
        mrhiRelease(allocator, device, sizeof(VulkanDevice), alignof(VulkanDevice));
        mrhiResult status = StatusOf(result);
        return status == mrhi_errorDeviceLost ? mrhi_errorPlatform : status;
    }
    *deviceOut = (mrhiDeviceDriver){.vtable = &s_vtable, .self = device};
    return mrhi_success;
}
