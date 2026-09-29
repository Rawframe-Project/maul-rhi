// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Vulkan driver's instance (mrhi-0003): a Vulkan 1.3 instance on the
// loader opened at run time, adapter searches answered at the next
// poll from the physical devices then present.

#include "driver_vulkan.h"

#include "allocator.h"
#include "invariant.h"
#include "vulkan_adapter.h"
#include "vulkan_device.h"

#include <stdalign.h>
#include <string.h>

typedef struct VulkanDriver
{
    mrhiAllocator allocator;
    size_t bytes;
    mrhiVulkan vulkan;
    VkInstance instance;
    mrhiDriverEvent* pending;
    uint32_t pendingCount;
    uint32_t pendingLimit;
    // Room for the physical devices one search reads.
    VkPhysicalDevice* devices;
    uint32_t deviceLimit;
} VulkanDriver;

static mrhiResult RequestAdapters(void* self, uint64_t tag)
{
    VulkanDriver* driver = self;
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
    VulkanDriver* driver = self;
    size_t moved = driver->pendingCount < capacity ? driver->pendingCount : capacity;
    memcpy(events, driver->pending, moved * sizeof(mrhiDriverEvent));
    memmove(driver->pending, driver->pending + moved,
            (driver->pendingCount - moved) * sizeof(mrhiDriverEvent));
    driver->pendingCount -= (uint32_t)moved;
    return moved;
}

// Lists the physical devices that meet the floor. A host with more
// physical devices than the room counts the ones not read, so that the
// core answers capacity.
static size_t GetAdapters(const void* self, mrhiDriverAdapter* adapters, size_t capacity)
{
    const VulkanDriver* driver = self;
    uint32_t present = 0;
    if (driver->vulkan.vkEnumeratePhysicalDevices(driver->instance, &present, nullptr) !=
        VK_SUCCESS)
    {
        return 0;
    }
    uint32_t read = driver->deviceLimit;
    VkResult status =
        driver->vulkan.vkEnumeratePhysicalDevices(driver->instance, &read, driver->devices);
    if (status != VK_SUCCESS && status != VK_INCOMPLETE)
    {
        return 0;
    }
    size_t found = 0;
    for (uint32_t i = 0; i < read; ++i)
    {
        mrhiDriverAdapter adapter;
        if (mrhiDescribeVulkanAdapter(&driver->vulkan, driver->devices[i], &adapter))
        {
            if (found < capacity)
            {
                adapters[found] = adapter;
            }
            ++found;
        }
    }
    return found + (present > read ? present - read : 0);
}

// The physical device an adapter handle holds.
static VkPhysicalDevice DeviceOf(uint64_t adapter)
{
    static_assert(sizeof(VkPhysicalDevice) == sizeof(uintptr_t), "a handle holds a device");
    uintptr_t bits = (uintptr_t)adapter;
    VkPhysicalDevice device;
    memcpy((void*)&device, &bits, sizeof(bits));
    return device;
}

static void GetFormatCaps(const void* self, uint64_t adapter, mrhiFormat format,
                          mrhiFormatCaps* capsOut)
{
    const VulkanDriver* driver = self;
    mrhiGetVulkanFormatCaps(&driver->vulkan, DeviceOf(adapter), format, capsOut);
}

// Surfaces come with the window system's headers.
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

// Opens the device at once and answers the opening at the next poll.
static mrhiResult CreateDevice(void* self, uint64_t adapter, const mrhiDeviceDef* def, uint64_t tag,
                               mrhiDeviceDriver* deviceOut)
{
    VulkanDriver* driver = self;
    if (driver->pendingCount == driver->pendingLimit)
    {
        return mrhi_errorCapacity;
    }
    mrhiResult status = mrhiCreateVulkanDevice(&driver->allocator, &driver->vulkan,
                                               DeviceOf(adapter), def, deviceOut);
    if (status == mrhi_success)
    {
        driver->pending[driver->pendingCount++] =
            (mrhiDriverEvent){.tag = tag, .outcome = mrhi_success};
    }
    return status;
}

static void Destroy(void* self)
{
    VulkanDriver* driver = self;
    driver->vulkan.vkDestroyInstance(driver->instance, nullptr);
    mrhiCloseVulkan(&driver->vulkan);
    mrhiAllocator allocator = driver->allocator;
    mrhiRelease(&allocator, driver, driver->bytes, alignof(VulkanDriver));
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

// Makes a Vulkan 1.3 instance with its functions read: VK_NULL_HANDLE
// where the loader is older or refuses. Vulkan's own allocations stay
// the platform's: its callbacks free without a size, which the
// program's allocator needs.
static VkInstance CreateInstance(mrhiVulkan* vulkan)
{
    uint32_t version = 0;
    if (vulkan->vkEnumerateInstanceVersion(&version) != VK_SUCCESS || version < VK_API_VERSION_1_3)
    {
        return VK_NULL_HANDLE;
    }
    const VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pEngineName = "Maul RHI",
        .engineVersion =
            VK_MAKE_API_VERSION(0, MRHI_VERSION_MAJOR, MRHI_VERSION_MINOR, MRHI_VERSION_PATCH),
        .apiVersion = VK_API_VERSION_1_3,
    };
    const VkInstanceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
    };
    VkInstance instance = VK_NULL_HANDLE;
    if (vulkan->vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    if (!mrhiLoadVulkanInstance(vulkan, instance))
    {
        if (vulkan->vkDestroyInstance != nullptr)
        {
            vulkan->vkDestroyInstance(instance, nullptr);
        }
        return VK_NULL_HANDLE;
    }
    return instance;
}

mrhiResult mrhiCreateVulkanDriver(const mrhiAllocator* allocator, uint32_t pendingLimit,
                                  uint32_t adapterLimit, mrhiInstanceDriver* driverOut)
{
    *driverOut = (mrhiInstanceDriver){0};
    mrhiVulkan vulkan;
    if (!mrhiOpenVulkan(&vulkan))
    {
        return mrhi_success;
    }
    mrhiLayout layout = {.size = sizeof(VulkanDriver)};
    size_t pendingAt =
        mrhiLayoutAdd(&layout, pendingLimit, sizeof(mrhiDriverEvent), alignof(mrhiDriverEvent));
    size_t devicesAt =
        mrhiLayoutAdd(&layout, adapterLimit, sizeof(VkPhysicalDevice), alignof(VkPhysicalDevice));
    VulkanDriver* driver =
        layout.overflow ? nullptr : mrhiAllocate(allocator, layout.size, alignof(VulkanDriver));
    if (driver == nullptr)
    {
        mrhiCloseVulkan(&vulkan);
        return mrhi_errorCapacity;
    }
    VkInstance instance = CreateInstance(&vulkan);
    if (instance == VK_NULL_HANDLE)
    {
        mrhiRelease(allocator, driver, layout.size, alignof(VulkanDriver));
        mrhiCloseVulkan(&vulkan);
        return mrhi_success;
    }
    unsigned char* block = (unsigned char*)driver;
    *driver = (VulkanDriver){
        .allocator = *allocator,
        .bytes = layout.size,
        .vulkan = vulkan,
        .instance = instance,
        .pending = (mrhiDriverEvent*)(block + pendingAt),
        .pendingLimit = pendingLimit,
        .devices = (VkPhysicalDevice*)(block + devicesAt),
        .deviceLimit = adapterLimit,
    };
    *driverOut = (mrhiInstanceDriver){.vtable = &s_vtable, .self = driver};
    return mrhi_success;
}
