// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Calls from inside an allocator: the instance and the device refuse
// each function that would change them with mrhi_errorBusy, one misuse
// counted, while their allocator runs, and take calls again once it
// returns.

#include "test_device_setup.h"

#include "maul-rhi/device.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/heap.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/shader.h"
#include "maul-rhi/surface.h"
#include "shaders/noop_container.h"

#include <stdlib.h>

// The program code an allocation or release runs, once, after s_skip
// others; NULL for none.
static void (*s_hook)(void);
static int s_skip;
static mrhiDevice* s_device;
// What a hook's calls returned that was not mrhi_errorBusy.
static int s_answered;

static void RunHook(void)
{
    if (s_skip > 0)
    {
        --s_skip;
        return;
    }
    void (*hook)(void) = s_hook;
    s_hook = nullptr;
    if (hook != nullptr)
    {
        hook();
    }
}

static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)context;
    RunHook();
    return aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    RunHook();
    free(memory);
}

static const mrhiAllocator kAllocator = {.alloc = Alloc, .free = Free};

static void Busy(mrhiResult result)
{
    s_answered += result == mrhi_errorBusy ? 0 : 1;
}

// Every function that would change the instance, called with what it
// would refuse anyway: the busy refusal comes first.
#define INSTANCE_CALLS 7
static void InstanceCalls(void)
{
    mrhiRequestId request;
    mrhiInstanceNotification notification;
    mrhiDiagnostic diagnostic;
    mrhiDevice* device = nullptr;
    mrhiSurfaceId surface = {0};
    Busy(mrhiRequestAdapters(s_instance, nullptr, &request));
    Busy(mrhiNextInstanceNotification(s_instance, &notification));
    Busy(mrhiNextInstanceDiagnostic(s_instance, &diagnostic));
    Busy(mrhiCreateDevice(s_instance, nullptr, &device, &request));
    Busy(mrhiCreateSurface(s_instance, nullptr, &surface));
    Busy(mrhiDestroySurface(s_instance, surface));
    mrhiDestroyInstance(s_instance);
}

// Every function that would change the device, as InstanceCalls.
#define DEVICE_CALLS 43
static void DeviceCalls(void)
{
    mrhiDevice* d = s_device;
    mrhiRequestId request = {0};
    mrhiResourceId resource = {0};
    mrhiDiagnostic diagnostic;
    mrhiDeviceNotification notification;
    size_t size = 0;
    Busy(mrhiSimulateDeviceLoss(d));
    Busy(mrhiNextDeviceDiagnostic(d, &diagnostic));
    Busy(mrhiCreateSampler(d, nullptr, nullptr));
    Busy(mrhiDestroySampler(d, (mrhiSamplerId){0}));
    Busy(mrhiCreateBuffer(d, nullptr, nullptr));
    Busy(mrhiDestroyBuffer(d, (mrhiBufferId){0}));
    Busy(mrhiCreateTexture(d, nullptr, nullptr));
    Busy(mrhiDestroyTexture(d, (mrhiTextureId){0}));
    Busy(mrhiCreateView(d, nullptr, nullptr));
    Busy(mrhiDestroyView(d, (mrhiViewId){0}));
    Busy(mrhiCreateQuerySet(d, nullptr, nullptr));
    Busy(mrhiDestroyQuerySet(d, (mrhiQuerySetId){0}));
    Busy(mrhiCreateHeap(d, nullptr, nullptr));
    Busy(mrhiDestroyHeap(d, (mrhiHeapId){0}));
    Busy(mrhiSetHeapEntry(d, (mrhiHeapId){0}, 0, nullptr));
    Busy(mrhiClearHeapEntry(d, (mrhiHeapId){0}, 0));
    Busy(mrhiSetHeapSampler(d, (mrhiHeapId){0}, 0, (mrhiSamplerId){0}));
    Busy(mrhiClearHeapSampler(d, (mrhiHeapId){0}, 0));
    Busy(mrhiConfigureSurface(d, nullptr));
    Busy(mrhiUnconfigureSurface(d, (mrhiSurfaceId){0}));
    Busy(mrhiBeginFrame(d, nullptr));
    Busy(mrhiDropFrame(d));
    Busy(mrhiSubmitFrame(d, &request));
    Busy(mrhiWaitFrame(d, request, 0));
    Busy(mrhiNextDeviceNotification(d, &notification));
    Busy(mrhiWaitDeviceNotification(d, 0));
    Busy(mrhiDeclareTexture(d, nullptr, &resource));
    Busy(mrhiDeclareBuffer(d, nullptr, &resource));
    Busy(mrhiImportTexture(d, (mrhiTextureId){0}, &resource));
    Busy(mrhiImportBuffer(d, (mrhiBufferId){0}, &resource));
    Busy(mrhiAcquireSurfaceImage(d, (mrhiSurfaceId){0}, &resource));
    Busy(mrhiSealResource(d, resource));
    Busy(mrhiUnsealResource(d, resource));
    Busy(mrhiAddPass(d, nullptr, nullptr));
    Busy(mrhiCompileFrame(d));
    Busy(mrhiCreateShader(d, nullptr, nullptr));
    Busy(mrhiDestroyShader(d, (mrhiShaderId){0}));
    Busy(mrhiCreateGraphicsPipeline(d, nullptr, nullptr, nullptr));
    Busy(mrhiDestroyGraphicsPipeline(d, (mrhiGraphicsPipelineId){0}));
    Busy(mrhiCreateComputePipeline(d, nullptr, nullptr, nullptr));
    Busy(mrhiDestroyComputePipeline(d, (mrhiComputePipelineId){0}));
    Busy(mrhiTakeReadback(d, request, nullptr, 0, &size));
    mrhiDestroyDevice(d);
}

