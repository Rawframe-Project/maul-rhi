// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The compile's plan on a test driver device: barriers per mip, layer
// and plane, in the order they run; states carried by imported objects
// between frames and unified at a frame's end; each resource's usages,
// transience and kept passes.

#include "test_device_setup.h"

static mrhiDevice* s_device;

static void Begin(void)
{
    mrhiFrameDef def = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &def) == mrhi_success, "begun");
}

static void Submit(void)
{
    mrhiRequestId token;
    CHECK(mrhiSubmitFrame(s_device, &token) == mrhi_success, "submitted");
}

static mrhiResourceId Declare(mrhiFormat format, uint32_t mips, uint32_t samples)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = format;
    def.width = 64;
    def.height = 64;
    def.mipLevels = mips;
    def.sampleCount = samples;
    mrhiResourceId resource = {0};
    CHECK(mrhiDeclareTexture(s_device, &def, &resource) == mrhi_success, "declared");
    return resource;
}

static mrhiTextureId MakeTexture(uint32_t mips, mrhiTextureUsage usage)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = 64;
    def.height = 64;
    def.mipLevels = mips;
    def.usage = usage;
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(s_device, &def, &texture) == mrhi_success, "a device texture");
    return texture;
}

static mrhiAccess Access(mrhiResourceId resource, mrhiAccessKind kind, uint32_t baseMip,
                         uint32_t mips)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.baseMip = baseMip, .mipCount = mips, .layerCount = MRHI_REMAINING},
    };
}

static mrhiPassId AddPass(mrhiPassDef def)
{
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success, "a pass");
    return pass;
}

static mrhiPassId Target(mrhiResourceId target, mrhiLoadOp load, const mrhiAccess* accesses,
                         uint32_t count)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){.resource = target, .load = load};
    def.colorTargetCount = 1;
    def.accesses = accesses;
    def.accessCount = count;
    return AddPass(def);
}

static mrhiPassId Compute(const mrhiAccess* accesses, uint32_t count, bool neverCull)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.accesses = accesses;
    def.accessCount = count;
    def.neverCull = neverCull;
    return AddPass(def);
}

// The compiled frame's barriers.
static mrhiBarrier s_barriers[32];
static size_t s_count;

static void ReadBarriers(void)
{
    CHECK(mrhiGetFrameBarriers(s_device, s_barriers, 32, &s_count) == mrhi_success, "read");
    CHECK(s_count <= 32, "room for them");
}

// Whether barrier i is the one described.
static bool Is(size_t i, mrhiPassId pass, mrhiResourceId resource, uint32_t baseMip, uint32_t mips,
               mrhiTextureAspect aspect, mrhiResourceState before, mrhiResourceState after)
{
    const mrhiBarrier* barrier = &s_barriers[i];
    return i < s_count && barrier->pass.index1 == pass.index1 &&
           barrier->pass.generation == pass.generation &&
           barrier->resource.index1 == resource.index1 && barrier->range.baseMip == baseMip &&
           barrier->range.mipCount == mips && barrier->range.aspect == aspect &&
           barrier->before == before && barrier->after == after;
}

static mrhiResourcePlan Plan(mrhiResourceId resource)
{
    mrhiResourcePlan plan = {0};
    CHECK(mrhiGetResourcePlan(s_device, resource, &plan) == mrhi_success, "a plan");
    return plan;
}

static const mrhiPassId kEnd = {0, 0};
static const mrhiTextureAspect kAll = mrhi_aspectAll;

