// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// New textures on Vulkan filled with ones by the program's own command
// buffer, through the functions and image the device lends out
// (maul-rhi/vulkan.h), before their first frame, reading as zeros in it,
// whichever clear the driver takes. Skipped where there is no Vulkan
// adapter, unless MAUL_RHI_REQUIRE_VULKAN is set.

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "dirty_textures.h"
#include "test_harness.h"

#include "maul-rhi/instance.h"
#include "maul-rhi/vulkan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

// The device's functions the program's command buffer takes.
typedef struct Functions
{
    PFN_vkGetDeviceQueue getQueue;
    PFN_vkCreateCommandPool createPool;
    PFN_vkDestroyCommandPool destroyPool;
    PFN_vkAllocateCommandBuffers allocate;
    PFN_vkBeginCommandBuffer begin;
    PFN_vkEndCommandBuffer end;
    PFN_vkCmdPipelineBarrier2 barrier;
    PFN_vkCmdClearColorImage clearColor;
    PFN_vkCmdClearDepthStencilImage clearDepth;
    PFN_vkQueueSubmit2 submit;
    PFN_vkQueueWaitIdle waitIdle;
} Functions;

static PFN_vkVoidFunction Find(PFN_vkGetDeviceProcAddr get, VkDevice device, const char* name)
{
    PFN_vkVoidFunction found = get(device, name);
    CHECK(found != nullptr, name);
    return found;
}

#define FIND(field, name)                                                                          \
    do                                                                                             \
    {                                                                                              \
        PFN_vkVoidFunction found_ = Find(get, device, name);                                       \
        memcpy((void*)&functions->field, (const void*)&found_, sizeof(found_));                    \
    } while (0)

static bool Load(Functions* functions, PFN_vkGetDeviceProcAddr get, VkDevice device)
{
    FIND(getQueue, "vkGetDeviceQueue");
    FIND(createPool, "vkCreateCommandPool");
    FIND(destroyPool, "vkDestroyCommandPool");
    FIND(allocate, "vkAllocateCommandBuffers");
    FIND(begin, "vkBeginCommandBuffer");
    FIND(end, "vkEndCommandBuffer");
    FIND(barrier, "vkCmdPipelineBarrier2");
    FIND(clearColor, "vkCmdClearColorImage");
    FIND(clearDepth, "vkCmdClearDepthStencilImage");
    FIND(submit, "vkQueueSubmit2");
    FIND(waitIdle, "vkQueueWaitIdle");
    return functions->waitIdle != nullptr;
}

// Records the image's move to the transfer destination layout and a
// clear of every mip and layer to 1.
static void RecordFill(const Functions* functions, VkCommandBuffer commands, VkImage image,
                       bool depth)
{
    const VkImageSubresourceRange range = {
        .aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
        .levelCount = VK_REMAINING_MIP_LEVELS,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
    };
    const VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .dstStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = range,
    };
    const VkDependencyInfo dependency = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier,
    };
    functions->barrier(commands, &dependency);
    if (depth)
    {
        const VkClearDepthStencilValue one = {.depth = 1.0f};
        functions->clearDepth(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &one, 1,
                              &range);
        return;
    }
    const VkClearColorValue ones = {.float32 = {1.0f, 1.0f, 1.0f, 1.0f}};
    functions->clearColor(commands, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &ones, 1, &range);
}

