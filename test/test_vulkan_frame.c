// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan frame whose command buffer fails to begin: the device's
// functions are read through an adopted instance whose
// vkGetDeviceProcAddr hands the library a vkBeginCommandBuffer that
// fails on the begin chosen. The frame then records nothing more into
// that buffer, ends nothing it holds, submits nothing, reports the
// failure under the command lists budget, and the device runs its next
// frame. A command pool that fails to reset is refused the same way. Each begin a frame makes
// fails in turn: the frame's first, the one after its native pass, and
// with a lane per class each lane's batches'. Skipped
// where there is no Vulkan 1.3 adapter, unless MAUL_RHI_REQUIRE_VULKAN is
// set.

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

// The loader's functions.
static mrhiVulkan s_vulkan;
// The device's own functions behind the wrappers.
static PFN_vkGetDeviceProcAddr s_getDevice;
static PFN_vkBeginCommandBuffer s_begin;
static PFN_vkEndCommandBuffer s_end;
static PFN_vkCmdPipelineBarrier2 s_barrier;
static PFN_vkCmdResetQueryPool s_resetQueries;
static PFN_vkCmdWriteTimestamp2 s_timestamp;
static PFN_vkQueueSubmit2 s_submit;
static PFN_vkResetCommandPool s_reset;

// The begins of the frame so far, the one that fails (0 for none), and
// the command buffer it failed.
static uint32_t s_begins;
static uint32_t s_failAt;
static VkCommandBuffer s_failed;
// The calls made on the failed buffer, and the submissions after it.
static uint32_t s_misuses;
// Whether the next reset of a command pool fails.
static bool s_failReset;

