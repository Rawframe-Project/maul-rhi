// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Vulkan objects made outside the library (maul-rhi/vulkan.h), as an
// OpenXR runtime makes them: an instance the test makes and the library
// adopts, a device the test makes from the library's description and the
// library adopts, both still alive after the library's end; extra
// extensions on instances and devices the library makes; and the
// refusals. Skipped where there is no Vulkan 1.3 adapter, unless
// MAUL_RHI_REQUIRE_VULKAN is set.

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "device_core.h"
#include "test_harness.h"
#include "vulkan_api.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/test.h"
#include "maul-rhi/vulkan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The functions the test calls, read from the loader.
static mrhiVulkan s_vulkan;
// The functions of the instance the test made, and that instance.
static mrhiVulkan s_made;
static VkInstance s_instance;
// Whether the test's instances enable VK_KHR_surface, which the loader
// offers wherever a driver presents.
static bool s_surface;

// The allocations a device's allocator holds.
static int s_deviceAllocations;

// Memory with its offset into the block before it, so that any
// alignment is served on every platform.
static void* CountingAlloc(size_t size, size_t alignment, void* context)
{
    (void)context;
    size_t head = alignment > sizeof(size_t) ? alignment : sizeof(size_t);
    unsigned char* block = malloc(size + head + alignment);
    if (block == nullptr)
    {
        return nullptr;
    }
    uintptr_t at = ((uintptr_t)block + head + alignment - 1) & ~(uintptr_t)(alignment - 1);
    size_t offset = (size_t)(at - (uintptr_t)block);
    memcpy((unsigned char*)at - sizeof(size_t), &offset, sizeof(size_t));
    ++s_deviceAllocations;
    return (void*)at;
}

static void CountingFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    if (memory == nullptr)
    {
        return;
    }
    size_t offset = 0;
    memcpy(&offset, (unsigned char*)memory - sizeof(size_t), sizeof(size_t));
    --s_deviceAllocations;
    free((unsigned char*)memory - offset);
}

static mrhiInstance* Create(const mrhiChain* chain, mrhiResult* statusOut)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = chain;
    mrhiInstance* instance = nullptr;
    *statusOut = mrhiCreateInstance(&def, &instance);
    return instance;
}

// The first adapter a search finds, software ones included.
static bool FirstAdapter(mrhiInstance* instance, mrhiAdapterId* adapterOut)
{
    mrhiAdapterRequestDef request = mrhiDefaultAdapterRequestDef();
    request.allowSoftware = true;
    mrhiRequestId id;
    mrhiInstanceNotification record;
    size_t count = 0;
    return mrhiRequestAdapters(instance, &request, &id) == mrhi_success &&
           mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
           mrhiGetAdapters(instance, adapterOut, 1, &count) == mrhi_success && count >= 1;
}

// Opens a device for a def and takes its readiness.
static mrhiDevice* Open(mrhiInstance* instance, const mrhiDeviceDef* def, mrhiResult* statusOut)
{
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    *statusOut = mrhiCreateDevice(instance, def, &device, &request);
    mrhiInstanceNotification record;
    if (device != nullptr)
    {
        CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
                  record.kind == mrhi_instanceDeviceReady && record.outcome == mrhi_success,
              "the device ready");
    }
    return device;
}

static mrhiAccess Whole(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
}

// Bytes uploaded into a buffer and read back: the device runs work.
static void CheckRuns(mrhiDevice* device)
{
    uint8_t pattern[256];
    for (size_t i = 0; i < sizeof(pattern); ++i)
    {
        pattern[i] = (uint8_t)(i * 5 + 1);
    }
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = sizeof(pattern);
    mrhiResourceId buffer = {0};
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
              mrhiDeclareBuffer(device, &bufferDef, &buffer) == mrhi_success,
          "a frame and a buffer");
    mrhiPassDef passDef = mrhiDefaultPassDef();
    passDef.neverCull = true;
    passDef.accessCount = 1;
    mrhiAccess write = Whole(buffer, mrhi_accessCopyDestination);
    mrhiAccess read = Whole(buffer, mrhi_accessCopySource);
    mrhiPassId upload = {0};
    mrhiPassId back = {0};
    passDef.accesses = &write;
    CHECK(mrhiAddPass(device, &passDef, &upload) == mrhi_success, "the upload");
    passDef.accesses = &read;
    CHECK(mrhiAddPass(device, &passDef, &back) == mrhi_success, "the readback");
    mrhiRequestId bytes = {0};
    mrhiRequestId token = {0};
    CHECK(
        mrhiCompileFrame(device) == mrhi_success && mrhiBeginPass(device, upload) == mrhi_success &&
            mrhiWriteBuffer(device, upload, buffer, 0, pattern, sizeof(pattern)) == mrhi_success &&
            mrhiEndPass(device, upload) == mrhi_success &&
            mrhiBeginPass(device, back) == mrhi_success &&
            mrhiReadBuffer(device, back, buffer, 0, sizeof(pattern), &bytes) == mrhi_success &&
            mrhiEndPass(device, back) == mrhi_success &&
            mrhiSubmitFrame(device, &token) == mrhi_success &&
            mrhiWaitFrame(device, token, UINT64_C(10000000000)) == mrhi_success,
        "recorded and finished");
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(device, &record) == mrhi_success)
    {
    }
    uint8_t taken[sizeof(pattern)] = {0};
    size_t size = 0;
    CHECK(mrhiTakeReadback(device, bytes, taken, sizeof(taken), &size) == mrhi_success &&
              size == sizeof(pattern) && memcmp(taken, pattern, size) == 0,
          "the bytes back");
}