static void BeginFrame(void)
{
    mrhiFrameDef def = mrhiDefaultFrameDef();
    Busy(mrhiBeginFrame(s_device, &def));
}

static void CreateBuffer(void)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = 64;
    def.usage = mrhi_bufferStorage;
    mrhiBufferId buffer;
    Busy(mrhiCreateBuffer(s_device, &def, &buffer));
}

// A device whose def gives the allocator, on an adapter with heaps.
static mrhiDevice* Open(void)
{
    ResetAdapter();
    s_adapter.features.bindlessSampling = true;
    s_adapter.limits.heapSize = 64;
    s_adapter.limits.samplerHeapSize = 16;
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.allocator = kAllocator;
    def.features.bindlessSampling = true;
    def.limits.heapSize = 64;
    def.limits.samplerHeapSize = 16;
    return OpenWith(def, true);
}

// The device's block, then its driver's, come from the def's allocator
// while the instance makes the device: the instance is busy for both.
static void TestDeviceCreation(void)
{
    for (int skip = 0; skip <= 1; ++skip)
    {
        s_answered = 0;
        s_skip = skip;
        s_hook = InstanceCalls;
        s_device = Open();
        CHECK(s_hook == nullptr && s_answered == 0, "every instance call refused as busy");
        CHECK(mrhiGetInstanceMisuse(s_instance) == INSTANCE_CALLS, "each counted");
        mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
        mrhiRequestId request;
        CHECK(mrhiRequestAdapters(s_instance, &search, &request) == mrhi_success,
              "taken again once the allocator returns");
        Close(s_device);
    }
}

// A heap's table comes from the device's allocator, and goes back to it.
static void TestHeap(void)
{
    s_device = Open();
    s_answered = 0;
    s_hook = DeviceCalls;
    mrhiHeapDef def = mrhiDefaultHeapDef();
    def.entries = 16;
    def.samplers = 4;
    mrhiHeapId heap = {0};
    CHECK(mrhiCreateHeap(s_device, &def, &heap) == mrhi_success, "the heap made");
    CHECK(s_hook == nullptr && s_answered == 0, "every device call refused as busy");
    CHECK(mrhiGetDeviceMisuse(s_device) == DEVICE_CALLS, "each counted");
    s_hook = CreateBuffer;
    CHECK(mrhiDestroyHeap(s_device, heap) == mrhi_success, "the heap destroyed");
    CHECK(s_hook == nullptr && s_answered == 0, "a call while its table goes back refused");
    CreateBuffer();
    CHECK(s_answered == 1, "a buffer made once the allocator returns");
    Close(s_device);
}

// A shader's reflection comes from the device's allocator.
static void TestShader(void)
{
    s_device = Open();
    s_answered = 0;
    s_hook = BeginFrame;
    mrhiShaderDef def = mrhiDefaultShaderDef();
    def.bytes = s_noopContainer;
    def.byteCount = sizeof(s_noopContainer);
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(s_device, &def, &shader) == mrhi_success, "the shader made");
    CHECK(s_hook == nullptr && s_answered == 0, "a frame begun from inside refused");
    CHECK(mrhiGetDeviceMisuse(s_device) == 1, "counted");
    Close(s_device);
}

// The device's driver goes back to the device's allocator as the device
// is destroyed, and a call then is refused.
static void TestDestroy(void)
{
    s_device = Open();
    s_answered = 0;
    s_hook = CreateBuffer;
    mrhiInstance* instance = s_instance;
    mrhiDestroyDevice(s_device);
    CHECK(s_hook == nullptr && s_answered == 0, "a buffer made while the device goes refused");
    mrhiDestroyInstance(instance);
    ResetAdapter();
}

int main(void)
{
    TestDeviceCreation();
    TestHeap();
    TestShader();
    TestDestroy();
    return s_failures == 0 ? 0 : 1;
}