static VKAPI_ATTR VkResult VKAPI_CALL Begin(VkCommandBuffer commands,
                                            const VkCommandBufferBeginInfo* info)
{
    if (++s_begins == s_failAt)
    {
        s_failed = commands;
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    return s_begin(commands, info);
}

static VKAPI_ATTR VkResult VKAPI_CALL End(VkCommandBuffer commands)
{
    if (commands == s_failed)
    {
        ++s_misuses;
        return VK_SUCCESS;
    }
    return s_end(commands);
}

static VKAPI_ATTR void VKAPI_CALL Barrier(VkCommandBuffer commands,
                                          const VkDependencyInfo* dependency)
{
    s_misuses += commands == s_failed ? 1 : 0;
    if (commands != s_failed)
    {
        s_barrier(commands, dependency);
    }
}

static VKAPI_ATTR void VKAPI_CALL ResetQueries(VkCommandBuffer commands, VkQueryPool pool,
                                               uint32_t first, uint32_t count)
{
    s_misuses += commands == s_failed ? 1 : 0;
    if (commands != s_failed)
    {
        s_resetQueries(commands, pool, first, count);
    }
}

static VKAPI_ATTR void VKAPI_CALL Timestamp(VkCommandBuffer commands, VkPipelineStageFlags2 stage,
                                            VkQueryPool pool, uint32_t query)
{
    s_misuses += commands == s_failed ? 1 : 0;
    if (commands != s_failed)
    {
        s_timestamp(commands, stage, pool, query);
    }
}

static VKAPI_ATTR VkResult VKAPI_CALL Submit(VkQueue queue, uint32_t count,
                                             const VkSubmitInfo2* submits, VkFence fence)
{
    s_misuses += s_failed != VK_NULL_HANDLE ? 1 : 0;
    return s_failed != VK_NULL_HANDLE ? VK_SUCCESS : s_submit(queue, count, submits, fence);
}

static VKAPI_ATTR VkResult VKAPI_CALL Reset(VkDevice device, VkCommandPool pool,
                                            VkCommandPoolResetFlags flags)
{
    if (s_failReset)
    {
        s_failReset = false;
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    return s_reset(device, pool, flags);
}

// A function the wrappers replace: its wrapper, keeping the device's own.
static PFN_vkVoidFunction Wrap(const char* name, PFN_vkVoidFunction own)
{
    static const struct
    {
        const char* name;
        PFN_vkVoidFunction* own;
        PFN_vkVoidFunction wrapper;
    } wrapped[] = {
        {"vkBeginCommandBuffer", (PFN_vkVoidFunction*)&s_begin, (PFN_vkVoidFunction)Begin},
        {"vkEndCommandBuffer", (PFN_vkVoidFunction*)&s_end, (PFN_vkVoidFunction)End},
        {"vkCmdPipelineBarrier2", (PFN_vkVoidFunction*)&s_barrier, (PFN_vkVoidFunction)Barrier},
        {"vkCmdResetQueryPool", (PFN_vkVoidFunction*)&s_resetQueries,
         (PFN_vkVoidFunction)ResetQueries},
        {"vkCmdWriteTimestamp2", (PFN_vkVoidFunction*)&s_timestamp, (PFN_vkVoidFunction)Timestamp},
        {"vkQueueSubmit2", (PFN_vkVoidFunction*)&s_submit, (PFN_vkVoidFunction)Submit},
        {"vkResetCommandPool", (PFN_vkVoidFunction*)&s_reset, (PFN_vkVoidFunction)Reset},
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
    return own;
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

// A device on the adopted instance's first adapter, with timestamps
// where it has them, or NULL.
static mrhiDevice* OpenDevice(mrhiInstance* instance, bool* timestampsOut)
{
    mrhiAdapterRequestDef request = mrhiDefaultAdapterRequestDef();
    request.allowSoftware = true;
    mrhiRequestId id;
    mrhiInstanceNotification record;
    mrhiAdapterId adapter = {0};
    size_t count = 0;
    mrhiFeatures features = {0};
    if (mrhiInstance_RequestAdapters(instance, &request, &id) != mrhi_success ||
        mrhiInstance_TakeNotification(instance, &record) != mrhi_success ||
        mrhiInstance_GetAdapters(instance, &adapter, 1, &count) != mrhi_success || count == 0 ||
        mrhiAdapter_GetFeatures(instance, adapter, &features) != mrhi_success)
    {
        return nullptr;
    }
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    def.features.timestampQuery = features.timestampQuery;
    def.budgets.diagnostics = 8;
    *timestampsOut = features.timestampQuery;
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

// A pass of a class with one access (none when NULL), timed when the set
// is not null.
static mrhiPassId AddPass(mrhiDevice* device, mrhiPassClass passClass, const mrhiAccess* access,
                          mrhiQuerySetId times)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = passClass;
    def.accesses = access;
    def.accessCount = access != nullptr ? 1 : 0;
    def.native = access == nullptr;
    def.neverCull = true;
    if (times.index1 != 0)
    {
        def.timestampQuerySet = times;
        def.timestampBegin = 0;
        def.timestampEnd = 1;
    }
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(device, &def, &pass) == mrhi_success, "a pass");
    return pass;
}

// Submits a frame of three classes: a transfer upload, an async compute
// pass reading it, a native pass with no command buffer of its own, and
// a graphics pass, timed where the device can be, reading it back.
// Returns what the submission returned; when the frame ran, the bytes it
// read back are checked.
static mrhiResult SubmitFrame(mrhiDevice* device, mrhiQuerySetId times)
{
    const uint32_t words[4] = {1, 2, 3, 4};
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = sizeof(words);
    mrhiResourceId buffer = {0};
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
              mrhiDeclareBuffer(device, &def, &buffer) == mrhi_success,
          "a frame and a buffer");
    mrhiAccess write = Whole(buffer, mrhi_accessCopyDestination);
    mrhiAccess back = Whole(buffer, mrhi_accessCopySource);
    mrhiPassId upload = AddPass(device, mrhi_passTransfer, &write, (mrhiQuerySetId){0});
    mrhiPassId compute = AddPass(device, mrhi_passAsyncCompute, &back, (mrhiQuerySetId){0});
    mrhiPassId native = AddPass(device, mrhi_passGraphics, nullptr, (mrhiQuerySetId){0});
    mrhiPassId graphics = AddPass(device, mrhi_passGraphics, &back, times);
    mrhiRequestId bytes = {0};
    CHECK(mrhiDevice_CompileFrame(device) == mrhi_success &&
              mrhiPass_Begin(device, upload) == mrhi_success &&
              mrhiPass_WriteBuffer(device, upload, buffer, 0, words, sizeof(words)) ==
                  mrhi_success &&
              mrhiPass_End(device, upload) == mrhi_success &&
              mrhiPass_Begin(device, compute) == mrhi_success &&
              mrhiPass_End(device, compute) == mrhi_success &&
              mrhiPass_Begin(device, native) == mrhi_success &&
              mrhiPass_End(device, native) == mrhi_success &&
              mrhiPass_Begin(device, graphics) == mrhi_success &&
              mrhiPass_ReadBuffer(device, graphics, buffer, 0, sizeof(words), &bytes) ==
                  mrhi_success &&
              mrhiPass_End(device, graphics) == mrhi_success,
          "recorded");
    mrhiRequestId token = {0};
    mrhiResult status = mrhiDevice_SubmitFrame(device, &token);
    if (status != mrhi_success)
    {
        return status;
    }
    CHECK(mrhiRequest_WaitFrame(device, token, UINT64_C(10000000000)) == mrhi_success, "finished");
    mrhiDeviceNotification record;
    while (mrhiDevice_TakeNotification(device, &record) == mrhi_success)
    {
    }
    uint32_t taken[4] = {0};
    size_t size = 0;
    CHECK(mrhiRequest_TakeReadback(device, bytes, taken, sizeof(taken), &size) == mrhi_success &&
              size == sizeof(words) && memcmp(taken, words, size) == 0,
          "the words back");
    return status;
}

// The code of the device's latest diagnostic, draining them; 0 for none.
static mrhiDiagnosticCode LastDiagnostic(mrhiDevice* device)
{
    mrhiDiagnosticCode code = 0;
    mrhiDiagnostic record;
    while (mrhiDevice_TakeDiagnostic(device, &record) == mrhi_success)
    {
        code = record.code;
    }
    return code;
}

// Fails each begin of the frame in turn: each failed frame is refused for
// want of memory under the command lists budget, with nothing recorded
// into or ended on the failed buffer and nothing submitted, and the next
// frame runs.
static void CheckFailedBegins(mrhiDevice* device, bool timestamps)
{
    mrhiQuerySetId times = {0};
    if (timestamps)
    {
        mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
        def.kind = mrhi_queryTimestamp;
        def.count = 2;
        CHECK(mrhiCreateQuerySet(device, &def, &times) == mrhi_success, "a timestamp set");
    }
    s_begins = 0;
    CHECK(SubmitFrame(device, times) == mrhi_success, "a frame");
    uint32_t begins = s_begins;
    CHECK(begins >= 2, "the frame began a command buffer, and another after its native pass");
    for (uint32_t n = 1; n <= begins; ++n)
    {
        s_begins = 0;
        s_failAt = n;
        s_failed = VK_NULL_HANDLE;
        s_misuses = 0;
        CHECK(SubmitFrame(device, times) == mrhi_errorCapacity, "a failed begin refused");
        CHECK(s_failed != VK_NULL_HANDLE, "the begin failed");
        CHECK(LastDiagnostic(device) == mrhi_diagnosticSubmitCommandLists,
              "the command lists budget named");
        if (s_misuses != 0)
        {
            printf("begin %u of %u: %u calls after it\n", n, begins, s_misuses);
        }
        CHECK(s_misuses == 0, "nothing recorded, ended or submitted after it");
        s_failAt = 0;
        s_failed = VK_NULL_HANDLE;
        CHECK(SubmitFrame(device, times) == mrhi_success, "the next frame runs");
    }
    s_failReset = true;
    CHECK(SubmitFrame(device, times) == mrhi_errorCapacity &&
              LastDiagnostic(device) == mrhi_diagnosticSubmitCommandLists,
          "a failed reset refused under the command lists budget");
    CHECK(SubmitFrame(device, times) == mrhi_success, "the next frame runs");
    CHECK(!timestamps || mrhiDestroyQuerySet(device, times) == mrhi_success, "destroyed");
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
    bool timestamps = false;
    mrhiDevice* device = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success &&
              (device = OpenDevice(instance, &timestamps)) != nullptr,
          "a device on the adopted instance");
    if (device != nullptr)
    {
        CheckFailedBegins(device, timestamps);
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