// The functions of the device the test made.
static mrhiVulkanDevice s_device;

// Moves an image between layouts on the device's queue, as the OpenXR
// runtime does around the program's frames, and waits.
static void Transition(VkDevice device, uint32_t family, VkImage image, VkImageLayout from,
                       VkImageLayout to)
{
    const VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family,
    };
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    const VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT,
        .oldLayout = from,
        .newLayout = to,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    const VkDependencyInfo dependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    VkQueue queue = VK_NULL_HANDLE;
    s_device.vkGetDeviceQueue(device, family, 0, &queue);
    bool done = s_device.vkCreateCommandPool(device, &poolInfo, nullptr, &pool) == VK_SUCCESS;
    const VkCommandBufferAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    done = done && s_device.vkAllocateCommandBuffers(device, &allocate, &commands) == VK_SUCCESS &&
           s_device.vkBeginCommandBuffer(commands, &begin) == VK_SUCCESS;
    if (done)
    {
        s_device.vkCmdPipelineBarrier2(commands, &dependency);
    }
    const VkCommandBufferSubmitInfo commandInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = commands,
    };
    const VkSubmitInfo2 submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandInfo,
    };
    done = done && s_device.vkEndCommandBuffer(commands) == VK_SUCCESS &&
           s_device.vkQueueSubmit2(queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS &&
           s_device.vkQueueWaitIdle(queue) == VK_SUCCESS;
    CHECK(done, "the image moved between layouts");
    if (pool != VK_NULL_HANDLE)
    {
        s_device.vkDestroyCommandPool(device, pool, nullptr);
    }
}

// An image as an OpenXR runtime makes one for a swapchain: 16 by 16,
// RGBA8, a color target, a copy source and sampled, bound to device memory and
// in COLOR_ATTACHMENT_OPTIMAL.
static VkImage MakeImage(VkDevice device, VkPhysicalDevice physical, uint32_t family,
                         VkDeviceMemory* memoryOut)
{
    const VkImageCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .extent = {16, 16, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    VkImage image = VK_NULL_HANDLE;
    *memoryOut = VK_NULL_HANDLE;
    if (s_device.vkCreateImage(device, &info, nullptr, &image) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    const VkImageMemoryRequirementsInfo2 asked = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
        .image = image,
    };
    VkMemoryRequirements2 needs = {.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};
    s_device.vkGetImageMemoryRequirements2(device, &asked, &needs);
    VkPhysicalDeviceMemoryProperties properties;
    s_made.vkGetPhysicalDeviceMemoryProperties(physical, &properties);
    uint32_t type = 0;
    while (type < properties.memoryTypeCount &&
           (needs.memoryRequirements.memoryTypeBits >> type & 1u) == 0)
    {
        ++type;
    }
    const VkMemoryAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = needs.memoryRequirements.size,
        .memoryTypeIndex = type,
    };
    if (s_device.vkAllocateMemory(device, &allocate, nullptr, memoryOut) != VK_SUCCESS ||
        s_device.vkBindImageMemory(device, image, *memoryOut, 0) != VK_SUCCESS)
    {
        s_device.vkDestroyImage(device, image, nullptr);
        return VK_NULL_HANDLE;
    }
    Transition(device, family, image, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    return image;
}

// One frame clearing the adopted texture and reading it back: every
// texel the clear's.
static void ClearAndRead(mrhiDevice* device, mrhiTextureId texture, float green)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId target = {0};
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
              mrhiImportTexture(device, texture, &target) == mrhi_success,
          "a frame with the image");
    mrhiPassDef clearDef = mrhiDefaultPassDef();
    clearDef.colorTargets[0] = (mrhiColorTarget){
        .resource = target,
        .load = mrhi_loadClear,
        .store = mrhi_storeKeep,
        .clear = {0.0f, green, 1.0f, 1.0f},
    };
    clearDef.colorTargetCount = 1;
    mrhiPassDef readDef = mrhiDefaultPassDef();
    mrhiAccess read = Whole(target, mrhi_accessCopySource);
    readDef.accesses = &read;
    readDef.accessCount = 1;
    readDef.neverCull = true;
    mrhiPassId clear = {0};
    mrhiPassId back = {0};
    const mrhiTextureCopy whole = {.resource = target};
    const mrhiExtent3d extent = {16, 16, 1};
    mrhiRequestId texels = {0};
    mrhiRequestId token = {0};
    CHECK(mrhiAddPass(device, &clearDef, &clear) == mrhi_success &&
              mrhiAddPass(device, &readDef, &back) == mrhi_success &&
              mrhiCompileFrame(device) == mrhi_success &&
              mrhiBeginPass(device, clear) == mrhi_success &&
              mrhiEndPass(device, clear) == mrhi_success &&
              mrhiBeginPass(device, back) == mrhi_success &&
              mrhiReadTexture(device, back, &whole, &extent, &texels) == mrhi_success &&
              mrhiEndPass(device, back) == mrhi_success &&
              mrhiSubmitFrame(device, &token) == mrhi_success &&
              mrhiWaitFrame(device, token, UINT64_C(10000000000)) == mrhi_success,
          "cleared and read");
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(device, &record) == mrhi_success)
    {
    }
    uint8_t taken[16 * 16 * 4] = {0};
    size_t size = 0;
    bool same = mrhiTakeReadback(device, texels, taken, sizeof(taken), &size) == mrhi_success &&
                size == sizeof(taken);
    uint8_t expected = green > 0.5f ? 255 : 0;
    for (size_t i = 0; same && i < sizeof(taken); i += 4)
    {
        same =
            taken[i] == 0 && taken[i + 1] == expected && taken[i + 2] == 255 && taken[i + 3] == 255;
    }
    CHECK(same, "the clear's texels back");
}

