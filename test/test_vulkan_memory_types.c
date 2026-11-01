// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The memory types of tile and desktop GPUs, which lavapipe lacks: the
// device's functions are read through an adopted instance that adds two
// types to the adapter's table, each backed by one of its own. One is
// lazily allocated, and only images made as transient attachments may
// take it: a target the frame renders and discards then takes memory of
// its own of that type, placed nowhere in the frame's memory, bound at its
// start and freed. The other is host visible and cached but not
// coherent, and buffers may take it: the readback ring takes it, and
// each readback's bytes are invalidated before they are read, in ranges
// of whole atoms or to the end of the memory, with an atom of 256 bytes.
// Skipped where there is no Vulkan 1.3 adapter, unless
// MAUL_RHI_REQUIRE_VULKAN is set.

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "test_harness.h"
#include "vulkan_api.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/vulkan.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ATOM    256
#define HANDLES 64
#define COPIES  16
#define RING    2048

// The loader's functions.
static mrhiVulkan s_vulkan;
// The adapter's and the device's own functions behind the wrappers.
static PFN_vkGetDeviceProcAddr s_getDevice;
static PFN_vkGetPhysicalDeviceMemoryProperties s_memoryProperties;
static PFN_vkGetPhysicalDeviceProperties2 s_properties;
static PFN_vkCreateImage s_createImage;
static PFN_vkDestroyImage s_destroyImage;
static PFN_vkGetDeviceImageMemoryRequirements s_deviceImageNeeds;
static PFN_vkGetImageMemoryRequirements2 s_imageNeeds;
static PFN_vkGetDeviceBufferMemoryRequirements s_deviceBufferNeeds;
static PFN_vkGetBufferMemoryRequirements2 s_bufferNeeds;
static PFN_vkAllocateMemory s_allocate;
static PFN_vkFreeMemory s_free;
static PFN_vkBindImageMemory s_bindImage;
static PFN_vkBindBufferMemory s_bindBuffer;
static PFN_vkCmdCopyBuffer s_copyBuffer;
static PFN_vkInvalidateMappedMemoryRanges s_invalidate;

// The added types' indices, and the adapter's types that back them.
static uint32_t s_lazyType = UINT32_MAX;
static uint32_t s_cachedType = UINT32_MAX;
static uint32_t s_lazyBacking;
static uint32_t s_cachedBacking;

// A small set of handles, as 64-bit values.
typedef struct Handles
{
    uint64_t values[HANDLES];
    uint32_t count;
} Handles;

static uint64_t ValueOf(const void* handle)
{
    uint64_t value = 0;
    memcpy(&value, handle, sizeof(value));
    return value;
}

static bool Holds(const Handles* set, uint64_t value)
{
    for (uint32_t i = 0; i < set->count; ++i)
    {
        if (set->values[i] == value)
        {
            return true;
        }
    }
    return false;
}

static void Add(Handles* set, uint64_t value)
{
    CHECK(set->count < HANDLES, "room for a handle");
    if (set->count < HANDLES)
    {
        set->values[set->count++] = value;
    }
}

static void Remove(Handles* set, uint64_t value)
{
    for (uint32_t i = 0; i < set->count; ++i)
    {
        if (set->values[i] == value)
        {
            set->values[i] = set->values[--set->count];
            return;
        }
    }
}

// The images made as transient attachments, and the memory of the lazy
// type alive.
static Handles s_transientImages;
static Handles s_lazyMemory;
// Allocations of the lazy type, frees of them, and binds of one to a
// transient image at its start; binds of one elsewhere.
static uint32_t s_lazyAllocations;
static uint32_t s_lazyFrees;
static uint32_t s_lazyBinds;
static uint32_t s_badLazyBinds;
// The memory of the cached type, its size and the buffer bound to it.
static uint64_t s_cachedMemory;
static VkDeviceSize s_cachedBytes;
static uint64_t s_readback;
static uint32_t s_cachedAllocations;
// The copies into the readback buffer, each covered by an invalidate yet.
static struct
{
    VkDeviceSize offset;
    VkDeviceSize size;
    bool invalidated;
} s_copies[COPIES];
static uint32_t s_copyCount;
// Invalidated ranges: to the end of the memory, of whole atoms, and
// breaking the rules for either, or of other memory.
static uint32_t s_wholeRanges;
static uint32_t s_atomRanges;
static uint32_t s_badRanges;

