// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Surface images in frames on a test driver device: acquiring one, its
// texture, the answers without an image, the pass that draws it kept,
// its end in the present state, presenting at submission, giving it back
// when the frame is dropped, and no reconfiguring while a frame holds it.

#include "device_core.h"
#include "test_device_setup.h"

static mrhiTestFrameLog s_log;
static mrhiResult s_outcome;
static mrhiDevice* s_device;
static mrhiSurfaceId s_surface;

// A test surface presenting on the first adapter in BGRA8, with copies.
static mrhiSurfaceId MakeSurface(void)
{
    mrhiSurfaceSourceTest source = {
        .chain = {.next = nullptr, .type = mrhi_structSurfaceSourceTest},
        .caps =
            {
                .colors = {{.format = mrhi_formatBgra8Unorm}},
                .colorCount = 1,
                .usages = mrhi_textureCopySource,
            },
        .presentingAdapters = 1,
    };
    mrhiSurfaceDef def = mrhiDefaultSurfaceDef();
    def.next = &source.chain;
    mrhiSurfaceId surface = {0};
    CHECK(mrhiCreateSurface(s_instance, &def, &surface) == mrhi_success, "a surface");
    return surface;
}

static mrhiSurfaceConfig Config(mrhiSurfaceId surface)
{
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    config.surface = surface;
    config.color = (mrhiSurfaceColor){.format = mrhi_formatBgra8Unorm};
    config.viewFormats[0] = mrhi_formatBgra8UnormSrgb;
    config.usage |= mrhi_textureCopySource;
    config.width = 1280;
    config.height = 720;
    return config;
}

// Opens a device with a configured surface, its acquires answering
// s_outcome.
static void Open(void)
{
    s_log = (mrhiTestFrameLog){0};
    s_outcome = mrhi_success;
    s_adapter.frameLog = &s_log;
    s_adapter.acquireOutcome = &s_outcome;
    s_device = OpenWith(mrhiDefaultDeviceDef(), true);
    s_surface = MakeSurface();
    mrhiSurfaceConfig config = Config(s_surface);
    CHECK(mrhiDevice_ConfigureSurface(s_device, &config) == mrhi_success, "configured");
}

static void CloseDevice(void)
{
    CHECK(mrhiDestroySurface(s_instance, s_surface) == mrhi_success, "the surface destroyed");
    Close(s_device);
    s_adapter.frameLog = nullptr;
    s_adapter.acquireOutcome = nullptr;
}

static void BeginFrame(void)
{
    mrhiFrameDef def = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &def) == mrhi_success, "begun");
}

// A pass drawing into an image, kept only for it.
static mrhiPassId Draw(mrhiResourceId image)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){.resource = image, .load = mrhi_loadClear};
    def.colorTargetCount = 1;
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success, "a pass drawing it");
    return pass;
}

static void TestAcquire(void)
{
    Open();
    mrhiResourceId image = {0};
    CHECK(mrhiAcquireSurfaceImage(s_device, s_surface, &image) == mrhi_errorState, "no frame");
    BeginFrame();
    mrhiSurfaceId other = MakeSurface();
    CHECK(mrhiAcquireSurfaceImage(s_device, other, &image) == mrhi_errorState, "not configured");
    CHECK(mrhiAcquireSurfaceImage(s_device, (mrhiSurfaceId){0}, &image) == mrhi_errorStale,
          "no surface");
    CHECK(mrhiAcquireSurfaceImage(s_device, s_surface, nullptr) == mrhi_errorInvalid &&
              mrhiDevice_GetMisuse(s_device) == 1,
          "no out, counted");
    CHECK(mrhiAcquireSurfaceImage(nullptr, s_surface, &image) == mrhi_errorInvalid, "no device");
    CHECK(mrhiAcquireSurfaceImage(s_device, s_surface, &image) == mrhi_success && image.index1 != 0,
          "an image");
    mrhiResourceId again = {0};
    CHECK(mrhiAcquireSurfaceImage(s_device, s_surface, &again) == mrhi_success &&
              again.index1 == image.index1 && again.generation == image.generation,
          "the same image again");
    mrhiSurfaceConfig config = Config(s_surface);
    CHECK(mrhiDevice_ConfigureSurface(s_device, &config) == mrhi_errorState &&
              mrhiSurface_Unconfigure(s_device, s_surface) == mrhi_errorState,
          "no reconfiguring while the frame holds its image");
    mrhiAccess read = {
        .resource = image,
        .kind = mrhi_accessCopySource,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
    mrhiPassDef def = mrhiDefaultPassDef();
    def.accesses = &read;
    def.accessCount = 1;
    def.neverCull = true;
    mrhiPassId pass;
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "read before drawn");
    mrhiAccess stored = {
        .resource = image,
        .kind = mrhi_accessStorageWrite,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
    def.accesses = &stored;
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "a usage not configured");
    mrhiPassId draw = Draw(image);
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success, "compiled");
    bool kept = false;
    CHECK(mrhiPass_IsKept(s_device, draw, &kept) == mrhi_success && kept, "kept for the image");
    mrhiPassPlan passPlan;
    CHECK(mrhiPass_GetPlan(s_device, draw, &passPlan) == mrhi_success &&
              passPlan.colorStores[0] == mrhi_storeKeep,
          "what it draws kept for presenting");
    CHECK(mrhiAcquireSurfaceImage(s_device, s_surface, &again) == mrhi_errorState,
          "no acquiring once compiled");
    mrhiBarrier barriers[8];
    size_t count = 0;
    CHECK(mrhiDevice_GetFrameBarriers(s_device, barriers, 8, &count) == mrhi_success &&
              count == 2 && barriers[0].pass.index1 == draw.index1 &&
              barriers[0].before == mrhi_stateUndefined &&
              barriers[0].after == mrhi_stateColorTarget,
          "undefined before its pass");
    CHECK(barriers[1].pass.index1 == 0 && barriers[1].resource.index1 == image.index1 &&
              barriers[1].before == mrhi_stateColorTarget && barriers[1].after == mrhi_statePresent,
          "ready to present at the frame's end");
    mrhiResourcePlan plan;
    CHECK(mrhiResource_GetPlan(s_device, image, &plan) == mrhi_success && !plan.transient &&
              plan.memoryBytes == 0,
          "never placed in the frame's memory");
    mrhiRequestId token;
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_success, "submitted");
    CHECK(s_log.frames == 1 && s_log.presented == 1 && s_log.passes == 1, "presented");
    CHECK(mrhiDevice_ConfigureSurface(s_device, &config) == mrhi_success, "reconfigured after");
    BeginFrame();
    mrhiResourceId next = {0};
    CHECK(mrhiAcquireSurfaceImage(s_device, s_surface, &next) == mrhi_success && next.index1 != 0,
          "the next image");
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success, "dropped, the image given back");
    BeginFrame();
    CHECK(mrhiAcquireSurfaceImage(s_device, s_surface, &next) == mrhi_success, "one more");
    Draw(next);
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_success && s_log.presented == 1,
          "presented, the dropped one not");
    CHECK(mrhiDestroySurface(s_instance, other) == mrhi_success, "the other destroyed");
    CloseDevice();
}