// A swapchain image adopted as a texture: refused when malformed,
// rendered to and read in frames that each end it a color target, as
// the runtime takes it back, and alive after the texture's end.
static void CheckImageAdoption(mrhiDevice* device, VkDevice made, VkPhysicalDevice physical,
                               uint32_t family, bool sealing)
{
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImage image = MakeImage(made, physical, family, &memory);
    CHECK(image != VK_NULL_HANDLE, "an image made");
    if (image == VK_NULL_HANDLE)
    {
        return;
    }
    mrhiTextureVulkanAdopt adopt = {
        .chain = {.type = mrhi_structTextureVulkanAdopt},
        .image = (void*)image,
    };
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.next = &adopt.chain;
    def.format = mrhi_formatRgba8Unorm;
    def.width = 16;
    def.height = 16;
    def.usage = mrhi_textureCopySource | mrhi_textureSampled;
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(device, &def, &texture) == mrhi_errorInvalid,
          "an image that is no render target refused");
    def.usage |= mrhi_textureRenderTarget;
    adopt.image = nullptr;
    CHECK(mrhiCreateTexture(device, &def, &texture) == mrhi_errorInvalid, "no image refused");
    adopt.image = (void*)image;
    CHECK(mrhiCreateTexture(device, &def, &texture) == mrhi_success, "the image adopted");
    CHECK(device->textureSlots[texture.index1 - 1].state == mrhi_stateColorTarget,
          "taken as the color target it is handed over as");
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId resource = {0};
    mrhiRequestId token = {0};
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
              mrhiImportTexture(device, texture, &resource) == mrhi_success,
          "a frame importing it");
    CHECK(mrhiSealResource(device, resource) ==
              (sealing ? mrhi_errorInvalid : mrhi_errorUnsupported),
          "never sealed");
    CHECK(mrhiCompileFrame(device) == mrhi_success &&
              mrhiSubmitFrame(device, &token) == mrhi_success &&
              mrhiWaitFrame(device, token, UINT64_C(10000000000)) == mrhi_success,
          "an empty frame");
    ClearAndRead(device, texture, 1.0f);
    // The frame ended it a color target, as the runtime takes it back:
    // the validation layer checks the runtime's barrier from that layout.
    Transition(made, family, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    ClearAndRead(device, texture, 0.0f);
    CHECK(mrhiDestroyTexture(device, texture) == mrhi_success, "the texture ended");
    // The image and its memory are still the program's.
    CHECK(s_device.vkDeviceWaitIdle(made) == VK_SUCCESS, "idle");
    Transition(made, family, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    s_device.vkDestroyImage(made, image, nullptr);
    s_device.vkFreeMemory(made, memory, nullptr);
}

// A native pass between two of the library's: a texture uploaded,
// cleared by the program's own command buffer, and read back, the
// clear's texels coming back; the native objects the device borrows
// out; and the refusals.
static void CheckNativePass(mrhiDevice* device, VkDevice made, VkInstance instance,
                            VkPhysicalDevice physical, uint32_t family)
{
    void* natives[5] = {0};
    CHECK(mrhiGetVulkanDevice(device, &natives[0], &natives[1], &natives[2], &natives[3],
                              &natives[4]) == mrhi_success &&
              natives[0] == (void*)instance && natives[1] == (void*)physical &&
              natives[2] == (void*)made && natives[3] != nullptr && natives[4] != nullptr,
          "the device's native objects");
    PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
    memcpy((void*)&getDeviceProcAddr, (const void*)&natives[4], sizeof(getDeviceProcAddr));
    PFN_vkCmdClearColorImage clearImage = nullptr;
    PFN_vkVoidFunction found = getDeviceProcAddr(made, "vkCmdClearColorImage");
    memcpy((void*)&clearImage, (const void*)&found, sizeof(clearImage));
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = 16;
    def.height = 16;
    def.usage = mrhi_textureCopySource | mrhi_textureCopyDestination;
    mrhiTextureId texture = {0};
    mrhiVulkanTextureInfo info = {0};
    CHECK(mrhiCreateTexture(device, &def, &texture) == mrhi_success &&
              mrhiGetVulkanTexture(device, texture, &info) == mrhi_success &&
              info.image != nullptr && info.memory != nullptr &&
              info.format == VK_FORMAT_R8G8B8A8_UNORM &&
              (info.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0,
          "the texture's image");
    // The program's command buffer: a clear, in the layout a copy
    // destination is in.
    const VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family,
    };
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    bool recorded = s_device.vkCreateCommandPool(made, &poolInfo, nullptr, &pool) == VK_SUCCESS;
    const VkCommandBufferAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    recorded = recorded &&
               s_device.vkAllocateCommandBuffers(made, &allocate, &commands) == VK_SUCCESS &&
               s_device.vkBeginCommandBuffer(commands, &begin) == VK_SUCCESS;
    const VkClearColorValue color = {.float32 = {0.0f, 1.0f, 0.0f, 1.0f}};
    const VkImageSubresourceRange range = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (recorded && clearImage != nullptr)
    {
        clearImage(commands, (VkImage)info.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &color, 1,
                   &range);
    }
    recorded =
        recorded && clearImage != nullptr && s_device.vkEndCommandBuffer(commands) == VK_SUCCESS;
    CHECK(recorded, "the program's command buffer");
    // The frame: an upload, the native clear, a readback.
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId resource = {0};
    mrhiResourceId declared = {0};
    mrhiBufferDef declaredDef = mrhiDefaultBufferDef();
    declaredDef.size = 256;
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
              mrhiImportTexture(device, texture, &resource) == mrhi_success &&
              mrhiDeclareBuffer(device, &declaredDef, &declared) == mrhi_success,
          "a frame with the texture");
    mrhiAccess write = Whole(resource, mrhi_accessCopyDestination);
    mrhiAccess read = Whole(resource, mrhi_accessCopySource);
    mrhiPassDef passDef = mrhiDefaultPassDef();
    passDef.neverCull = true;
    passDef.accessCount = 1;
    passDef.accesses = &write;
    mrhiPassId upload = {0};
    mrhiPassId native = {0};
    mrhiPassId back = {0};
    mrhiPassId refused = {0};
    CHECK(mrhiAddPass(device, &passDef, &upload) == mrhi_success, "the upload");
    passDef.native = true;
    mrhiAccess onDeclared = Whole(declared, mrhi_accessCopyDestination);
    passDef.accesses = &onDeclared;
    CHECK(mrhiAddPass(device, &passDef, &refused) == mrhi_errorInvalid,
          "a native pass on a declared resource refused");
    passDef.accesses = &write;
    passDef.colorTargetCount = 1;
    passDef.colorTargets[0] = (mrhiColorTarget){.resource = resource};
    CHECK(mrhiAddPass(device, &passDef, &refused) == mrhi_errorInvalid,
          "a native pass with a target refused");
    passDef.colorTargetCount = 0;
    CHECK(mrhiAddPass(device, &passDef, &native) == mrhi_success, "the native pass");
    passDef.native = false;
    passDef.accesses = &read;
    CHECK(mrhiAddPass(device, &passDef, &back) == mrhi_success, "the readback");
    uint8_t texels[16 * 16 * 4];
    memset(texels, 0x40, sizeof(texels));
    const mrhiTextureCopy whole = {.resource = resource};
    const mrhiTexelLayout layout = {.bytesPerRow = 64, .rowsPerImage = 16};
    const mrhiExtent3d extent = {16, 16, 1};
    mrhiRequestId bytes = {0};
    mrhiRequestId token = {0};
    CHECK(mrhiCompileFrame(device) == mrhi_success &&
              mrhiBeginPass(device, upload) == mrhi_success &&
              mrhiWriteTexture(device, upload, &whole, texels, sizeof(texels), &layout, &extent) ==
                  mrhi_success &&
              mrhiEndPass(device, upload) == mrhi_success,
          "uploaded");
    CHECK(mrhiSetVulkanPassCommands(device, native, commands) == mrhi_errorState,
          "no command buffer before the pass begins");
    CHECK(mrhiBeginPass(device, native) == mrhi_success &&
              mrhiWriteTexture(device, native, &whole, texels, sizeof(texels), &layout, &extent) ==
                  mrhi_errorInvalid &&
              mrhiSetVulkanPassCommands(device, native, nullptr) == mrhi_errorInvalid &&
              mrhiSetVulkanPassCommands(device, native, commands) == mrhi_success &&
              mrhiSetVulkanPassCommands(device, native, commands) == mrhi_errorState &&
              mrhiEndPass(device, native) == mrhi_success,
          "the native pass given its commands, and no encoder call");
    CHECK(mrhiBeginPass(device, back) == mrhi_success &&
              mrhiSetVulkanPassCommands(device, back, commands) == mrhi_errorInvalid &&
              mrhiReadTexture(device, back, &whole, &extent, &bytes) == mrhi_success &&
              mrhiEndPass(device, back) == mrhi_success &&
              mrhiSubmitFrame(device, &token) == mrhi_success &&
              mrhiWaitFrame(device, token, UINT64_C(10000000000)) == mrhi_success,
          "read back and finished");
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(device, &record) == mrhi_success)
    {
    }
    uint8_t taken[sizeof(texels)] = {0};
    size_t size = 0;
    bool cleared = mrhiTakeReadback(device, bytes, taken, sizeof(taken), &size) == mrhi_success &&
                   size == sizeof(taken);
    for (size_t i = 0; cleared && i < sizeof(taken); i += 4)
    {
        cleared = taken[i] == 0 && taken[i + 1] == 255 && taken[i + 2] == 0 && taken[i + 3] == 255;
    }
    CHECK(cleared, "the program's clear, after the upload and before the readback");
    if (pool != VK_NULL_HANDLE)
    {
        s_device.vkDestroyCommandPool(made, pool, nullptr);
    }
    CHECK(mrhiDestroyTexture(device, texture) == mrhi_success, "the texture ended");
    // A frame holds MRHI_NATIVE_PASSES of them.
    mrhiPassDef nativeDef = mrhiDefaultPassDef();
    nativeDef.native = true;
    bool added = mrhiBeginFrame(device, &frame) == mrhi_success;
    for (uint32_t i = 0; i < MRHI_NATIVE_PASSES && added; ++i)
    {
        added = mrhiAddPass(device, &nativeDef, &refused) == mrhi_success;
    }
    CHECK(added && mrhiAddPass(device, &nativeDef, &refused) == mrhi_errorCapacity &&
              mrhiDropFrame(device) == mrhi_success,
          "native passes up to their limit");
}