// Render, sample, present; the window's state carried between frames.
static void TestRenderThenSample(void)
{
    mrhiTextureId windowTexture = MakeTexture(1, mrhi_textureRenderTarget | mrhi_textureSampled);
    Begin();
    mrhiResourceId scene = Declare(mrhi_formatRgba16Float, 1, 1);
    mrhiResourceId window;
    CHECK(mrhiImportTexture(s_device, windowTexture, &window) == mrhi_success, "imported");
    mrhiPassId draw = Target(scene, mrhi_loadClear, nullptr, 0);
    mrhiAccess sample = Access(scene, mrhi_accessSampled, 0, 1);
    mrhiPassId compose = Target(window, mrhi_loadDiscard, &sample, 1);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    ReadBarriers();
    CHECK(s_count == 3, "three barriers");
    CHECK(Is(0, draw, scene, 0, 1, kAll, mrhi_stateUndefined, mrhi_stateColorTarget),
          "the scene made a target");
    CHECK(Is(1, compose, scene, 0, 1, kAll, mrhi_stateColorTarget, mrhi_stateSampled),
          "then sampled");
    CHECK(Is(2, compose, window, 0, 1, kAll, mrhi_stateUndefined, mrhi_stateColorTarget),
          "the window made a target");
    mrhiResourcePlan plan = Plan(scene);
    CHECK(plan.usage == (mrhi_textureRenderTarget | mrhi_textureSampled) && !plan.transient,
          "rendered and sampled");
    CHECK(!Plan(window).transient, "an imported target is never transient");
    CHECK(plan.firstPass.index1 == draw.index1 && plan.lastPass.index1 == compose.index1 &&
              plan.lastPass.generation == compose.generation,
          "its passes");
    Submit();
    Begin();
    CHECK(mrhiImportTexture(s_device, windowTexture, &window) == mrhi_success, "again");
    sample = Access(window, mrhi_accessSampled, 0, 1);
    mrhiPassId read = Compute(&sample, 1, true);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    ReadBarriers();
    CHECK(s_count == 1 && Is(0, read, window, 0, 1, kAll, mrhi_stateColorTarget, mrhi_stateSampled),
          "from the state the last frame left");
    Submit();
    Begin();
    CHECK(mrhiImportTexture(s_device, windowTexture, &window) == mrhi_success, "a third time");
    mrhiPassId again = Target(window, mrhi_loadKeep, nullptr, 0);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    ReadBarriers();
    CHECK(s_count == 1 &&
              Is(0, again, window, 0, 1, kAll, mrhi_stateSampled, mrhi_stateColorTarget),
          "sampled last frame");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    Begin();
    CHECK(mrhiImportTexture(s_device, windowTexture, &window) == mrhi_success, "after a drop");
    again = Target(window, mrhi_loadKeep, nullptr, 0);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    ReadBarriers();
    CHECK(s_count == 1 &&
              Is(0, again, window, 0, 1, kAll, mrhi_stateSampled, mrhi_stateColorTarget),
          "a dropped frame leaves no state");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    CHECK(mrhiDestroyTexture(s_device, windowTexture) == mrhi_success, "done");
}

// Each mip written from the one before.
static void TestMipChain(void)
{
    Begin();
    mrhiResourceId mips = Declare(mrhi_formatRgba16Float, 3, 1);
    mrhiAccess first = Access(mips, mrhi_accessStorageWrite, 0, 1);
    mrhiPassId p1 = Compute(&first, 1, false);
    mrhiAccess second[2] = {Access(mips, mrhi_accessSampled, 0, 1),
                            Access(mips, mrhi_accessStorageWrite, 1, 1)};
    mrhiPassId p2 = Compute(second, 2, false);
    mrhiAccess third[2] = {Access(mips, mrhi_accessSampled, 1, 1),
                           Access(mips, mrhi_accessStorageWrite, 2, 1)};
    mrhiPassId p3 = Compute(third, 2, false);
    mrhiAccess all = Access(mips, mrhi_accessSampled, 0, MRHI_REMAINING);
    mrhiPassId p4 = Compute(&all, 1, true);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    ReadBarriers();
    const mrhiResourceState undefined = mrhi_stateUndefined;
    const mrhiResourceState write = mrhi_stateStorageWrite;
    const mrhiResourceState sampled = mrhi_stateSampled;
    CHECK(s_count == 6, "six barriers");
    CHECK(Is(0, p1, mips, 0, 1, kAll, undefined, write), "mip 0 written");
    CHECK(Is(1, p2, mips, 0, 1, kAll, write, sampled), "mip 0 read");
    CHECK(Is(2, p2, mips, 1, 1, kAll, undefined, write), "mip 1 written");
    CHECK(Is(3, p3, mips, 1, 1, kAll, write, sampled), "mip 1 read");
    CHECK(Is(4, p3, mips, 2, 1, kAll, undefined, write), "mip 2 written");
    CHECK(Is(5, p4, mips, 2, 1, kAll, write, sampled), "only mip 2 changes for the last read");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
}

