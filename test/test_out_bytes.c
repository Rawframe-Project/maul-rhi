// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The structs the library fills for a program: every getter's out struct,
// padding included. Made over memory and out structs that held one byte
// pattern, then over a second, each holds the same bytes, so nothing the
// library's store, its stack or the caller's storage held reaches them.

#include "test_harness.h"

#include "maul-rhi/device.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/shader.h"
#include "maul-rhi/surface.h"
#include "maul-rhi/test.h"
#include "shaders/noop_container.h"

#include <stdalign.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// The byte allocations, the stack and the out structs hold before the
// library writes them.
static int s_pattern;

static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)context;
    // The library asks for no more than malloc's alignment.
    void* block = alignment <= alignof(max_align_t) ? malloc(size) : nullptr;
    if (block != nullptr)
    {
        memset(block, s_pattern, size);
    }
    return block;
}

static void Free(void* block, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    free(block);
}

static const mrhiAllocator kAllocator = {.alloc = Alloc, .free = Free};

// Fills the stack below the caller with the pattern, so that padding of
// a struct the library builds there holds it. Called through a volatile
// pointer so that no compiler inlines it away.
static void FillStack(void)
{
    volatile unsigned char stack[16384];
    for (size_t i = 0; i < sizeof stack; ++i)
    {
        stack[i] = (unsigned char)s_pattern;
    }
}

static void (*volatile s_fillStack)(void) = FillStack;

// A getter's result, called over a stack of the pattern.
#define OVER_PATTERN(call) (s_fillStack(), (call) == mrhi_success)

// What one pass's getters filled.
typedef struct Filled
{
    mrhiInstanceNotification found;
    mrhiInstanceNotification ready;
    mrhiDiagnostic instanceDiagnostic;
    mrhiAdapterInfo adapterInfo;
    mrhiFeatures adapterFeatures;
    mrhiLimits adapterLimits;
    mrhiFormatCaps formatCaps;
    mrhiSurfaceCaps surfaceCaps;
    mrhiSurfaceColor suggested;
    mrhiFeatures deviceFeatures;
    mrhiLimits deviceLimits;
    mrhiDeviceBudgets budgets;
    mrhiDiagnostic deviceDiagnostic;
    mrhiShaderInfo shader;
    mrhiBarrier barriers[4];
    size_t barrierCount;
    mrhiResourcePlan resourcePlan;
    mrhiPassPlan passPlan;
    mrhiFrameUsage usage;
    mrhiDeviceNotification frameDone;
    mrhiDeviceLossReport loss;
} Filled;

static Filled s_filled[2];

static mrhiInstance* Instance(mrhiTestAdapter* adapter, mrhiTestDriverDef* driver, Filled* filled)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &driver->chain;
    def.allocator = kAllocator;
    def.limits.diagnostics = 4;
    *driver = (mrhiTestDriverDef){
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = adapter,
        .adapterCount = 1,
    };
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "the instance");
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    CHECK(mrhiInstance_RequestAdapters(instance, &search, &request) == mrhi_success &&
              OVER_PATTERN(mrhiInstance_TakeNotification(instance, &filled->found)),
          "the adapter found");
    return instance;
}

