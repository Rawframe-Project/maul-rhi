// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Vulkan device memory as the implementation describes it: a buffer it
// gives a size and a power-of-two alignment is placed and bound, and
// one of no bytes, or with an alignment of zero or not a power of two,
// is refused as the implementation's failure, never placed.

#include "test_harness.h"
#include "vulkan_memory.h"

#include <string.h>

#define BLOCKS 4
#define NODES  16

// The fake implementation's answers and what it was asked.
static VkMemoryRequirements s_needs;
static uint32_t s_allocated;
static uint32_t s_bound;
static uint64_t s_nextHandle;

static VKAPI_ATTR void VKAPI_CALL BufferNeeds(VkDevice device,
                                              const VkBufferMemoryRequirementsInfo2* info,
                                              VkMemoryRequirements2* needs)
{
    (void)device;
    (void)info;
    needs->memoryRequirements = s_needs;
}

static VKAPI_ATTR VkResult VKAPI_CALL Allocate(VkDevice device, const VkMemoryAllocateInfo* info,
                                               const VkAllocationCallbacks* callbacks,
                                               VkDeviceMemory* memory)
{
    (void)device;
    (void)info;
    (void)callbacks;
    ++s_allocated;
    uint64_t handle = ++s_nextHandle;
    memcpy((void*)memory, &handle, sizeof(handle));
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL Free(VkDevice device, VkDeviceMemory memory,
                                       const VkAllocationCallbacks* callbacks)
{
    (void)device;
    (void)memory;
    (void)callbacks;
}

static VKAPI_ATTR VkResult VKAPI_CALL Bind(VkDevice device, VkBuffer buffer, VkDeviceMemory memory,
                                           VkDeviceSize offset)
{
    (void)device;
    (void)buffer;
    (void)memory;
    (void)offset;
    ++s_bound;
    return VK_SUCCESS;
}

static mrhiVulkanDevice s_api;
static mrhiVulkanPool s_pools[2 * VK_MAX_MEMORY_TYPES];
static mrhiVulkanBlock s_blocks[BLOCKS];
static mrhiTlsfNode s_nodes[NODES];

// Memory of one device-local type in a heap of 1 GiB, nothing asked of
// the fake yet.
static void Fresh(mrhiVulkanMemory* memory)
{
    s_api = (mrhiVulkanDevice){
        .vkGetBufferMemoryRequirements2 = BufferNeeds,
        .vkAllocateMemory = Allocate,
        .vkFreeMemory = Free,
        .vkBindBufferMemory = Bind,
    };
    const VkPhysicalDeviceMemoryProperties properties = {
        .memoryTypeCount = 1,
        .memoryTypes = {{.propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, .heapIndex = 0}},
        .memoryHeapCount = 1,
        .memoryHeaps = {{.size = UINT64_C(1) << 30, .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT}},
    };
    mrhiVulkanMemoryInit(memory, &s_api, VK_NULL_HANDLE, &properties, s_pools, s_blocks, BLOCKS,
                         s_nodes, NODES);
    s_allocated = 0;
    s_bound = 0;
}

// What placing a buffer the fake describes so returns, and whether it
// was left untouched: nothing allocated and nothing bound.
static mrhiResult Place(VkDeviceSize size, VkDeviceSize alignment, bool* untouchedOut)
{
    mrhiVulkanMemory memory;
    Fresh(&memory);
    s_needs = (VkMemoryRequirements){.size = size, .alignment = alignment, .memoryTypeBits = 1};
    uint64_t handle = 1;
    VkBuffer buffer = VK_NULL_HANDLE;
    memcpy((void*)&buffer, &handle, sizeof(handle));
    mrhiVulkanAllocation allocation;
    mrhiResult status = mrhiVulkanPlace(&memory, buffer, VK_NULL_HANDLE, &allocation);
    *untouchedOut = s_allocated == 0 && s_bound == 0;
    mrhiVulkanMemoryDestroy(&memory);
    return status;
}

static void TestDescribed(void)
{
    bool untouched = false;
    CHECK(Place(256, 256, &untouched) == mrhi_success && !untouched, "placed and bound");
    CHECK(Place(0, 256, &untouched) == mrhi_errorPlatform && untouched, "no bytes");
    CHECK(Place(256, 0, &untouched) == mrhi_errorPlatform && untouched, "an alignment of zero");
    CHECK(Place(256, 48, &untouched) == mrhi_errorPlatform && untouched,
          "an alignment not a power of two");
}

int main(void)
{
    TestDescribed();
    return s_failures == 0 ? 0 : 1;
}