// Buffers: no barrier before a first use or between reads.
static void TestBuffers(void)
{
    Begin();
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = 256;
    mrhiResourceId buffer;
    CHECK(mrhiDeclareBuffer(s_device, &def, &buffer) == mrhi_success, "declared");
    mrhiAccess upload = Access(buffer, mrhi_accessCopyDestination, 0, 1);
    mrhiPassDef transfer = mrhiDefaultPassDef();
    transfer.passClass = mrhi_passTransfer;
    transfer.accesses = &upload;
    transfer.accessCount = 1;
    AddPass(transfer);
    mrhiAccess read = Access(buffer, mrhi_accessStorageRead, 0, 1);
    mrhiPassId compute = Compute(&read, 1, true);
    mrhiAccess vertices = Access(buffer, mrhi_accessVertex, 0, 1);
    Compute(&vertices, 1, true);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    ReadBarriers();
    CHECK(s_count == 1 &&
              Is(0, compute, buffer, 0, 1, kAll, mrhi_stateCopyDestination, mrhi_stateStorageRead),
          "only after the write");
    mrhiResourcePlan plan = Plan(buffer);
    CHECK(plan.usage == (mrhi_bufferCopyDestination | mrhi_bufferStorage | mrhi_bufferVertex),
          "its usages");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
}