// The device's queue family: the first that takes graphics and compute,
// as the library chooses.
static uint32_t FamilyOf(PFN_vkGetInstanceProcAddr getInstance, VkInstance instance,
                         VkPhysicalDevice physical)
{
    PFN_vkGetPhysicalDeviceQueueFamilyProperties families = nullptr;
    PFN_vkVoidFunction found = getInstance(instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    memcpy((void*)&families, (const void*)&found, sizeof(found));
    VkQueueFamilyProperties properties[16];
    uint32_t count = 16;
    if (families != nullptr)
    {
        families(physical, &count, properties);
    }
    for (uint32_t i = 0; families != nullptr && i < count; ++i)
    {
        VkQueueFlags wanted = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if ((properties[i].queueFlags & wanted) == wanted)
        {
            return i;
        }
    }
    return 0;
}

// Fills the image through a command buffer of the program's on the
// device's queue, and waits. The library then moves it from the
// undefined layout, over which the drivers this runs on keep the bytes.
static bool Dirty(const Functions* functions, VkDevice device, uint32_t family, VkImage image,
                  bool depth)
{
    VkQueue queue = VK_NULL_HANDLE;
    functions->getQueue(device, family, 0, &queue);
    const VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = family,
    };
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    const VkCommandBufferBeginInfo begin = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bool done = functions->createPool(device, &poolInfo, nullptr, &pool) == VK_SUCCESS;
    const VkCommandBufferAllocateInfo allocate = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    done = done && functions->allocate(device, &allocate, &commands) == VK_SUCCESS &&
           functions->begin(commands, &begin) == VK_SUCCESS;
    if (done)
    {
        RecordFill(functions, commands, image, depth);
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
    done = done && functions->end(commands) == VK_SUCCESS &&
           functions->submit(queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS &&
           functions->waitIdle(queue) == VK_SUCCESS;
    if (pool != VK_NULL_HANDLE)
    {
        functions->destroyPool(device, pool, nullptr);
    }
    return done;
}

// Each new texture, filled with ones before its first frame, reads as
// zeros in it.
static void CheckDirtyTextures(mrhiDevice* device)
{
    void* natives[5] = {0};
    CHECK(mrhiGetVulkanDevice(device, &natives[0], &natives[1], &natives[2], &natives[3],
                              &natives[4]) == mrhi_success,
          "the device's native objects");
    PFN_vkGetInstanceProcAddr getInstance = nullptr;
    PFN_vkGetDeviceProcAddr get = nullptr;
    memcpy((void*)&getInstance, (const void*)&natives[3], sizeof(getInstance));
    memcpy((void*)&get, (const void*)&natives[4], sizeof(get));
    VkDevice native = (VkDevice)natives[2];
    uint32_t family = getInstance == nullptr ? 0
                                             : FamilyOf(getInstance, (VkInstance)natives[0],
                                                        (VkPhysicalDevice)natives[1]);
    Functions functions = {0};
    if (get == nullptr || !Load(&functions, get, native))
    {
        return;
    }
    DirtyTexture textures[DIRTY_TEXTURES];
    DirtyTextures(textures);
    for (int i = 0; i < DIRTY_TEXTURES; ++i)
    {
        mrhiTextureId texture = {0};
        mrhiVulkanTextureInfo info = {0};
        bool depth = textures[i].def.format == mrhi_formatDepth32Float;
        CHECK(mrhiCreateTexture(device, &textures[i].def, &texture) == mrhi_success &&
                  mrhiGetVulkanTexture(device, texture, &info) == mrhi_success &&
                  Dirty(&functions, native, family, (VkImage)info.image, depth),
              textures[i].what);
        CHECK(ReadsZerosFirst(device, texture, &textures[i].def), textures[i].what);
        CHECK(mrhiDestroyTexture(device, texture) == mrhi_success, "ended");
    }
}

int main(void)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "an instance");
    mrhiAdapterRequestDef request = mrhiDefaultAdapterRequestDef();
    request.allowSoftware = true;
    mrhiRequestId searched = {0};
    mrhiInstanceNotification record;
    mrhiAdapterId adapter = {0};
    size_t count = 0;
    bool found = instance != nullptr &&
                 mrhiRequestAdapters(instance, &request, &searched) == mrhi_success &&
                 mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
                 mrhiGetAdapters(instance, &adapter, 1, &count) == mrhi_success && count >= 1;
    mrhiDevice* device = nullptr;
    if (found)
    {
        mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
        deviceDef.adapter = adapter;
        mrhiRequestId opened = {0};
        CHECK(mrhiCreateDevice(instance, &deviceDef, &device, &opened) == mrhi_success &&
                  mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
                  record.outcome == mrhi_success,
              "a device");
    }
    if (device != nullptr)
    {
        CheckDirtyTextures(device);
        mrhiDestroyDevice(device);
    }
    else
    {
        const char* required = getenv("MAUL_RHI_REQUIRE_VULKAN");
        CHECK(required == nullptr || required[0] == '\0', "Vulkan where required");
        printf("skip: no Vulkan adapter\n");
    }
    mrhiDestroyInstance(instance);
    return s_failures == 0 ? 0 : 1;
}