static VKAPI_ATTR void VKAPI_CALL MemoryProperties(VkPhysicalDevice physical,
                                                   VkPhysicalDeviceMemoryProperties* properties)
{
    s_memoryProperties(physical, properties);
    uint32_t count = properties->memoryTypeCount;
    s_lazyBacking = UINT32_MAX;
    s_cachedBacking = UINT32_MAX;
    for (uint32_t i = 0; i < count; ++i)
    {
        VkMemoryPropertyFlags flags = properties->memoryTypes[i].propertyFlags;
        bool local = (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
        bool visible = (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
        s_lazyBacking = local && s_lazyBacking == UINT32_MAX ? i : s_lazyBacking;
        s_cachedBacking = visible && s_cachedBacking == UINT32_MAX ? i : s_cachedBacking;
        // Only the added type is cached, so the readback ring takes it.
        properties->memoryTypes[i].propertyFlags &=
            ~(VkMemoryPropertyFlags)VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }
    if (count + 2 > VK_MAX_MEMORY_TYPES || s_lazyBacking == UINT32_MAX ||
        s_cachedBacking == UINT32_MAX)
    {
        return;
    }
    s_lazyType = count;
    s_cachedType = count + 1;
    properties->memoryTypes[s_lazyType] = (VkMemoryType){
        .propertyFlags =
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT,
        .heapIndex = properties->memoryTypes[s_lazyBacking].heapIndex,
    };
    properties->memoryTypes[s_cachedType] = (VkMemoryType){
        .propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
        .heapIndex = properties->memoryTypes[s_cachedBacking].heapIndex,
    };
    properties->memoryTypeCount = count + 2;
}

static VKAPI_ATTR void VKAPI_CALL Properties(VkPhysicalDevice physical,
                                             VkPhysicalDeviceProperties2* properties)
{
    s_properties(physical, properties);
    // The largest atom Vulkan allows, a multiple of the adapter's own.
    properties->properties.limits.nonCoherentAtomSize = ATOM;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateImage(VkDevice device, const VkImageCreateInfo* info,
                                                  const VkAllocationCallbacks* callbacks,
                                                  VkImage* image)
{
    VkResult result = s_createImage(device, info, callbacks, image);
    if (result == VK_SUCCESS && (info->usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) != 0)
    {
        Add(&s_transientImages, ValueOf(image));
    }
    return result;
}

static VKAPI_ATTR void VKAPI_CALL DestroyImage(VkDevice device, VkImage image,
                                               const VkAllocationCallbacks* callbacks)
{
    Remove(&s_transientImages, ValueOf(&image));
    s_destroyImage(device, image, callbacks);
}

static uint32_t LazyBit(bool transient)
{
    return transient && s_lazyType != UINT32_MAX ? 1u << s_lazyType : 0u;
}

static uint32_t CachedBit(void)
{
    return s_cachedType != UINT32_MAX ? 1u << s_cachedType : 0u;
}

static VKAPI_ATTR void VKAPI_CALL DeviceImageNeeds(VkDevice device,
                                                   const VkDeviceImageMemoryRequirements* info,
                                                   VkMemoryRequirements2* needs)
{
    s_deviceImageNeeds(device, info, needs);
    needs->memoryRequirements.memoryTypeBits |=
        LazyBit((info->pCreateInfo->usage & VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT) != 0);
}

static VKAPI_ATTR void VKAPI_CALL ImageNeeds(VkDevice device,
                                             const VkImageMemoryRequirementsInfo2* info,
                                             VkMemoryRequirements2* needs)
{
    s_imageNeeds(device, info, needs);
    needs->memoryRequirements.memoryTypeBits |=
        LazyBit(Holds(&s_transientImages, ValueOf(&info->image)));
}

static VKAPI_ATTR void VKAPI_CALL DeviceBufferNeeds(VkDevice device,
                                                    const VkDeviceBufferMemoryRequirements* info,
                                                    VkMemoryRequirements2* needs)
{
    s_deviceBufferNeeds(device, info, needs);
    needs->memoryRequirements.memoryTypeBits |= CachedBit();
}

static VKAPI_ATTR void VKAPI_CALL BufferNeeds(VkDevice device,
                                              const VkBufferMemoryRequirementsInfo2* info,
                                              VkMemoryRequirements2* needs)
{
    s_bufferNeeds(device, info, needs);
    needs->memoryRequirements.memoryTypeBits |= CachedBit();
}

static VKAPI_ATTR VkResult VKAPI_CALL Allocate(VkDevice device, const VkMemoryAllocateInfo* info,
                                               const VkAllocationCallbacks* callbacks,
                                               VkDeviceMemory* memory)
{
    VkMemoryAllocateInfo backed = *info;
    bool lazy = info->memoryTypeIndex == s_lazyType;
    bool cached = info->memoryTypeIndex == s_cachedType;
    backed.memoryTypeIndex =
        lazy ? s_lazyBacking : (cached ? s_cachedBacking : info->memoryTypeIndex);
    VkResult result = s_allocate(device, &backed, callbacks, memory);
    if (result == VK_SUCCESS && lazy)
    {
        ++s_lazyAllocations;
        Add(&s_lazyMemory, ValueOf(memory));
    }
    if (result == VK_SUCCESS && cached)
    {
        ++s_cachedAllocations;
        s_cachedMemory = ValueOf(memory);
        s_cachedBytes = info->allocationSize;
    }
    return result;
}

static VKAPI_ATTR void VKAPI_CALL Free(VkDevice device, VkDeviceMemory memory,
                                       const VkAllocationCallbacks* callbacks)
{
    uint64_t value = ValueOf(&memory);
    if (value != 0 && Holds(&s_lazyMemory, value))
    {
        ++s_lazyFrees;
        Remove(&s_lazyMemory, value);
    }
    s_free(device, memory, callbacks);
}

static VKAPI_ATTR VkResult VKAPI_CALL BindImage(VkDevice device, VkImage image,
                                                VkDeviceMemory memory, VkDeviceSize offset)
{
    if (Holds(&s_lazyMemory, ValueOf(&memory)))
    {
        bool fits = Holds(&s_transientImages, ValueOf(&image)) && offset == 0;
        s_lazyBinds += fits ? 1 : 0;
        s_badLazyBinds += fits ? 0 : 1;
    }
    return s_bindImage(device, image, memory, offset);
}

static VKAPI_ATTR VkResult VKAPI_CALL BindBuffer(VkDevice device, VkBuffer buffer,
                                                 VkDeviceMemory memory, VkDeviceSize offset)
{
    s_readback = ValueOf(&memory) == s_cachedMemory ? ValueOf(&buffer) : s_readback;
    s_badLazyBinds += Holds(&s_lazyMemory, ValueOf(&memory)) ? 1 : 0;
    return s_bindBuffer(device, buffer, memory, offset);
}

static VKAPI_ATTR void VKAPI_CALL CopyBuffer(VkCommandBuffer commands, VkBuffer source,
                                             VkBuffer destination, uint32_t count,
                                             const VkBufferCopy* regions)
{
    for (uint32_t i = 0; ValueOf(&destination) == s_readback && i < count; ++i)
    {
        CHECK(s_copyCount < COPIES, "room for a copy");
        if (s_copyCount < COPIES)
        {
            s_copies[s_copyCount].offset = regions[i].dstOffset;
            s_copies[s_copyCount].size = regions[i].size;
            s_copies[s_copyCount++].invalidated = false;
        }
    }
    s_copyBuffer(commands, source, destination, count, regions);
}

static VKAPI_ATTR VkResult VKAPI_CALL Invalidate(VkDevice device, uint32_t count,
                                                 const VkMappedMemoryRange* ranges)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        const VkMappedMemoryRange* range = &ranges[i];
        bool whole = range->size == VK_WHOLE_SIZE;
        VkDeviceSize end = whole ? s_cachedBytes : range->offset + range->size;
        bool rules = ValueOf(&range->memory) == s_cachedMemory && range->offset % ATOM == 0 &&
                     end <= s_cachedBytes && (whole || range->size % ATOM == 0);
        s_wholeRanges += rules && whole ? 1 : 0;
        s_atomRanges += rules && !whole ? 1 : 0;
        s_badRanges += rules ? 0 : 1;
        for (uint32_t c = 0; rules && c < s_copyCount; ++c)
        {
            s_copies[c].invalidated =
                s_copies[c].invalidated || (range->offset <= s_copies[c].offset &&
                                            s_copies[c].offset + s_copies[c].size <= end);
        }
    }
    return s_invalidate(device, count, ranges);
}

// A function the wrappers replace: its wrapper, keeping the adapter's or
// the device's own.
static PFN_vkVoidFunction Wrap(const char* name, PFN_vkVoidFunction own)
{
    static const struct
    {
        const char* name;
        PFN_vkVoidFunction* own;
        PFN_vkVoidFunction wrapper;
    } wrapped[] = {
        {"vkGetPhysicalDeviceMemoryProperties", (PFN_vkVoidFunction*)&s_memoryProperties,
         (PFN_vkVoidFunction)MemoryProperties},
        {"vkGetPhysicalDeviceProperties2", (PFN_vkVoidFunction*)&s_properties,
         (PFN_vkVoidFunction)Properties},
        {"vkCreateImage", (PFN_vkVoidFunction*)&s_createImage, (PFN_vkVoidFunction)CreateImage},
        {"vkDestroyImage", (PFN_vkVoidFunction*)&s_destroyImage, (PFN_vkVoidFunction)DestroyImage},
        {"vkGetDeviceImageMemoryRequirements", (PFN_vkVoidFunction*)&s_deviceImageNeeds,
         (PFN_vkVoidFunction)DeviceImageNeeds},
        {"vkGetImageMemoryRequirements2", (PFN_vkVoidFunction*)&s_imageNeeds,
         (PFN_vkVoidFunction)ImageNeeds},
        {"vkGetDeviceBufferMemoryRequirements", (PFN_vkVoidFunction*)&s_deviceBufferNeeds,
         (PFN_vkVoidFunction)DeviceBufferNeeds},
        {"vkGetBufferMemoryRequirements2", (PFN_vkVoidFunction*)&s_bufferNeeds,
         (PFN_vkVoidFunction)BufferNeeds},
        {"vkAllocateMemory", (PFN_vkVoidFunction*)&s_allocate, (PFN_vkVoidFunction)Allocate},
        {"vkFreeMemory", (PFN_vkVoidFunction*)&s_free, (PFN_vkVoidFunction)Free},
        {"vkBindImageMemory", (PFN_vkVoidFunction*)&s_bindImage, (PFN_vkVoidFunction)BindImage},
        {"vkBindBufferMemory", (PFN_vkVoidFunction*)&s_bindBuffer, (PFN_vkVoidFunction)BindBuffer},
        {"vkCmdCopyBuffer", (PFN_vkVoidFunction*)&s_copyBuffer, (PFN_vkVoidFunction)CopyBuffer},
        {"vkInvalidateMappedMemoryRanges", (PFN_vkVoidFunction*)&s_invalidate,
         (PFN_vkVoidFunction)Invalidate},
    };
    for (size_t i = 0; own != nullptr && i < sizeof(wrapped) / sizeof(wrapped[0]); ++i)
    {
        if (strcmp(name, wrapped[i].name) == 0)
        {
            *wrapped[i].own = own;
            return wrapped[i].wrapper;
        }
    }
    return own;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDevice(VkDevice device, const char* name)
{
    return Wrap(name, s_getDevice(device, name));
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstance(VkInstance instance, const char* name)
{
    PFN_vkVoidFunction own = s_vulkan.vkGetInstanceProcAddr(instance, name);
    if (own != nullptr && strcmp(name, "vkGetDeviceProcAddr") == 0)
    {
        s_getDevice = (PFN_vkGetDeviceProcAddr)own;
        return (PFN_vkVoidFunction)GetDevice;
    }
    return Wrap(name, own);
}

// An instance the test makes, Vulkan 1.3, or VK_NULL_HANDLE.
static VkInstance MakeInstance(void)
{
    uint32_t version = 0;
    if (!mrhiOpenVulkan(&s_vulkan) || s_vulkan.vkEnumerateInstanceVersion(&version) != VK_SUCCESS ||
        version < VK_API_VERSION_1_3)
    {
        return VK_NULL_HANDLE;
    }
    const VkApplicationInfo application = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .apiVersion = VK_API_VERSION_1_3,
    };
    const VkInstanceCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &application,
    };
    VkInstance instance = VK_NULL_HANDLE;
    return s_vulkan.vkCreateInstance(&info, nullptr, &instance) == VK_SUCCESS ? instance
                                                                              : VK_NULL_HANDLE;
}

// A device on the adopted instance's first adapter, with a readback ring
// of the bytes given (the default for 0), or NULL.
static mrhiDevice* OpenDevice(mrhiInstance* instance, uint32_t ring)
{
    mrhiAdapterRequestDef request = mrhiDefaultAdapterRequestDef();
    request.allowSoftware = true;
    mrhiRequestId id;
    mrhiInstanceNotification record;
    mrhiAdapterId adapter = {0};
    size_t count = 0;
    if (mrhiInstance_RequestAdapters(instance, &request, &id) != mrhi_success ||
        mrhiInstance_TakeNotification(instance, &record) != mrhi_success ||
        mrhiInstance_GetAdapters(instance, &adapter, 1, &count) != mrhi_success || count == 0)
    {
        return nullptr;
    }
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    def.budgets.readbackBytes = ring != 0 ? ring : def.budgets.readbackBytes;
    mrhiDevice* device = nullptr;
    mrhiRequestId ready;
    if (mrhiCreateDevice(instance, &def, &device, &ready) != mrhi_success ||
        mrhiInstance_TakeNotification(instance, &record) != mrhi_success ||
        record.outcome != mrhi_success)
    {
        return nullptr;
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

static mrhiResourceId DeclareTarget(mrhiDevice* device, uint32_t samples)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = 8;
    def.height = 8;
    def.sampleCount = samples;
    mrhiResourceId texture = {0};
    CHECK(mrhiDeclareTexture(device, &def, &texture) == mrhi_success, "a target");
    return texture;
}

static void Finish(mrhiDevice* device)
{
    mrhiRequestId token = {0};
    CHECK(mrhiDevice_SubmitFrame(device, &token) == mrhi_success &&
              mrhiRequest_WaitFrame(device, token, UINT64_C(10000000000)) == mrhi_success,
          "the frame ran");
    mrhiDeviceNotification record;
    while (mrhiDevice_TakeNotification(device, &record) == mrhi_success)
    {
    }
}

// A frame clearing four samples it discards and resolving them into a
// texture that is read back: the samples take lazily allocated memory of
// their own, the resolved texture its place in the frame's memory, and
// the clear color comes back.
static void CheckOnChip(mrhiDevice* device)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success, "a frame");
    mrhiResourceId samples = DeclareTarget(device, 4);
    mrhiResourceId resolved = DeclareTarget(device, 1);
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){
        .resource = samples,
        .resolve = resolved,
        .load = mrhi_loadClear,
        .store = mrhi_storeDiscard,
        .clear = {0.0f, 1.0f, 0.0f, 1.0f},
    };
    def.colorTargetCount = 1;
    mrhiPassId clear = {0};
    CHECK(mrhiAddPass(device, &def, &clear) == mrhi_success, "a resolving pass");
    mrhiAccess read = Whole(resolved, mrhi_accessCopySource);
    mrhiPassDef readDef = mrhiDefaultPassDef();
    readDef.accesses = &read;
    readDef.accessCount = 1;
    readDef.neverCull = true;
    mrhiPassId reading = {0};
    CHECK(mrhiAddPass(device, &readDef, &reading) == mrhi_success, "a reading pass");
    uint32_t allocations = s_lazyAllocations;
    uint32_t binds = s_lazyBinds;
    mrhiRequestId pixels = {0};
    CHECK(mrhiDevice_CompileFrame(device) == mrhi_success &&
              mrhiPass_Begin(device, clear) == mrhi_success &&
              mrhiPass_End(device, clear) == mrhi_success &&
              mrhiPass_Begin(device, reading) == mrhi_success &&
              mrhiPass_ReadTexture(device, reading, &(mrhiTextureCopy){.resource = resolved},
                                   &(mrhiExtent3d){8, 8, 1}, &pixels) == mrhi_success &&
              mrhiPass_End(device, reading) == mrhi_success,
          "recorded");
    mrhiResourcePlan samplesPlan = {0};
    mrhiResourcePlan resolvedPlan = {0};
    CHECK(mrhiResource_GetPlan(device, samples, &samplesPlan) == mrhi_success &&
              mrhiResource_GetPlan(device, resolved, &resolvedPlan) == mrhi_success,
          "the plans");
    CHECK(samplesPlan.memoryBytes == 0, "the discarded samples placed nowhere");
    CHECK(resolvedPlan.memoryBytes > 0, "the texture read back placed in the frame's memory");
    Finish(device);
    CHECK(s_lazyAllocations == allocations + 1 && s_lazyBinds == binds + 1,
          "the samples given lazily allocated memory of their own, bound at its start");
    CHECK(s_badLazyBinds == 0, "nothing else bound to lazily allocated memory");
    uint8_t image[256];
    size_t size = 0;
    CHECK(mrhiRequest_TakeReadback(device, pixels, image, sizeof(image), &size) == mrhi_success &&
              size == sizeof(image),
          "the pixels");
    bool green = true;
    for (int i = 0; i < 64; ++i)
    {
        green = green && image[i * 4] == 0 && image[i * 4 + 1] == 255 && image[i * 4 + 3] == 255;
    }
    CHECK(green, "the clear color resolved");
}