// Writes after writes of the same kind, and writes after reads.
static void TestWritesAfter(void)
{
    Begin();
    mrhiResourceId storage = Declare(mrhi_formatRgba16Float, 1, 1);
    mrhiResourceId color = Declare(mrhi_formatRgba8Unorm, 1, 1);
    mrhiResourceId depth = Declare(mrhi_formatDepth32Float, 1, 1);
    mrhiResourceId samples = Declare(mrhi_formatRgba8Unorm, 1, 4);
    mrhiResourceId resolved = Declare(mrhi_formatRgba8Unorm, 1, 1);
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 64;
    mrhiResourceId buffer;
    CHECK(mrhiDeclareBuffer(s_device, &bufferDef, &buffer) == mrhi_success, "a buffer");
    mrhiPassId passes[4];
    for (int i = 0; i < 2; ++i)
    {
        mrhiAccess writes[2] = {Access(storage, mrhi_accessStorageWrite, 0, 1),
                                Access(buffer, mrhi_accessStorageWrite, 0, 1)};
        mrhiPassDef def = mrhiDefaultPassDef();
        def.accesses = writes;
        def.accessCount = 2;
        def.colorTargets[0] = (mrhiColorTarget){
            .resource = color,
            .load = i == 0 ? mrhi_loadClear : mrhi_loadKeep,
        };
        def.colorTargetCount = 1;
        def.depthTarget = (mrhiDepthTarget){
            .resource = depth,
            .depthLoad = i == 0 ? mrhi_loadClear : mrhi_loadKeep,
        };
        def.neverCull = true;
        passes[i] = AddPass(def);
    }
    for (int i = 2; i < 4; ++i)
    {
        mrhiPassDef def = mrhiDefaultPassDef();
        def.colorTargets[0] = (mrhiColorTarget){
            .resource = samples,
            .load = mrhi_loadClear,
            .resolve = resolved,
        };
        def.colorTargetCount = 1;
        def.neverCull = true;
        passes[i] = AddPass(def);
    }
    mrhiAccess read = Access(buffer, mrhi_accessUniform, 0, 1);
    mrhiPassId reader = Compute(&read, 1, true);
    mrhiAccess rewrite = Access(buffer, mrhi_accessCopyDestination, 0, 1);
    mrhiPassDef copy = mrhiDefaultPassDef();
    copy.passClass = mrhi_passTransfer;
    copy.accesses = &rewrite;
    copy.accessCount = 1;
    copy.neverCull = true;
    mrhiPassId last = AddPass(copy);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    ReadBarriers();
    CHECK(s_count == 13, "thirteen barriers");
    CHECK(Is(3, passes[1], storage, 0, 1, kAll, mrhi_stateStorageWrite, mrhi_stateStorageWrite),
          "storage after storage");
    CHECK(Is(4, passes[1], color, 0, 1, kAll, mrhi_stateColorTarget, mrhi_stateColorTarget),
          "a target after a target");
    CHECK(Is(5, passes[1], depth, 0, 1, kAll, mrhi_stateDepthTarget, mrhi_stateDepthTarget),
          "depth after depth");
    CHECK(Is(6, passes[1], buffer, 0, 1, kAll, mrhi_stateStorageWrite, mrhi_stateStorageWrite),
          "a buffer's storage after storage");
    CHECK(Is(9, passes[3], samples, 0, 1, kAll, mrhi_stateColorTarget, mrhi_stateColorTarget),
          "samples after samples");
    CHECK(Is(10, passes[3], resolved, 0, 1, kAll, mrhi_stateResolve, mrhi_stateResolve),
          "a resolve after a resolve");
    CHECK(Is(11, reader, buffer, 0, 1, kAll, mrhi_stateStorageWrite, mrhi_stateUniform),
          "a read after a write");
    CHECK(Is(12, last, buffer, 0, 1, kAll, mrhi_stateUniform, mrhi_stateCopyDestination),
          "a write after a read");
    CHECK(!Plan(storage).transient, "storage alone is not transient");
    CHECK(!Plan(resolved).transient, "a resolve alone is not transient");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
}