// The adapter's facts, a surface's caps, and an instance diagnostic.
static void Adapter(mrhiInstance* instance, mrhiAdapterId adapter, Filled* filled)
{
    CHECK(OVER_PATTERN(mrhiAdapter_GetInfo(instance, adapter, &filled->adapterInfo)) &&
              OVER_PATTERN(mrhiAdapter_GetFeatures(instance, adapter, &filled->adapterFeatures)) &&
              OVER_PATTERN(mrhiAdapter_GetLimits(instance, adapter, &filled->adapterLimits)) &&
              OVER_PATTERN(mrhiAdapter_GetFormatCaps(instance, adapter, mrhi_formatRgba8Unorm,
                                                     &filled->formatCaps)),
          "the adapter's facts");
    mrhiSurfaceSourceTest source = {
        .chain = {.next = nullptr, .type = mrhi_structSurfaceSourceTest},
        .presentingAdapters = 1,
    };
    mrhiSurfaceDef surfaceDef = mrhiDefaultSurfaceDef();
    surfaceDef.next = &source.chain;
    mrhiSurfaceId surface;
    CHECK(mrhiCreateSurface(instance, &surfaceDef, &surface) == mrhi_success &&
              OVER_PATTERN(mrhiSurface_GetCaps(instance, surface, adapter, &filled->surfaceCaps)) &&
              OVER_PATTERN(mrhiSuggestSurfaceColor(
                  &filled->surfaceCaps, &filled->surfaceCaps.colors[0], &filled->suggested)),
          "the surface's caps and a color");
    CHECK(mrhiInstance_GetAdapters(instance, nullptr, 1, nullptr) == mrhi_errorInvalid &&
              OVER_PATTERN(mrhiInstance_TakeDiagnostic(instance, &filled->instanceDiagnostic)),
          "a misuse recorded");
}

static mrhiDevice* Device(mrhiInstance* instance, mrhiAdapterId adapter, Filled* filled)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    def.allocator = kAllocator;
    def.budgets.diagnostics = 4;
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success &&
              OVER_PATTERN(mrhiInstance_TakeNotification(instance, &filled->ready)),
          "the device ready");
    CHECK(OVER_PATTERN(mrhiDevice_GetFeatures(device, &filled->deviceFeatures)) &&
              OVER_PATTERN(mrhiDevice_GetLimits(device, &filled->deviceLimits)) &&
              OVER_PATTERN(mrhiDevice_GetBudgets(device, &filled->budgets)),
          "the device's facts");
    CHECK(mrhiDevice_GetFeatures(device, nullptr) == mrhi_errorInvalid &&
              OVER_PATTERN(mrhiDevice_TakeDiagnostic(device, &filled->deviceDiagnostic)),
          "a misuse recorded");
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_noopContainer;
    shaderDef.byteCount = sizeof(s_noopContainer);
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(device, &shaderDef, &shader) == mrhi_success &&
              OVER_PATTERN(mrhiShader_GetInfo(device, shader, &filled->shader)),
          "the shader's info");
    return device;
}

static mrhiPassId Pass(mrhiDevice* device, const mrhiAccess* access)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.neverCull = true;
    def.accesses = access;
    def.accessCount = 1;
    mrhiPassId pass;
    CHECK(mrhiAddPass(device, &def, &pass) == mrhi_success, "a pass");
    return pass;
}

// A declared buffer written and then read, compiled: its barriers and
// plans, and the frame's usage once it is done.
static void Frame(mrhiDevice* device, Filled* filled)
{
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 256;
    mrhiResourceId buffer = {0};
    CHECK(mrhiDevice_BeginFrame(device, &frameDef) == mrhi_success &&
              mrhiDeclareBuffer(device, &bufferDef, &buffer) == mrhi_success,
          "the frame");
    const mrhiAccess write = {.resource = buffer, .kind = mrhi_accessCopyDestination};
    const mrhiAccess read = {.resource = buffer, .kind = mrhi_accessCopySource};
    mrhiPassId writes = Pass(device, &write);
    mrhiPassId reads = Pass(device, &read);
    CHECK(mrhiDevice_CompileFrame(device) == mrhi_success, "compiled");
    filled->barrierCount = 0;
    CHECK(OVER_PATTERN(
              mrhiDevice_GetFrameBarriers(device, filled->barriers, 4, &filled->barrierCount)) &&
              filled->barrierCount > 0 && filled->barrierCount <= 4,
          "the barriers");
    CHECK(OVER_PATTERN(mrhiResource_GetPlan(device, buffer, &filled->resourcePlan)) &&
              OVER_PATTERN(mrhiPass_GetPlan(device, writes, &filled->passPlan)),
          "the plans");
    CHECK(mrhiPass_Begin(device, writes) == mrhi_success &&
              mrhiPass_End(device, writes) == mrhi_success &&
              mrhiPass_Begin(device, reads) == mrhi_success &&
              mrhiPass_End(device, reads) == mrhi_success,
          "the passes recorded");
    mrhiRequestId token;
    CHECK(mrhiDevice_SubmitFrame(device, &token) == mrhi_success &&
              OVER_PATTERN(mrhiDevice_GetFrameUsage(device, &filled->usage)),
          "submitted");
    bool done = false;
    while (!done && OVER_PATTERN(mrhiDevice_TakeNotification(device, &filled->frameDone)))
    {
        done = filled->frameDone.kind == mrhi_deviceFrameDone;
    }
    CHECK(done, "done");
}