// Reads a buffer of known words back through the readback ring, the
// sizes chosen one after another.
static void ReadBack(mrhiDevice* device, const uint64_t* sizes, uint32_t count)
{
    uint8_t words[RING];
    for (size_t i = 0; i < sizeof(words); ++i)
    {
        words[i] = (uint8_t)(i * 7 + 3);
    }
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = sizeof(words);
    mrhiResourceId buffer = {0};
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
              mrhiDeclareBuffer(device, &def, &buffer) == mrhi_success,
          "a frame and a buffer");
    mrhiAccess write = Whole(buffer, mrhi_accessCopyDestination);
    mrhiAccess read = Whole(buffer, mrhi_accessCopySource);
    mrhiPassDef passDef = mrhiDefaultPassDef();
    passDef.accesses = &write;
    passDef.accessCount = 1;
    passDef.neverCull = true;
    mrhiPassId upload = {0};
    CHECK(mrhiAddPass(device, &passDef, &upload) == mrhi_success, "an upload");
    passDef.accesses = &read;
    mrhiPassId reading = {0};
    CHECK(mrhiAddPass(device, &passDef, &reading) == mrhi_success, "a read");
    CHECK(mrhiDevice_CompileFrame(device) == mrhi_success &&
              mrhiPass_Begin(device, upload) == mrhi_success &&
              mrhiPass_WriteBuffer(device, upload, buffer, 0, words, sizeof(words)) ==
                  mrhi_success &&
              mrhiPass_End(device, upload) == mrhi_success &&
              mrhiPass_Begin(device, reading) == mrhi_success,
          "uploaded");
    mrhiRequestId requests[4] = {0};
    for (uint32_t i = 0; i < count; ++i)
    {
        CHECK(mrhiPass_ReadBuffer(device, reading, buffer, 0, sizes[i], &requests[i]) ==
                  mrhi_success,
              "a read back");
    }
    CHECK(mrhiPass_End(device, reading) == mrhi_success, "read");
    s_copyCount = 0;
    Finish(device);
    for (uint32_t i = 0; i < count; ++i)
    {
        uint8_t taken[RING];
        size_t size = 0;
        CHECK(mrhiRequest_TakeReadback(device, requests[i], taken, sizeof(taken), &size) ==
                      mrhi_success &&
                  size == sizes[i] && memcmp(taken, words, size) == 0,
              "the words back");
    }
    CHECK(s_copyCount == count, "each read copied into the readback ring");
    bool invalidated = true;
    for (uint32_t c = 0; c < s_copyCount; ++c)
    {
        invalidated = invalidated && s_copies[c].invalidated;
    }
    CHECK(invalidated, "each read's bytes invalidated before they were taken");
}