// Layers of an array and slices of a volume.
static void TestLayers(void)
{
    mrhiTextureDef volumeDef = mrhiDefaultTextureDef();
    volumeDef.kind = mrhi_texture3d;
    volumeDef.format = mrhi_formatRgba8Unorm;
    volumeDef.width = 16;
    volumeDef.height = 16;
    volumeDef.depthOrLayers = 8;
    volumeDef.usage = mrhi_textureSampled | mrhi_textureStorage;
    mrhiTextureId volume;
    CHECK(mrhiCreateTexture(s_device, &volumeDef, &volume) == mrhi_success, "a volume");
    Begin();
    mrhiTextureDef arrayDef = mrhiDefaultTextureDef();
    arrayDef.kind = mrhi_texture2dArray;
    arrayDef.format = mrhi_formatRgba8Unorm;
    arrayDef.width = 16;
    arrayDef.height = 16;
    arrayDef.depthOrLayers = 4;
    mrhiResourceId array;
    CHECK(mrhiDeclareTexture(s_device, &arrayDef, &array) == mrhi_success, "an array");
    mrhiAccess layer = Access(array, mrhi_accessStorageWrite, 0, 1);
    layer.range.baseLayer = 1;
    layer.range.layerCount = 1;
    mrhiPassId first = Compute(&layer, 1, false);
    layer.range.baseLayer = 2;
    mrhiPassId second = Compute(&layer, 1, false);
    mrhiAccess all = Access(array, mrhi_accessSampled, 0, 1);
    mrhiPassId read = Compute(&all, 1, true);
    mrhiResourceId slices;
    CHECK(mrhiImportTexture(s_device, volume, &slices) == mrhi_success, "the volume");
    mrhiAccess whole = Access(slices, mrhi_accessStorageWrite, 0, 1);
    mrhiPassId fill = Compute(&whole, 1, false);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    ReadBarriers();
    CHECK(s_count == 7, "seven barriers");
    CHECK(Is(0, first, array, 0, 1, kAll, mrhi_stateUndefined, mrhi_stateStorageWrite) &&
              s_barriers[0].range.baseLayer == 1 && s_barriers[0].range.layerCount == 1,
          "layer 1 written");
    CHECK(Is(1, second, array, 0, 1, kAll, mrhi_stateUndefined, mrhi_stateStorageWrite) &&
              s_barriers[1].range.baseLayer == 2,
          "layer 2 written, not from layer 1's state");
    CHECK(Is(2, read, array, 0, 1, kAll, mrhi_stateUndefined, mrhi_stateSampled) &&
              s_barriers[2].range.baseLayer == 0 && s_barriers[2].range.layerCount == 1,
          "layer 0 read");
    CHECK(Is(3, read, array, 0, 1, kAll, mrhi_stateUndefined, mrhi_stateSampled) &&
              s_barriers[3].range.baseLayer == 3,
          "layer 3 read");
    CHECK(Is(4, read, array, 0, 1, kAll, mrhi_stateStorageWrite, mrhi_stateSampled) &&
              s_barriers[4].range.baseLayer == 1,
          "layer 1 read");
    CHECK(Is(5, read, array, 0, 1, kAll, mrhi_stateStorageWrite, mrhi_stateSampled) &&
              s_barriers[5].range.baseLayer == 2,
          "layer 2 read");
    CHECK(Is(6, fill, slices, 0, 1, kAll, mrhi_stateUndefined, mrhi_stateStorageWrite) &&
              s_barriers[6].range.layerCount == 1,
          "a volume is one layer, with nothing left at the end");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    CHECK(mrhiDestroyTexture(s_device, volume) == mrhi_success, "done");
}

// An imported buffer's state carried to the next frame.
static void TestBufferCarried(void)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = 64;
    def.usage = mrhi_bufferStorage;
    mrhiBufferId buffer;
    CHECK(mrhiCreateBuffer(s_device, &def, &buffer) == mrhi_success, "a buffer");
    Begin();
    mrhiResourceId imported;
    CHECK(mrhiImportBuffer(s_device, buffer, &imported) == mrhi_success, "imported");
    mrhiAccess write = Access(imported, mrhi_accessStorageWrite, 0, 1);
    Compute(&write, 1, false);
    Submit();
    Begin();
    CHECK(mrhiImportBuffer(s_device, buffer, &imported) == mrhi_success, "again");
    mrhiAccess read = Access(imported, mrhi_accessStorageRead, 0, 1);
    mrhiPassId pass = Compute(&read, 1, true);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    ReadBarriers();
    CHECK(s_count == 1 &&
              Is(0, pass, imported, 0, 1, kAll, mrhi_stateStorageWrite, mrhi_stateStorageRead),
          "after the last frame's write");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    CHECK(mrhiDestroyBuffer(s_device, buffer) == mrhi_success, "done");
}

// Depth and stencil planes move apart and together.
static void TestPlanes(void)
{
    Begin();
    mrhiResourceId depth = Declare(mrhi_formatDepthStencil, 1, 1);
    mrhiPassDef def = mrhiDefaultPassDef();
    def.depthTarget = (mrhiDepthTarget){
        .resource = depth,
        .depthLoad = mrhi_loadClear,
        .stencilLoad = mrhi_loadClear,
    };
    mrhiPassId write = AddPass(def);
    mrhiAccess depthOnly = Access(depth, mrhi_accessSampled, 0, 1);
    depthOnly.range.aspect = mrhi_aspectDepthOnly;
    mrhiPassId read = Compute(&depthOnly, 1, true);
    Compute(&depthOnly, 1, true);
    def.depthTarget = (mrhiDepthTarget){.resource = depth, .readOnly = true};
    def.neverCull = true;
    mrhiPassId test = AddPass(def);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    ReadBarriers();
    CHECK(s_count == 4, "four barriers");
    CHECK(Is(0, write, depth, 0, 1, kAll, mrhi_stateUndefined, mrhi_stateDepthTarget),
          "both planes made a target");
    CHECK(Is(1, read, depth, 0, 1, mrhi_aspectDepthOnly, mrhi_stateDepthTarget, mrhi_stateSampled),
          "depth sampled alone");
    CHECK(Is(2, test, depth, 0, 1, mrhi_aspectStencilOnly, mrhi_stateDepthTarget,
             mrhi_stateDepthRead),
          "stencil to read-only");
    CHECK(Is(3, test, depth, 0, 1, mrhi_aspectDepthOnly, mrhi_stateSampled, mrhi_stateDepthRead),
          "depth to read-only");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
}