// Every getter, over memory, a stack and out structs of the pattern.
static void Fill(int pattern, Filled* filled)
{
    s_pattern = pattern;
    memset(filled, pattern, sizeof(*filled));
    bool lose = false;
    mrhiTestAdapter adapter = {
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete, .vendorId = 7},
        .limits = mrhiDefaultLimits(),
        .loseDevice = &lose,
    };
    memcpy(adapter.info.name, "out", 3);
    adapter.info.nameLength = 3;
    mrhiTestDriverDef driver;
    mrhiInstance* instance = Instance(&adapter, &driver, filled);
    mrhiAdapterId id;
    size_t count = 0;
    CHECK(mrhiInstance_GetAdapters(instance, &id, 1, &count) == mrhi_success && count == 1,
          "one adapter");
    Adapter(instance, id, filled);
    mrhiDevice* device = Device(instance, id, filled);
    Frame(device, filled);
    lose = true;
    mrhiDeviceNotification record;
    while (mrhiDevice_TakeNotification(device, &record) == mrhi_success)
    {
    }
    CHECK(OVER_PATTERN(mrhiDevice_GetLossReport(device, &filled->loss)), "the loss report");
    mrhiDestroyDevice(device);
    mrhiDestroyInstance(instance);
}

// Whether a member holds the same bytes in both passes.
#define SAME(member) (memcmp(&a->member, &b->member, sizeof(a->member)) == 0)

static void TestSameBytes(void)
{
    Fill(0x00, &s_filled[0]);
    Fill(0xFF, &s_filled[1]);
    const Filled* a = &s_filled[0];
    const Filled* b = &s_filled[1];
    CHECK(SAME(found) && SAME(ready), "the instance's notifications");
    CHECK(SAME(instanceDiagnostic) && SAME(deviceDiagnostic), "the diagnostics");
    CHECK(SAME(adapterInfo), "the adapter's info");
    CHECK(SAME(adapterFeatures) && SAME(deviceFeatures), "the features");
    CHECK(SAME(adapterLimits) && SAME(deviceLimits) && SAME(budgets), "the limits and budgets");
    CHECK(SAME(formatCaps), "a format's caps");
    CHECK(SAME(surfaceCaps) && SAME(suggested), "the surface's caps and its suggested color");
    CHECK(SAME(shader), "the shader's info");
    CHECK(a->barrierCount == b->barrierCount &&
              memcmp(a->barriers, b->barriers, a->barrierCount * sizeof(a->barriers[0])) == 0,
          "the frame's barriers");
    CHECK(SAME(resourcePlan) && SAME(passPlan), "the plans");
    CHECK(SAME(usage), "the frame's usage");
    CHECK(SAME(frameDone), "the device's notification");
    CHECK(SAME(loss), "the loss report");
    // The bytes compared are the getters' own, not the patterns left.
    CHECK(a->adapterInfo.vendorId == 7 && a->surfaceCaps.presentable &&
              a->usage.framePassCount == 2,
          "the getters filled them");
}

int main(void)
{
    TestSameBytes();
    return s_failures == 0 ? 0 : 1;
}