// Whether a create info's features hold the merged ones beside the
// library's own: clip distances, mirror clamping and timelines.
static bool Features(const VkDeviceCreateInfo* info, const VkPhysicalDeviceVulkan12Features* asked)
{
    bool clip = false;
    bool mirror = false;
    bool timeline = false;
    for (const VkBaseInStructure* node = info->pNext; node != nullptr; node = node->pNext)
    {
        if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2)
        {
            clip =
                ((const VkPhysicalDeviceFeatures2*)(const void*)node)->features.shaderClipDistance;
        }
        if (node->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES &&
            (const void*)node != (const void*)asked)
        {
            const VkPhysicalDeviceVulkan12Features* features = (const void*)node;
            mirror = features->samplerMirrorClampToEdge;
            timeline = features->timelineSemaphore;
        }
    }
    return clip && mirror && timeline;
}

// Whether a create info names an extension, and how often.
static int Named(const VkDeviceCreateInfo* info, const char* name)
{
    int found = 0;
    for (uint32_t i = 0; i < info->enabledExtensionCount; ++i)
    {
        found += strcmp(info->ppEnabledExtensionNames[i], name) == 0 ? 1 : 0;
    }
    return found;
}

// An instance the test makes: Vulkan 1.3, with VK_KHR_surface where
// the loader offers it.
static VkInstance MakeInstance(void)
{
    const VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_3,
    };
    const char* const surface = VK_KHR_SURFACE_EXTENSION_NAME;
    const VkInstanceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
        .enabledExtensionCount = s_surface ? 1 : 0,
        .ppEnabledExtensionNames = &surface,
    };
    VkInstance instance = VK_NULL_HANDLE;
    return s_vulkan.vkCreateInstance(&info, nullptr, &instance) == VK_SUCCESS ? instance
                                                                              : VK_NULL_HANDLE;
}