// One frame's reads on a fresh ring, which places each at a multiple of
// 512 bytes: two of 100 and 4 bytes, ending inside an atom, and one of
// 1024 that ends at the ring's end. The ring takes the cached type, and
// each read is invalidated, in whole atoms or to the memory's end.
static void CheckNonCoherent(mrhiDevice* device)
{
    CHECK(s_readback != 0 && s_cachedBytes >= RING,
          "the readback ring in the cached, non-coherent type");
    const uint64_t sizes[3] = {100, 4, RING - 1024};
    ReadBack(device, sizes, 3);
    CHECK(s_atomRanges > 0 && s_wholeRanges > 0,
          "ranges of whole atoms, and one to the end of the memory");
    CHECK(s_badRanges == 0, "every range of the ring's memory, aligned to atoms");
}

int main(void)
{
    VkInstance made = MakeInstance();
    if (made == VK_NULL_HANDLE)
    {
        const char* required = getenv("MAUL_RHI_REQUIRE_VULKAN");
        CHECK(required == nullptr || required[0] == '\0', "Vulkan where required");
        printf("skip: no Vulkan 1.3 adapter on this host\n");
        mrhiCloseVulkan(&s_vulkan);
        return s_failures == 0 ? 0 : 1;
    }
    PFN_vkGetInstanceProcAddr entry = GetInstance;
    mrhiInstanceVulkanAdopt adopt = {
        .chain = {.type = mrhi_structInstanceVulkanAdopt},
        .instance = (void*)made,
        .apiVersion = VK_API_VERSION_1_3,
    };
    static_assert(sizeof(adopt.getInstanceProcAddr) == sizeof(entry), "function pointers");
    memcpy((void*)&adopt.getInstanceProcAddr, (const void*)&entry, sizeof(entry));
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &adopt.chain;
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "an adopted instance");
    mrhiDevice* device = OpenDevice(instance, 0);
    CHECK(device != nullptr && s_lazyType != UINT32_MAX, "a device, with the two types added");
    if (device != nullptr)
    {
        CheckOnChip(device);
        mrhiDestroyDevice(device);
    }
    CHECK(s_lazyAllocations > 0 && s_lazyFrees == s_lazyAllocations,
          "each lazily allocated memory freed");
    uint32_t cached = s_cachedAllocations;
    device = OpenDevice(instance, RING);
    CHECK(device != nullptr && s_cachedAllocations == cached + 1, "a device with a small ring");
    if (device != nullptr)
    {
        CheckNonCoherent(device);
        mrhiDestroyDevice(device);
    }
    mrhiDestroyInstance(instance);
    PFN_vkDestroyInstance destroy =
        (PFN_vkDestroyInstance)s_vulkan.vkGetInstanceProcAddr(made, "vkDestroyInstance");
    if (destroy != nullptr)
    {
        destroy(made, nullptr);
    }
    mrhiCloseVulkan(&s_vulkan);
    return s_failures == 0 ? 0 : 1;
}