// Targets that live only inside their passes are transient.
static void TestTransient(void)
{
    Begin();
    mrhiResourceId samples = Declare(mrhi_formatRgba8Unorm, 1, 4);
    mrhiResourceId resolved = Declare(mrhi_formatRgba8Unorm, 1, 1);
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){
        .resource = samples,
        .load = mrhi_loadClear,
        .store = mrhi_storeDiscard,
        .resolve = resolved,
    };
    def.colorTargetCount = 1;
    AddPass(def);
    mrhiAccess read = Access(resolved, mrhi_accessSampled, 0, 1);
    Compute(&read, 1, true);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    mrhiResourcePlan plan = Plan(samples);
    CHECK(plan.transient && plan.usage == (mrhi_textureRenderTarget | mrhi_textureTransient),
          "the samples live in the pass");
    plan = Plan(resolved);
    CHECK(!plan.transient, "what they resolve to is read later");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    Begin();
    samples = Declare(mrhi_formatRgba8Unorm, 1, 1);
    def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){.resource = samples, .load = mrhi_loadClear};
    def.colorTargetCount = 1;
    AddPass(def);
    def.colorTargets[0].load = mrhi_loadKeep;
    def.neverCull = true;
    AddPass(def);
    mrhiResourceId unused = Declare(mrhi_formatRgba8Unorm, 1, 1);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(!Plan(samples).transient, "contents kept between passes");
    plan = Plan(unused);
    CHECK(!plan.transient && plan.usage == 0 && plan.firstPass.index1 == 0 &&
              plan.lastPass.generation == 0,
          "an unused resource");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
}

// An import whose parts end in different states is unified at the end.
static void TestUnified(void)
{
    mrhiTextureId texture = MakeTexture(3, mrhi_textureStorage | mrhi_textureSampled);
    Begin();
    mrhiResourceId imported;
    CHECK(mrhiImportTexture(s_device, texture, &imported) == mrhi_success, "imported");
    mrhiAccess write = Access(imported, mrhi_accessStorageWrite, 0, 1);
    mrhiPassId pass = Compute(&write, 1, false);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    ReadBarriers();
    CHECK(s_count == 2, "two barriers");
    CHECK(Is(0, pass, imported, 0, 1, kAll, mrhi_stateUndefined, mrhi_stateStorageWrite),
          "mip 0 written");
    CHECK(Is(1, kEnd, imported, 1, 2, kAll, mrhi_stateUndefined, mrhi_stateStorageWrite),
          "the other mips follow at the end");
    Submit();
    Begin();
    CHECK(mrhiImportTexture(s_device, texture, &imported) == mrhi_success, "again");
    mrhiAccess read = Access(imported, mrhi_accessSampled, 1, 1);
    pass = Compute(&read, 1, true);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    ReadBarriers();
    CHECK(s_count == 3, "three barriers");
    CHECK(Is(0, pass, imported, 1, 1, kAll, mrhi_stateStorageWrite, mrhi_stateSampled),
          "every mip in the carried state");
    CHECK(Is(1, kEnd, imported, 0, 1, kAll, mrhi_stateStorageWrite, mrhi_stateSampled),
          "mip 0 follows");
    CHECK(Is(2, kEnd, imported, 2, 1, kAll, mrhi_stateStorageWrite, mrhi_stateSampled),
          "and mip 2");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    CHECK(mrhiDestroyTexture(s_device, texture) == mrhi_success, "done");
}