static mrhiInstanceVulkanAdopt AdoptOf(VkInstance instance)
{
    static const char surface[] = VK_KHR_SURFACE_EXTENSION_NAME;
    mrhiInstanceVulkanAdopt adopt = {
        .chain = {.type = mrhi_structInstanceVulkanAdopt},
        .instance = (void*)instance,
        .apiVersion = VK_API_VERSION_1_3,
        .extensions = s_surface ? surface : nullptr,
        .extensionsLength = s_surface ? sizeof(surface) : 0,
    };
    static_assert(sizeof(adopt.getInstanceProcAddr) == sizeof(s_vulkan.vkGetInstanceProcAddr),
                  "function pointers");
    memcpy((void*)&adopt.getInstanceProcAddr, (const void*)&s_vulkan.vkGetInstanceProcAddr,
           sizeof(adopt.getInstanceProcAddr));
    return adopt;
}

// A device made from a description and adopted, then the refusals of
// devices that do not match their description.
static void CheckDeviceAdoption(mrhiInstance* instance, mrhiAdapterId adapter)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    // Sealing, which adopted images refuse, where the adapter grants it.
    mrhiFeatures granted = {0};
    CHECK(mrhiGetAdapterFeatures(instance, adapter, &granted) == mrhi_success, "the features");
    def.features.bindlessSampling = granted.bindlessSampling;
    mrhiDeviceVulkanAdopt adopt = {.chain = {.type = mrhi_structDeviceVulkanAdopt}};
    mrhiDeviceDef adopting = def;
    adopting.next = &adopt.chain;
    mrhiResult status = mrhi_success;
    adopt.device = (void*)(uintptr_t)1;
    CHECK(Open(instance, &adopting, &status) == nullptr && status == mrhi_errorInvalid,
          "no device adopted before a description");
    void* info = nullptr;
    void* physical = nullptr;
    CHECK(mrhiDescribeVulkanDevice(instance, &def, &info, &physical) == mrhi_success &&
              info != nullptr && physical != nullptr,
          "a description");
    void* listed = nullptr;
    CHECK(mrhiGetVulkanPhysicalDevice(instance, adapter, &listed) == mrhi_success &&
              listed == physical,
          "the adapter's physical device");
    const VkDeviceCreateInfo* created = info;
    CHECK(created->sType == VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO &&
              created->queueCreateInfoCount == 1 && created->pNext != nullptr,
          "one queue and the features");
    // The runtime makes the device from the description.
    VkDevice made = VK_NULL_HANDLE;
    CHECK(s_made.vkCreateDevice((VkPhysicalDevice)physical, created, nullptr, &made) == VK_SUCCESS,
          "the device made from it");
    adopt.device = (void*)made;
    static const char maintenance[] = VK_KHR_MAINTENANCE_1_EXTENSION_NAME;
    mrhiDeviceVulkanExtensions extensions = {
        .chain = {.next = &adopt.chain, .type = mrhi_structDeviceVulkanExtensions},
        .extensions = maintenance,
        .extensionsLength = sizeof(maintenance),
    };
    mrhiDeviceDef other = adopting;
    other.next = &extensions.chain;
    CHECK(Open(instance, &other, &status) == nullptr && status == mrhi_errorInvalid,
          "no device adopted for other extensions");
    other.next = &adopt.chain;
    other.adapter.generation += 1;
    CHECK(Open(instance, &other, &status) == nullptr && status == mrhi_errorStale,
          "no device adopted on a stale adapter");
    mrhiDevice* device = Open(instance, &adopting, &status);
    CHECK(device != nullptr && status == mrhi_success, "the device adopted");
    if (device == nullptr)
    {
        return;
    }
    uint32_t family = UINT32_MAX;
    uint32_t index = UINT32_MAX;
    CHECK(mrhiGetVulkanQueue(device, &family, &index) == mrhi_success &&
              family == created->pQueueCreateInfos[0].queueFamilyIndex && index == 0,
          "the queue the session binds");
    CheckRuns(device);
    CHECK(mrhiLoadVulkanDevice(&s_made, made, false, &s_device), "the device's functions");
    CheckNativePass(device, made, s_instance, (VkPhysicalDevice)physical, family);
    CheckImageAdoption(device, made, (VkPhysicalDevice)physical, family,
                       def.features.bindlessSampling);
    mrhiDestroyDevice(device);
    // The device outlives the library's hold of it.
    CHECK(s_device.vkDeviceWaitIdle(made) == VK_SUCCESS, "the adopted device still alive");
    s_device.vkDestroyDevice(made, nullptr);
    // A later description replaces the earlier one.
    CHECK(mrhiDescribeVulkanDevice(instance, &def, &info, &physical) == mrhi_success,
          "described again");
    mrhiDeviceDef changed = def;
    changed.next = &extensions.chain;
    extensions.chain.next = nullptr;
    CHECK(mrhiDescribeVulkanDevice(instance, &changed, &info, &physical) == mrhi_success,
          "described with an extension");
    adopt.device = (void*)(uintptr_t)1;
    CHECK(Open(instance, &adopting, &status) == nullptr && status == mrhi_errorInvalid,
          "the earlier description gone");
    // A refused description ends the earlier one too.
    CHECK(mrhiDescribeVulkanDevice(instance, &def, &info, &physical) == mrhi_success,
          "described once more");
    static const char unended[] = {'a'};
    extensions.extensions = unended;
    extensions.extensionsLength = sizeof(unended);
    CHECK(mrhiDescribeVulkanDevice(instance, &changed, &info, &physical) == mrhi_errorInvalid,
          "a description refused");
    CHECK(Open(instance, &adopting, &status) == nullptr && status == mrhi_errorInvalid,
          "the earlier description gone with the refusal");
}