// The answers without an image, and suboptimal with one.
static void TestOutcomes(void)
{
    Open();
    BeginFrame();
    mrhiResourceId image = {0};
    s_outcome = mrhi_suboptimal;
    CHECK(mrhiAcquireSurfaceImage(s_device, s_surface, &image) == mrhi_suboptimal &&
              image.index1 != 0,
          "suboptimal, with an image");
    s_outcome = mrhi_success;
    CHECK(mrhiAcquireSurfaceImage(s_device, s_surface, &image) == mrhi_suboptimal,
          "the same answer again");
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success, "dropped");
    BeginFrame();
    s_outcome = mrhi_occluded;
    image = (mrhiResourceId){7, 7};
    CHECK(mrhiAcquireSurfaceImage(s_device, s_surface, &image) == mrhi_occluded &&
              image.index1 == 0 && image.generation == 0,
          "occluded, no image");
    image = (mrhiResourceId){7, 7};
    CHECK(mrhiAcquireSurfaceImage(s_device, s_surface, &image) == mrhi_occluded &&
              image.index1 == 0 && image.generation == 0,
          "again, no image");
    mrhiSurfaceConfig config = Config(s_surface);
    CHECK(mrhiDevice_ConfigureSurface(s_device, &config) == mrhi_success,
          "reconfigured, no image held");
    s_outcome = mrhi_success;
    CHECK(mrhiAcquireSurfaceImage(s_device, s_surface, &image) == mrhi_success && image.index1 != 0,
          "an image of the new configuration");
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success, "dropped");
    BeginFrame();
    s_outcome = mrhi_errorOutOfDate;
    CHECK(mrhiAcquireSurfaceImage(s_device, s_surface, &image) == mrhi_errorOutOfDate &&
              image.index1 == 0,
          "out of date");
    mrhiRequestId token;
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_success && s_log.presented == 0,
          "a frame without its image still submitted");
    BeginFrame();
    s_outcome = mrhi_errorDeviceLost;
    CHECK(mrhiAcquireSurfaceImage(s_device, s_surface, &image) == mrhi_errorDeviceLost,
          "the device lost");
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success, "dropped");
    CHECK(mrhiDevice_GetMisuse(s_device) == 0, "no misuse");
    CloseDevice();
}

// A device destroyed with its frame open gives its image back; the test
// driver traps otherwise.
static void TestDestroyedOpen(void)
{
    Open();
    BeginFrame();
    mrhiResourceId image = {0};
    CHECK(mrhiAcquireSurfaceImage(s_device, s_surface, &image) == mrhi_success, "an image");
    CloseDevice();
}

// A full frame refuses before asking the driver for an image.
static void TestCapacity(void)
{
    s_log = (mrhiTestFrameLog){0};
    s_outcome = mrhi_success;
    s_adapter.acquireOutcome = &s_outcome;
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.budgets.frameResources = 1;
    s_device = OpenWith(deviceDef, true);
    s_surface = MakeSurface();
    mrhiSurfaceConfig config = Config(s_surface);
    CHECK(mrhiDevice_ConfigureSurface(s_device, &config) == mrhi_success, "configured");
    BeginFrame();
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 4;
    mrhiResourceId buffer;
    CHECK(mrhiDeclareBuffer(s_device, &bufferDef, &buffer) == mrhi_success, "the one resource");
    mrhiResourceId image = {0};
    CHECK(mrhiAcquireSurfaceImage(s_device, s_surface, &image) == mrhi_errorCapacity &&
              image.index1 == 0,
          "no room");
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success, "dropped");
    CloseDevice();
}

int main(void)
{
    ResetAdapter();
    TestAcquire();
    TestOutcomes();
    TestDestroyedOpen();
    TestCapacity();
    return s_failures == 0 ? 0 : 1;
}
