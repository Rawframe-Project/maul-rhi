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
// The functions of the instance the test made.
static mrhiVulkan s_made;
// Whether the test's instances enable VK_KHR_surface, which the loader
// offers wherever a driver presents.
static bool s_surface;

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
           mrhiGetAdapters(instance, adapterOut, 1, &count) == mrhi_success && count == 1;
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
    mrhiDestroyDevice(device);
    // The device outlives the library's hold of it.
    mrhiVulkanDevice functions;
    CHECK(mrhiLoadVulkanDevice(&s_made, made, false, &functions) &&
              functions.vkDeviceWaitIdle(made) == VK_SUCCESS,
          "the adopted device still alive");
    functions.vkDestroyDevice(made, nullptr);
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
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    def.next = &extensions.chain;
    void* info = nullptr;
    void* physical = nullptr;
    CHECK(mrhiDescribeVulkanDevice(instance, &def, &info, &physical) == mrhi_success,
          "described with extensions");
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
    if (device != nullptr)
    {
        CheckRuns(device);
        mrhiDestroyDevice(device);
    }
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
