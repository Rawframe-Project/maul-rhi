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
// The surface a hook's instance calls destroy; none by default.
static mrhiSurfaceId s_surface;
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
    (void)alignment;
    (void)context;
    RunHook();
    return malloc(size);
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
    Busy(mrhiInstance_RequestAdapters(s_instance, nullptr, &request));
    Busy(mrhiInstance_TakeNotification(s_instance, &notification));
    Busy(mrhiInstance_TakeDiagnostic(s_instance, &diagnostic));
    Busy(mrhiCreateDevice(s_instance, nullptr, &device, &request));
    mrhiSurfaceId surface = {0};
    Busy(mrhiCreateSurface(s_instance, nullptr, &surface));
    Busy(mrhiDestroySurface(s_instance, s_surface));
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
    Busy(mrhiDevice_SimulateLoss(d));
    Busy(mrhiDevice_TakeDiagnostic(d, &diagnostic));
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
    Busy(mrhiHeap_SetEntry(d, (mrhiHeapId){0}, 0, nullptr));
    Busy(mrhiHeap_ClearEntry(d, (mrhiHeapId){0}, 0));
    Busy(mrhiHeap_SetSampler(d, (mrhiHeapId){0}, 0, (mrhiSamplerId){0}));
    Busy(mrhiHeap_ClearSampler(d, (mrhiHeapId){0}, 0));
    Busy(mrhiDevice_ConfigureSurface(d, nullptr));
    Busy(mrhiSurface_Unconfigure(d, (mrhiSurfaceId){0}));
    Busy(mrhiDevice_BeginFrame(d, nullptr));
    Busy(mrhiDevice_DropFrame(d));
    Busy(mrhiDevice_SubmitFrame(d, &request));
    Busy(mrhiRequest_WaitFrame(d, request, 0));
    Busy(mrhiDevice_TakeNotification(d, &notification));
    Busy(mrhiDevice_WaitNotification(d, 0));
    Busy(mrhiDeclareTexture(d, nullptr, &resource));
    Busy(mrhiDeclareBuffer(d, nullptr, &resource));
    Busy(mrhiImportTexture(d, (mrhiTextureId){0}, &resource));
    Busy(mrhiImportBuffer(d, (mrhiBufferId){0}, &resource));
    Busy(mrhiAcquireSurfaceImage(d, (mrhiSurfaceId){0}, &resource));
    Busy(mrhiResource_Seal(d, resource));
    Busy(mrhiResource_Unseal(d, resource));
    Busy(mrhiAddPass(d, nullptr, nullptr));
    Busy(mrhiDevice_CompileFrame(d));
    Busy(mrhiCreateShader(d, nullptr, nullptr));
    Busy(mrhiDestroyShader(d, (mrhiShaderId){0}));
    Busy(mrhiCreateGraphicsPipeline(d, nullptr, nullptr, nullptr));
    Busy(mrhiDestroyGraphicsPipeline(d, (mrhiGraphicsPipelineId){0}));
    Busy(mrhiCreateComputePipeline(d, nullptr, nullptr, nullptr));
    Busy(mrhiDestroyComputePipeline(d, (mrhiComputePipelineId){0}));
    Busy(mrhiRequest_TakeReadback(d, request, nullptr, 0, &size));
    mrhiDestroyDevice(d);
}