static void TestRefusals(void)
{
    mrhiBarrier barrier;
    size_t count = 0;
    mrhiResourcePlan plan;
    CHECK(mrhiGetFrameBarriers(s_device, &barrier, 1, &count) == mrhi_errorState, "no frame");
    Begin();
    mrhiResourceId scene = Declare(mrhi_formatRgba8Unorm, 1, 1);
    Target(scene, mrhi_loadClear, nullptr, 0);
    CHECK(mrhiGetFrameBarriers(s_device, &barrier, 1, &count) == mrhi_errorState, "not compiled");
    CHECK(mrhiGetResourcePlan(s_device, scene, &plan) == mrhi_errorState, "no plan yet");
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(mrhiGetFrameBarriers(s_device, nullptr, 0, &count) == mrhi_success && count == 0,
          "a culled pass makes no barrier");
    CHECK(mrhiGetFrameBarriers(s_device, nullptr, 1, &count) == mrhi_errorInvalid,
          "a capacity without an array");
    CHECK(mrhiGetFrameBarriers(s_device, &barrier, 1, nullptr) == mrhi_errorInvalid, "no count");
    CHECK(mrhiGetFrameBarriers(nullptr, &barrier, 1, &count) == mrhi_errorInvalid, "no device");
    CHECK(mrhiGetResourcePlan(s_device, scene, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiGetResourcePlan(nullptr, scene, &plan) == mrhi_errorInvalid, "no device");
    CHECK(mrhiGetResourcePlan(s_device, (mrhiResourceId){0, 0}, &plan) == mrhi_errorStale,
          "no resource");
    CHECK(mrhiGetResourcePlan(s_device, (mrhiResourceId){2, scene.generation}, &plan) ==
              mrhi_errorStale,
          "past the resources");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    Begin();
    Declare(mrhi_formatRgba8Unorm, 1, 1);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(mrhiGetResourcePlan(s_device, scene, &plan) == mrhi_errorStale, "the last frame's");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
}

// Fewer barriers allowed than a frame plans: the compile refuses, and
// the frame stays open.
static void TestBarrierLimit(void)
{
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.deviceLimits.frameBarriers = 2;
    mrhiInstance* instance = s_instance;
    mrhiDevice* previous = s_device;
    s_device = OpenWith(deviceDef, true);
    Begin();
    mrhiResourceId mips = Declare(mrhi_formatRgba16Float, 3, 1);
    mrhiAccess first = Access(mips, mrhi_accessStorageWrite, 0, 1);
    Compute(&first, 1, false);
    mrhiAccess all = Access(mips, mrhi_accessSampled, 0, MRHI_REMAINING);
    Compute(&all, 1, true);
    CHECK(mrhiCompileFrame(s_device) == mrhi_errorCapacity, "three barriers for two");
    mrhiRequestId token;
    CHECK(mrhiSubmitFrame(s_device, &token) == mrhi_errorCapacity, "not submitted either");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "still open");
    Close(s_device);
    s_device = previous;
    s_instance = instance;
}

int main(void)
{
    ResetAdapter();
    s_device = OpenWith(mrhiDefaultDeviceDef(), true);
    mrhiInstance* instance = s_instance;
    TestRenderThenSample();
    TestMipChain();
    TestBuffers();
    TestWritesAfter();
    TestLayers();
    TestBufferCarried();
    TestPlanes();
    TestTransient();
    TestUnified();
    TestRefusals();
    TestBarrierLimit();
    s_instance = instance;
    Close(s_device);
    return s_failures == 0 ? 0 : 1;
}