// Extra device extensions: copied at the call, named once, enabled on a
// device the library makes, refused when malformed.
static void CheckDeviceExtensions(mrhiInstance* instance, mrhiAdapterId adapter)
{
    // The swapchain, which the library enables itself where the instance
    // has surfaces, and an extension it does not.
    char names[] = VK_KHR_SWAPCHAIN_EXTENSION_NAME "\0" VK_KHR_MAINTENANCE_1_EXTENSION_NAME;
    size_t maintenanceAt = sizeof(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    mrhiDeviceVulkanExtensions extensions = {
        .chain = {.type = mrhi_structDeviceVulkanExtensions},
        .extensions = s_surface ? names : names + maintenanceAt,
        .extensionsLength = s_surface ? sizeof(names) : sizeof(names) - maintenanceAt,
    };
    // Features an upscaler asks for, merged into the library's own.
    VkPhysicalDeviceVulkan12Features wanted12 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .samplerMirrorClampToEdge = VK_TRUE,
    };
    VkPhysicalDeviceFeatures2 wanted = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &wanted12,
        .features = {.shaderClipDistance = VK_TRUE},
    };
    extensions.features = &wanted;
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    def.next = &extensions.chain;
    def.allocator = (mrhiAllocator){CountingAlloc, CountingFree, nullptr};
    void* info = nullptr;
    void* physical = nullptr;
    CHECK(mrhiDescribeVulkanDevice(instance, &def, &info, &physical) == mrhi_success,
          "described with extensions");
    CHECK(Features(info, &wanted12), "the features merged into the library's");
    memset(names, 'x', sizeof(names) - 1);
    const VkDeviceCreateInfo* created = info;
    CHECK(Named(created, VK_KHR_MAINTENANCE_1_EXTENSION_NAME) == 1 &&
              Named(created, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == (s_surface ? 1 : 0),
          "copied at the call, each named once");
    memcpy(names, VK_KHR_SWAPCHAIN_EXTENSION_NAME "\0" VK_KHR_MAINTENANCE_1_EXTENSION_NAME,
           sizeof(names));
    mrhiResult status = mrhi_success;
    mrhiDevice* device = Open(instance, &def, &status);
    CHECK(device != nullptr && status == mrhi_success, "a device with them");
    CHECK(s_deviceAllocations >= 2, "the device's memory, the core's and the driver's, its own");
    if (device != nullptr)
    {
        CheckRuns(device);
        mrhiDestroyDevice(device);
    }
    // A feature struct the library cannot place is refused.
    const VkPhysicalDeviceRobustness2FeaturesEXT unknown = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT,
    };
    extensions.features = &unknown;
    CHECK(mrhiDescribeVulkanDevice(instance, &def, &info, &physical) == mrhi_errorUnsupported &&
              Open(instance, &def, &status) == nullptr && status == mrhi_errorUnsupported,
          "an unknown feature struct refused");
    extensions.features = nullptr;
    static const char unended[] = {'a', 'b'};
    static const char empty[] = {'a', 0, 0};
    extensions.extensions = unended;
    extensions.extensionsLength = sizeof(unended);
    CHECK(Open(instance, &def, &status) == nullptr && status == mrhi_errorInvalid,
          "a name not ended refused");
    extensions.extensions = empty;
    extensions.extensionsLength = sizeof(empty);
    CHECK(Open(instance, &def, &status) == nullptr && status == mrhi_errorInvalid,
          "an empty name refused");
    extensions.extensions = nullptr;
    extensions.extensionsLength = 1;
    CHECK(mrhiDescribeVulkanDevice(instance, &def, &info, &physical) == mrhi_errorInvalid,
          "no names with bytes refused");
}