static void BeginFrame(void)
{
    mrhiFrameDef def = mrhiDefaultFrameDef();
    Busy(mrhiDevice_BeginFrame(s_device, &def));
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
        CHECK(mrhiInstance_GetMisuse(s_instance) == INSTANCE_CALLS, "each counted");
        mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
        mrhiRequestId request;
        CHECK(mrhiInstance_RequestAdapters(s_instance, &search, &request) == mrhi_success,
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
    CHECK(mrhiDevice_GetMisuse(s_device) == DEVICE_CALLS, "each counted");
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
    CHECK(mrhiDevice_GetMisuse(s_device) == 1, "counted");
    Close(s_device);
}

// A second device on the instance, with the C library's allocator.
static mrhiDevice* s_second;

// A surface on the instance, presenting on its adapter.
static mrhiSurfaceId MakeSurface(void)
{
    mrhiSurfaceSourceTest source = {
        .chain = {.next = nullptr, .type = mrhi_structSurfaceSourceTest},
        .caps = {.colors = {{.format = mrhi_formatBgra8Unorm}}, .colorCount = 1},
        .presentingAdapters = 1,
    };
    mrhiSurfaceDef def = mrhiDefaultSurfaceDef();
    def.next = &source.chain;
    mrhiSurfaceId surface = {0};
    CHECK(mrhiCreateSurface(s_instance, &def, &surface) == mrhi_success, "the surface");
    return surface;
}

static mrhiSurfaceConfig Config(uint32_t width)
{
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    config.surface = s_surface;
    config.color = (mrhiSurfaceColor){.format = mrhi_formatBgra8Unorm};
    config.width = width;
    config.height = 64;
    return config;
}

// Every function of the second device that would change the instance.
#define SECOND_CALLS 3
static void SecondCalls(void)
{
    mrhiSurfaceConfig config = Config(64);
    Busy(mrhiDevice_ConfigureSurface(s_second, &config));
    Busy(mrhiSurface_Unconfigure(s_second, s_surface));
    mrhiDestroyDevice(s_second);
}

// The calls refused while a device call holds the instance.
static void HeldCalls(void)
{
    InstanceCalls();
    SecondCalls();
}

static void DestroySurface(void)
{
    Busy(mrhiDestroySurface(s_instance, s_surface));
}

// The device, with s_surface configured on it, and the second device.
static void OpenConfigured(void)
{
    s_device = Open();
    s_surface = MakeSurface();
    mrhiSurfaceConfig config = Config(64);
    CHECK(mrhiDevice_ConfigureSurface(s_device, &config) == mrhi_success, "configured");
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    size_t count = 0;
    mrhiRequestId request;
    CHECK(mrhiInstance_GetAdapters(s_instance, &def.adapter, 1, &count) == mrhi_success &&
              mrhiCreateDevice(s_instance, &def, &s_second, &request) == mrhi_success,
          "a second device");
    s_answered = 0;
}

// Checks that a hook ran HeldCalls and each was refused and counted.
static void CheckHeld(void)
{
    CHECK(s_hook == nullptr && s_answered == 0, "every call refused as busy");
    CHECK(mrhiInstance_GetMisuse(s_instance) == INSTANCE_CALLS, "each instance call counted");
    CHECK(mrhiDevice_GetMisuse(s_second) == SECOND_CALLS, "each second device call counted");
}

static void CloseConfigured(void)
{
    mrhiDestroyDevice(s_second);
    s_second = nullptr;
    s_surface = (mrhiSurfaceId){0};
    Close(s_device);
}

// A surface reconfigured: the driver releases the old swapchain's
// records, then allocates the new one's, from the device's allocator.
// The surface's slot is the instance's, so the instance is held, and a
// call that would change it, from destroying the surface to another
// device's configuration, is refused rather than ending the swapchain
// the configure is replacing.
static void TestReconfigure(void)
{
    for (int skip = 0; skip <= 1; ++skip)
    {
        OpenConfigured();
        s_skip = skip;
        s_hook = HeldCalls;
        mrhiSurfaceConfig config = Config(128);
        CHECK(mrhiDevice_ConfigureSurface(s_device, &config) == mrhi_success, "reconfigured");
        CheckHeld();
        CHECK(mrhiDestroySurface(s_instance, s_surface) == mrhi_success,
              "the surface destroyed once the allocator returns");
        CloseConfigured();
    }
}

// Unconfiguring gives the swapchain's records back, the instance held.
static void TestUnconfigure(void)
{
    OpenConfigured();
    s_hook = HeldCalls;
    CHECK(mrhiSurface_Unconfigure(s_device, s_surface) == mrhi_success, "unconfigured");
    CheckHeld();
    CloseConfigured();
}

// Destroying a surface ends its configuration, which changes the device
// it is configured on: refused while that device is in its allocator,
// whatever the call.
static void TestBusyConfiguredDevice(void)
{
    OpenConfigured();
    s_hook = DestroySurface;
    mrhiHeapDef def = mrhiDefaultHeapDef();
    def.entries = 16;
    mrhiHeapId heap = {0};
    CHECK(mrhiCreateHeap(s_device, &def, &heap) == mrhi_success, "the heap made");
    CHECK(s_hook == nullptr && s_answered == 0, "the surface's destruction refused");
    CHECK(mrhiInstance_GetMisuse(s_instance) == 1, "counted on the instance");
    CHECK(mrhiSurface_Unconfigure(s_device, s_surface) == mrhi_success, "still configured");
    CloseConfigured();
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

// The device's answer, configurations and count are the instance's, so
// the instance is held while the device's driver goes back to the
// allocator, after the swapchain's records.
static void TestDestroyHeld(void)
{
    OpenConfigured();
    s_skip = 1;
    s_hook = HeldCalls;
    mrhiDestroyDevice(s_device);
    CheckHeld();
    mrhiDestroyDevice(s_second);
    mrhiDestroyInstance(s_instance);
    ResetAdapter();
}

int main(void)
{
    TestDeviceCreation();
    TestHeap();
    TestShader();
    TestReconfigure();
    TestUnconfigure();
    TestBusyConfiguredDevice();
    TestDestroy();
    TestDestroyHeld();
    return s_failures == 0 ? 0 : 1;
}