// An instance the test makes, adopted, used, and alive after the
// library's end.
static void CheckInstanceAdoption(void)
{
    VkInstance made = MakeInstance();
    CHECK(made != VK_NULL_HANDLE, "an instance made");
    if (made == VK_NULL_HANDLE)
    {
        return;
    }
    s_made = s_vulkan;
    s_instance = made;
    CHECK(mrhiLoadVulkanInstance(&s_made, made), "its functions");
    mrhiInstanceVulkanAdopt adopt = AdoptOf(made);
    mrhiResult status = mrhi_success;
    mrhiInstance* instance = Create(&adopt.chain, &status);
    mrhiAdapterId adapter = {0};
    CHECK(instance != nullptr && FirstAdapter(instance, &adapter), "adopted, with an adapter");
    if (instance != nullptr)
    {
        CheckDeviceAdoption(instance, adapter);
        CheckDeviceExtensions(instance, adapter);
        mrhiDestroyInstance(instance);
    }
    uint32_t count = 0;
    CHECK(s_made.vkEnumeratePhysicalDevices(made, &count, nullptr) == VK_SUCCESS && count > 0,
          "the adopted instance still alive");
    // Adopted as made without surfaces, its devices have no swapchain,
    // which needs them.
    adopt.extensions = nullptr;
    adopt.extensionsLength = 0;
    instance = Create(&adopt.chain, &status);
    CHECK(instance != nullptr && FirstAdapter(instance, &adapter), "adopted without surfaces");
    if (instance != nullptr)
    {
        mrhiDeviceDef def = mrhiDefaultDeviceDef();
        def.adapter = adapter;
        void* info = nullptr;
        void* physical = nullptr;
        CHECK(mrhiDescribeVulkanDevice(instance, &def, &info, &physical) == mrhi_success &&
                  Named(info, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0,
              "no swapchain without surfaces");
        mrhiDestroyInstance(instance);
    }
    adopt.apiVersion = VK_API_VERSION_1_2;
    CHECK(Create(&adopt.chain, &status) == nullptr && status == mrhi_errorUnsupported,
          "an instance older than 1.3 refused");
    s_made.vkDestroyInstance(made, nullptr);
}

// Extra instance extensions, and their refusals.
static void CheckInstanceExtensions(void)
{
    static const char known[] = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
    mrhiInstanceVulkanExtensions extensions = {
        .chain = {.type = mrhi_structInstanceVulkanExtensions},
        .extensions = known,
        .extensionsLength = sizeof(known),
    };
    mrhiResult status = mrhi_success;
    mrhiInstance* instance = Create(&extensions.chain, &status);
    mrhiAdapterId adapter = {0};
    CHECK(instance != nullptr && FirstAdapter(instance, &adapter), "made with an extension");
    mrhiDestroyInstance(instance);
    static const char unknown[] = "VK_MAUL_none";
    extensions.extensions = unknown;
    extensions.extensionsLength = sizeof(unknown);
    CHECK(Create(&extensions.chain, &status) == nullptr && status == mrhi_errorUnsupported,
          "an extension the loader lacks refused");
    static const char unended[] = {'a'};
    extensions.extensions = unended;
    extensions.extensionsLength = sizeof(unended);
    CHECK(Create(&extensions.chain, &status) == nullptr && status == mrhi_errorInvalid,
          "a name not ended refused");
    // Adopting and extending at once is malformed.
    mrhiInstanceVulkanAdopt adopt = AdoptOf((VkInstance)(uintptr_t)1);
    extensions.extensions = known;
    extensions.extensionsLength = sizeof(known);
    adopt.chain.next = &extensions.chain;
    CHECK(Create(&adopt.chain, &status) == nullptr && status == mrhi_errorInvalid,
          "adopted and extended refused");
    adopt.chain.next = nullptr;
    adopt.instance = nullptr;
    CHECK(Create(&adopt.chain, &status) == nullptr && status == mrhi_errorInvalid,
          "no instance refused");
}

// The test driver takes no Vulkan struct and answers the functions
// unsupported.
static void CheckOtherDriver(void)
{
    mrhiTestAdapter adapterDef = {
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .limits = mrhiDefaultLimits(),
    };
    mrhiTestDriverDef test = {
        .chain = {.type = mrhi_structTestDriver},
        .adapters = &adapterDef,
        .adapterCount = 1,
    };
    mrhiResult status = mrhi_success;
    mrhiInstance* instance = Create(&test.chain, &status);
    mrhiAdapterId adapter = {0};
    CHECK(instance != nullptr && FirstAdapter(instance, &adapter), "the test driver");
    if (instance == nullptr)
    {
        return;
    }
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    void* info = nullptr;
    void* physical = nullptr;
    CHECK(mrhiDescribeVulkanDevice(instance, &def, &info, &physical) == mrhi_errorUnsupported &&
              info == nullptr && physical == nullptr,
          "no description");
    CHECK(mrhiGetVulkanPhysicalDevice(instance, adapter, &physical) == mrhi_errorUnsupported,
          "no physical device");
    mrhiDeviceVulkanAdopt adopt = {.chain = {.type = mrhi_structDeviceVulkanAdopt},
                                   .device = (void*)(uintptr_t)1};
    def.next = &adopt.chain;
    CHECK(Open(instance, &def, &status) == nullptr && status == mrhi_errorUnsupported,
          "no device adopted");
    def.next = nullptr;
    mrhiDevice* device = Open(instance, &def, &status);
    uint32_t family = 0;
    uint32_t index = 0;
    CHECK(device != nullptr && mrhiGetVulkanQueue(device, &family, &index) == mrhi_errorUnsupported,
          "no queue");
    mrhiTextureVulkanAdopt image = {.chain = {.type = mrhi_structTextureVulkanAdopt},
                                    .image = (void*)(uintptr_t)1};
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.next = &image.chain;
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = 16;
    textureDef.height = 16;
    textureDef.usage = mrhi_textureRenderTarget;
    mrhiTextureId texture = {0};
    CHECK(device != nullptr &&
              mrhiCreateTexture(device, &textureDef, &texture) == mrhi_errorUnsupported,
          "no image adopted");
    void* natives[5] = {0};
    mrhiPassDef nativeDef = mrhiDefaultPassDef();
    nativeDef.native = true;
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    mrhiPassId pass = {0};
    CHECK(device != nullptr &&
              mrhiGetVulkanDevice(device, &natives[0], &natives[1], &natives[2], &natives[3],
                                  &natives[4]) == mrhi_errorUnsupported &&
              mrhiBeginFrame(device, &frameDef) == mrhi_success &&
              mrhiAddPass(device, &nativeDef, &pass) == mrhi_errorUnsupported &&
              mrhiDropFrame(device) == mrhi_success,
          "no native objects or passes");
    mrhiDestroyDevice(device);
    mrhiDestroyInstance(instance);
    mrhiInstanceVulkanExtensions extensions = {
        .chain = {.type = mrhi_structInstanceVulkanExtensions}};
    test.chain.next = &extensions.chain;
    CHECK(Create(&test.chain, &status) == nullptr && status == mrhi_errorUnsupported,
          "the test driver with a Vulkan struct refused");
}

// Whether the loader makes a Vulkan 1.3 instance with an adapter.
static bool HasVulkan(void)
{
    uint32_t version = 0;
    VkInstance instance = VK_NULL_HANDLE;
    if (!mrhiOpenVulkan(&s_vulkan) || s_vulkan.vkEnumerateInstanceVersion(&version) != VK_SUCCESS ||
        version < VK_API_VERSION_1_3)
    {
        return false;
    }
    VkExtensionProperties offered[64];
    uint32_t offeredCount = 64;
    VkResult listed =
        s_vulkan.vkEnumerateInstanceExtensionProperties(nullptr, &offeredCount, offered);
    for (uint32_t i = 0; (listed == VK_SUCCESS || listed == VK_INCOMPLETE) && i < offeredCount; ++i)
    {
        s_surface =
            s_surface || strcmp(offered[i].extensionName, VK_KHR_SURFACE_EXTENSION_NAME) == 0;
    }
    if ((instance = MakeInstance()) == VK_NULL_HANDLE)
    {
        return false;
    }
    mrhiVulkan functions = s_vulkan;
    uint32_t count = 0;
    bool found = mrhiLoadVulkanInstance(&functions, instance) &&
                 functions.vkEnumeratePhysicalDevices(instance, &count, nullptr) == VK_SUCCESS &&
                 count > 0;
    if (functions.vkDestroyInstance != nullptr)
    {
        functions.vkDestroyInstance(instance, nullptr);
    }
    return found;
}

int main(void)
{
    CheckOtherDriver();
    if (HasVulkan())
    {
        CheckInstanceAdoption();
        CheckInstanceExtensions();
    }
    else
    {
        const char* required = getenv("MAUL_RHI_REQUIRE_VULKAN");
        CHECK(required == nullptr || required[0] == '\0', "Vulkan where required");
        printf("skip: no Vulkan 1.3 adapter on this host\n");
    }
    mrhiCloseVulkan(&s_vulkan);
    return s_failures == 0 ? 0 : 1;
}
