// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Binding tables on a test driver device: whole tables checked against
// the slots of the pass's pipeline as WebGPU checks a bind group, and
// against the accesses the pass declares, then recorded after their
// command.

#include "device_core.h"
#include "test_container.h"
#include "test_device_setup.h"

#include "maul-rhi/encoder.h"

// The default container's tables: table 0 holds a uniform buffer of at
// least 64 bytes (slot 0), a storage buffer (1), a read-only storage
// buffer (2) and a filtering sampler (3); table 1 a float cube texture
// (0) and a write-only rgba8 2D array storage texture (1).
#define SAMPLED_SLOT 4
#define SAMPLER_SLOT 3

static mrhiDevice* s_device;
static mrhiComputePipelineId s_pipeline;
static mrhiBufferId s_uniform;
static mrhiBufferId s_written;
static mrhiBufferId s_read;
static mrhiTextureId s_cube;
static mrhiTextureId s_array;
static mrhiSamplerId s_linear;
static mrhiSamplerId s_nearest;
static mrhiSamplerId s_compare;

// The open frame's resources, imported, and its passes: one that
// dispatches with the declared accesses, and one that copies.
static mrhiResourceId s_u;
static mrhiResourceId s_w;
static mrhiResourceId s_r;
static mrhiResourceId s_c;
static mrhiResourceId s_t;
static mrhiPassId s_pass;
static mrhiPassId s_copy;

// A compute pipeline of the sections as they stand, answered.
static mrhiComputePipelineId MakePipeline(void)
{
    Assemble();
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_container;
    shaderDef.byteCount = s_size;
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(s_device, &shaderDef, &shader) == mrhi_success, "a shader");
    mrhiComputePipelineDef def = mrhiDefaultComputePipelineDef();
    def.shader = shader;
    def.entry = "cs";
    def.entryLength = 2;
    mrhiComputePipelineId pipeline = {0};
    mrhiRequestId request;
    CHECK(mrhiCreateComputePipeline(s_device, &def, &pipeline, &request) == mrhi_success,
          "a pipeline");
    mrhiDeviceNotification record;
    CHECK(mrhiNextDeviceNotification(s_device, &record) == mrhi_success &&
              record.outcome == mrhi_success,
          "ready");
    CHECK(mrhiDestroyShader(s_device, shader) == mrhi_success, "its shader destroyed");
    return pipeline;
}

static mrhiBufferId MakeBuffer(uint64_t size)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = mrhi_bufferUniform | mrhi_bufferStorage;
    mrhiBufferId buffer = {0};
    CHECK(mrhiCreateBuffer(s_device, &def, &buffer) == mrhi_success, "a buffer");
    return buffer;
}

static mrhiTextureId MakeTexture(mrhiTextureKind kind, mrhiFormat format, uint32_t layers,
                                 uint32_t samples, mrhiTextureUsage usage)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.kind = kind;
    def.format = format;
    def.width = 16;
    def.height = 16;
    def.depthOrLayers = layers;
    def.mipLevels = samples > 1 ? 1 : 2;
    def.sampleCount = samples;
    def.usage = usage;
    def.viewFormats[0] = format == mrhi_formatRgba8Unorm ? mrhi_formatRgba8UnormSrgb : 0;
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(s_device, &def, &texture) == mrhi_success, "a texture");
    return texture;
}

static mrhiSamplerId MakeSampler(mrhiFilter filter, mrhiCompareFunction compare)
{
    mrhiSamplerDef def = mrhiDefaultSamplerDef();
    def.magFilter = filter;
    def.minFilter = filter;
    def.compare = compare;
    mrhiSamplerId sampler = {0};
    CHECK(mrhiCreateSampler(s_device, &def, &sampler) == mrhi_success, "a sampler");
    return sampler;
}

// Opens a ready device with the default container's pipeline and the
// objects its tables take.
static void Open(void)
{
    s_device = OpenWith(mrhiDefaultDeviceDef(), true);
    Reset();
    s_pipeline = MakePipeline();
    s_uniform = MakeBuffer(1u << 17);
    s_written = MakeBuffer(1024);
    s_read = MakeBuffer(1024);
    s_cube = MakeTexture(mrhi_textureCube, mrhi_formatRgba8Unorm, 6, 1, mrhi_textureSampled);
    s_array = MakeTexture(mrhi_texture2dArray, mrhi_formatRgba8Unorm, 4, 1,
                          mrhi_textureStorage | mrhi_textureSampled);
    s_linear = MakeSampler(mrhi_filterLinear, mrhi_compareNone);
    s_nearest = MakeSampler(mrhi_filterNearest, mrhi_compareNone);
    s_compare = MakeSampler(mrhi_filterNearest, mrhi_compareLess);
}

static mrhiResourceId ImportBuffer(mrhiBufferId buffer)
{
    mrhiResourceId resource = {0};
    CHECK(mrhiImportBuffer(s_device, buffer, &resource) == mrhi_success, "imported");
    return resource;
}

static mrhiResourceId ImportTexture(mrhiTextureId texture)
{
    mrhiResourceId resource = {0};
    CHECK(mrhiImportTexture(s_device, texture, &resource) == mrhi_success, "imported");
    return resource;
}

static mrhiAccess Access(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
}

// The accesses the default tables need: the uniform, written, read,
// cube and array resources as uniform, storage write, storage read,
// sampled, and storage write of the array's first mip.
static uint32_t DefaultAccesses(mrhiAccess* accesses)
{
    accesses[0] = Access(s_u, mrhi_accessUniform);
    accesses[1] = Access(s_w, mrhi_accessStorageWrite);
    accesses[2] = Access(s_r, mrhi_accessStorageRead);
    accesses[3] = Access(s_c, mrhi_accessSampled);
    accesses[4] = Access(s_t, mrhi_accessStorageWrite);
    accesses[4].range.mipCount = 1;
    return 5;
}

// Begins a frame importing the objects.
static void BeginFrame(void)
{
    mrhiFrameDef def = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &def) == mrhi_success, "begun");
    s_u = ImportBuffer(s_uniform);
    s_w = ImportBuffer(s_written);
    s_r = ImportBuffer(s_read);
    s_c = ImportTexture(s_cube);
    s_t = ImportTexture(s_array);
}

// Adds the dispatching pass with the accesses and a copying pass,
// compiles, begins both, and sets the pipeline in the first.
static void Passes(const mrhiAccess* accesses, uint32_t count)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.accesses = accesses;
    def.accessCount = count;
    CHECK(mrhiAddPass(s_device, &def, &s_pass) == mrhi_success, "the pass");
    def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.passClass = mrhi_passTransfer;
    CHECK(mrhiAddPass(s_device, &def, &s_copy) == mrhi_success, "a copying pass");
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(mrhiBeginPass(s_device, s_pass) == mrhi_success &&
              mrhiBeginPass(s_device, s_copy) == mrhi_success,
          "begun");
    CHECK(mrhiSetComputePipeline(s_device, s_pass, s_pipeline) == mrhi_success, "the pipeline");
}

// A frame whose pass declares the default accesses.
static void Frame(void)
{
    BeginFrame();
    mrhiAccess accesses[5];
    Passes(accesses, DefaultAccesses(accesses));
}

static void Drop(void)
{
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
}

static void CloseDevice(void)
{
    Close(s_device);
    s_device = nullptr;
}

// Table 0 as the default accesses take it.
static void Table0(mrhiBinding* bindings)
{
    bindings[0] = (mrhiBinding){.slot = 0, .resource = s_u, .size = 256};
    bindings[1] = (mrhiBinding){.slot = 1, .resource = s_w, .size = MRHI_WHOLE_SIZE};
    bindings[2] = (mrhiBinding){.slot = 2, .resource = s_r, .offset = 256, .size = 512};
    bindings[3] = (mrhiBinding){.slot = SAMPLER_SLOT, .sampler = s_linear};
}

// Table 1 as the default accesses take it.
static void Table1(mrhiBinding* bindings)
{
    bindings[0] = (mrhiBinding){
        .slot = 0,
        .resource = s_c,
        .viewKind = mrhi_textureCube,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
    bindings[1] = (mrhiBinding){
        .slot = 1,
        .resource = s_t,
        .viewKind = mrhi_texture2dArray,
        .range = {.mipCount = 1, .layerCount = MRHI_REMAINING},
    };
}

// Sets table 0 with one binding changed: the result.
static mrhiResult With0(uint32_t index, mrhiBinding binding)
{
    mrhiBinding bindings[4];
    Table0(bindings);
    bindings[index] = binding;
    return mrhiSetBindings(s_device, s_pass, 0, bindings, 4);
}

// Sets table 1 with one binding changed: the result.
static mrhiResult With1(uint32_t index, mrhiBinding binding)
{
    mrhiBinding bindings[2];
    Table1(bindings);
    bindings[index] = binding;
    return mrhiSetBindings(s_device, s_pass, 1, bindings, 2);
}

// The n-th record of the pass, walking its chunks: or NULL past them.
static const mrhiCommand* Nth(mrhiPassId id, uint32_t n)
{
    const mrhiFramePass* pass = &s_device->framePasses[id.index1 - 1];
    for (uint32_t chunk = pass->firstChunk; chunk != 0;
         chunk = s_device->frameChunks[chunk - 1].next)
    {
        const mrhiCommandChunk* at = &s_device->frameChunks[chunk - 1];
        if (n < at->count)
        {
            return &at->commands[n];
        }
        n -= at->count;
    }
    return nullptr;
}

static const mrhiCommandBinding* NthBinding(uint32_t n)
{
    return (const mrhiCommandBinding*)Nth(s_pass, n);
}

static void TestTables(void)
{
    Open();
    Frame();
    mrhiBinding bindings[4];
    Table0(bindings);
    mrhiBinding swapped = bindings[0];
    bindings[0] = bindings[3];
    bindings[3] = swapped;
    CHECK(mrhiSetBindings(s_device, s_pass, 0, bindings, 4) == mrhi_success,
          "table 0, in any order");
    const mrhiCommand* command = Nth(s_pass, 1);
    CHECK(command != nullptr && command->type == mrhiCommandBindings && command->a == 0 &&
              command->b == 4 && command->payload == 0,
          "recorded");
    const mrhiCommandBinding* sampler = NthBinding(2);
    const mrhiCommandBinding* written = NthBinding(3);
    const mrhiCommandBinding* read = NthBinding(4);
    const mrhiCommandBinding* uniform = NthBinding(5);
    CHECK(sampler->slot == SAMPLER_SLOT && sampler->object == s_linear.index1, "the sampler");
    CHECK(written->slot == 1 && written->object == s_w.index1 && written->offset == 0 &&
              written->size == 1024,
          "the whole written buffer");
    CHECK(read->object == s_r.index1 && read->offset == 256 && read->size == 512, "a range");
    CHECK(uniform->object == s_u.index1 && uniform->size == 256 && Nth(s_pass, 6) == nullptr,
          "the uniform buffer, last");
    mrhiBinding textures[2];
    Table1(textures);
    CHECK(mrhiSetBindings(s_device, s_pass, 1, textures, 2) == mrhi_success, "table 1");
    const mrhiCommandBinding* cube = NthBinding(7);
    const mrhiCommandBinding* array = NthBinding(8);
    CHECK(cube->object == s_c.index1 && cube->viewKind == mrhi_textureCube &&
              cube->viewFormat == mrhi_formatRgba8Unorm && cube->aspect == mrhi_aspectAll &&
              cube->baseMip == 0 && cube->mipCount == 2 && cube->offset == 0 && cube->size == 6,
          "the cube's view resolved");
    CHECK(array->viewKind == mrhi_texture2dArray && array->mipCount == 1 && array->size == 4,
          "the array's");
    CHECK(mrhiSetBindings(s_device, s_pass, 2, nullptr, 0) == mrhi_success &&
              mrhiSetBindings(s_device, s_pass, 3, nullptr, 0) == mrhi_success,
          "the empty tables");
    CHECK(Nth(s_pass, 10) != nullptr && Nth(s_pass, 11) == nullptr, "one record each");
    CHECK(mrhiGetDeviceMisuse(s_device) == 0, "no misuse");
    CHECK(mrhiSetBindings(s_device, s_pass, 4, nullptr, 0) == mrhi_errorInvalid,
          "past the device's tables");
    CHECK(mrhiSetBindings(s_device, s_pass, 0, bindings, 3) == mrhi_errorInvalid &&
              mrhiSetBindings(s_device, s_pass, 2, bindings, 1) == mrhi_errorInvalid,
          "fewer or more than the table's slots");
    Table0(bindings);
    CHECK(With0(3, bindings[0]) == mrhi_errorInvalid, "a slot twice");
    bindings[3].slot = 9;
    CHECK(With0(3, bindings[3]) == mrhi_errorInvalid, "a slot the table lacks");
    CHECK(mrhiSetBindings(s_device, s_pass, 0, nullptr, 4) == mrhi_errorInvalid, "no bindings");
    CHECK(mrhiSetBindings(s_device, s_copy, 2, nullptr, 0) == mrhi_errorInvalid,
          "a pass that copies");
    CHECK(mrhiGetDeviceMisuse(s_device) == 7, "each counted");
    CHECK(Nth(s_pass, 11) == nullptr, "nothing recorded");
    CHECK(mrhiSetBindings(nullptr, s_pass, 2, nullptr, 0) == mrhi_errorInvalid, "no device");
    CHECK(mrhiEndPass(s_device, s_pass) == mrhi_success, "ended");
    CHECK(mrhiSetBindings(s_device, s_pass, 2, nullptr, 0) == mrhi_errorState, "not recording");
    Drop();
    CHECK(mrhiSetBindings(s_device, s_pass, 2, nullptr, 0) == mrhi_errorState, "no frame");
    CloseDevice();
}

static void TestPipeline(void)
{
    Open();
    BeginFrame();
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    mrhiPassId bare;
    CHECK(mrhiAddPass(s_device, &def, &bare) == mrhi_success, "a pass");
    mrhiAccess accesses[5];
    Passes(accesses, DefaultAccesses(accesses));
    CHECK(mrhiBeginPass(s_device, bare) == mrhi_success, "begun");
    CHECK(mrhiSetBindings(s_device, bare, 2, nullptr, 0) == mrhi_errorState, "no pipeline set");
    CHECK(mrhiDestroyComputePipeline(s_device, s_pipeline) == mrhi_success, "destroyed");
    CHECK(mrhiSetBindings(s_device, s_pass, 2, nullptr, 0) == mrhi_errorStale,
          "a destroyed pipeline");
    s_pipeline = MakePipeline();
    CHECK(mrhiSetBindings(s_device, s_pass, 2, nullptr, 0) == mrhi_errorStale,
          "and not its slot's next");
    CHECK(mrhiSetComputePipeline(s_device, s_pass, s_pipeline) == mrhi_success &&
              mrhiSetBindings(s_device, s_pass, 2, nullptr, 0) == mrhi_success,
          "the new one");
    Drop();
    // A render pass and a graphics pipeline.
    mrhiTextureId target =
        MakeTexture(mrhi_texture2d, mrhi_formatRgba8Unorm, 1, 1, mrhi_textureRenderTarget);
    mrhiGraphicsPipelineDef graphicsDef = mrhiDefaultGraphicsPipelineDef();
    Assemble();
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_container;
    shaderDef.byteCount = s_size;
    CHECK(mrhiCreateShader(s_device, &shaderDef, &graphicsDef.shader) == mrhi_success, "shader");
    graphicsDef.vertexEntry = "vs";
    graphicsDef.vertexEntryLength = 2;
    graphicsDef.fragmentEntry = "fs";
    graphicsDef.fragmentEntryLength = 2;
    mrhiVertexBufferLayout layout = {.stride = 12, .stepMode = mrhi_stepVertex};
    mrhiVertexAttribute attribute = {.location = 3, .format = mrhi_vertexFloat32x3};
    graphicsDef.vertexBuffers = &layout;
    graphicsDef.vertexBufferCount = 1;
    graphicsDef.vertexAttributes = &attribute;
    graphicsDef.vertexAttributeCount = 1;
    graphicsDef.colorTargets[0].format = mrhi_formatRgba8Unorm;
    graphicsDef.colorTargetCount = 1;
    mrhiGraphicsPipelineId graphics = {0};
    mrhiRequestId request;
    CHECK(mrhiCreateGraphicsPipeline(s_device, &graphicsDef, &graphics, &request) == mrhi_success,
          "a graphics pipeline");
    mrhiDeviceNotification record;
    CHECK(mrhiNextDeviceNotification(s_device, &record) == mrhi_success, "ready");
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &frameDef) == mrhi_success, "begun");
    mrhiPassDef renderDef = mrhiDefaultPassDef();
    renderDef.colorTargets[0] = (mrhiColorTarget){
        .resource = ImportTexture(target),
        .load = mrhi_loadClear,
        .store = mrhi_storeKeep,
    };
    renderDef.colorTargetCount = 1;
    mrhiPassId render;
    CHECK(mrhiAddPass(s_device, &renderDef, &render) == mrhi_success, "a render pass");
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(mrhiBeginPass(s_device, render) == mrhi_success &&
              mrhiSetGraphicsPipeline(s_device, render, graphics) == mrhi_success,
          "the graphics pipeline set");
    CHECK(mrhiSetBindings(s_device, render, 2, nullptr, 0) == mrhi_success, "a table set");
    Drop();
    BeginFrame();
    Passes(accesses, DefaultAccesses(accesses));
    // A pipeline of another container: table 2 holds a sampler there.
    Reset();
    uint8_t* added = Binding(6, 2, 0, mrhi_bindingSampler, mrhi_stageCompute);
    added[8] = mrhi_samplerFiltering;
    mrhiComputePipelineId other = MakePipeline();
    CHECK(mrhiSetComputePipeline(s_device, s_pass, other) == mrhi_success, "set");
    CHECK(mrhiSetBindings(s_device, s_pass, 2, nullptr, 0) == mrhi_errorInvalid,
          "its table has a slot");
    mrhiBinding sampler = {.slot = 0, .sampler = s_nearest};
    CHECK(mrhiSetBindings(s_device, s_pass, 2, &sampler, 1) == mrhi_success, "filled");
    Drop();
    CloseDevice();
}

static void TestBuffers(void)
{
    Open();
    Frame();
    mrhiBinding bindings[4];
    Table0(bindings);
    mrhiBinding uniform = bindings[0];
    uniform.offset = 128;
    CHECK(With0(0, uniform) == mrhi_errorInvalid, "an offset off the uniform alignment");
    uniform.offset = 256;
    CHECK(With0(0, uniform) == mrhi_success, "on it");
    uniform.size = 0;
    CHECK(With0(0, uniform) == mrhi_errorInvalid, "empty");
    uniform = (mrhiBinding){.slot = 0, .resource = s_u, .offset = 65536, .size = 65536};
    CHECK(With0(0, uniform) == mrhi_success, "the largest uniform binding");
    uniform.offset = 0;
    uniform.size = 65540;
    CHECK(With0(0, uniform) == mrhi_errorInvalid, "past it");
    uniform.offset = 0;
    uniform.size = 60;
    CHECK(With0(0, uniform) == mrhi_errorInvalid, "below the shader's minimum");
    uniform.size = 64;
    CHECK(With0(0, uniform) == mrhi_success, "at it");
    uniform.size = 66;
    CHECK(With0(0, uniform) == mrhi_success, "a uniform size of any multiple");
    mrhiBinding read = bindings[2];
    read.offset = 1024;
    read.size = MRHI_WHOLE_SIZE;
    CHECK(With0(2, read) == mrhi_errorInvalid, "the rest of nothing");
    read.offset = 768;
    CHECK(With0(2, read) == mrhi_success, "the rest");
    // The pipeline's record, four tables before this one, and its header.
    CHECK(NthBinding(1 + 4 * 5 + 3)->offset == 768 && NthBinding(1 + 4 * 5 + 3)->size == 256,
          "resolved");
    read.size = 260;
    CHECK(With0(2, read) == mrhi_errorInvalid, "past the end");
    read.offset = 1280;
    read.size = 4;
    CHECK(With0(2, read) == mrhi_errorInvalid, "starting past it");
    read.offset = UINT64_MAX - 255;
    read.size = 512;
    CHECK(With0(2, read) == mrhi_errorInvalid, "wrapping");
    read.offset = 256;
    read.size = 6;
    CHECK(With0(2, read) == mrhi_errorInvalid, "storage not a multiple of 4");
    read.size = 8;
    CHECK(With0(2, read) == mrhi_success, "a multiple of 4");
    read.offset = 4;
    CHECK(With0(2, read) == mrhi_errorInvalid, "off the storage alignment");
    CHECK(mrhiGetDeviceMisuse(s_device) == 10, "each counted");
    Drop();
    CloseDevice();
}

static void TestKinds(void)
{
    Open();
    Frame();
    mrhiBinding bindings[4];
    Table0(bindings);
    mrhiBinding textures[2];
    Table1(textures);
    mrhiBinding texture = textures[0];
    texture.slot = 0;
    CHECK(With0(0, texture) == mrhi_errorInvalid, "a texture for a buffer");
    mrhiBinding buffer = bindings[0];
    CHECK(With1(0, buffer) == mrhi_errorInvalid, "a buffer for a texture");
    // The written buffer is declared written as storage, as the storage
    // texture slot needs, so only its kind refuses it.
    mrhiBinding written = {.slot = 1, .resource = s_w, .size = 4};
    CHECK(With1(1, written) == mrhi_errorInvalid, "a buffer for a storage texture");
    mrhiBinding sampler = bindings[3];
    sampler.resource = s_u;
    CHECK(With0(3, sampler) == mrhi_errorInvalid, "a sampler with a resource");
    CHECK(mrhiGetDeviceMisuse(s_device) == 4, "each counted");
    mrhiResourceId old = s_u;
    Drop();
    Frame();
    mrhiBinding stale = bindings[0];
    stale.resource = old;
    CHECK(With0(0, stale) == mrhi_errorStale, "a resource of an earlier frame");
    stale.resource = (mrhiResourceId){0};
    CHECK(With0(0, stale) == mrhi_errorStale, "none");
    CHECK(mrhiDestroySampler(s_device, s_linear) == mrhi_success, "a sampler destroyed");
    CHECK(With0(3, bindings[3]) == mrhi_errorStale, "its id ended");
    CHECK(mrhiGetDeviceMisuse(s_device) == 4, "not misuse");
    Drop();
    CloseDevice();
}

// Runs a frame whose pass declares the default accesses with one
// changed: the result of setting both tables, table 1 with its view
// ranges from the bindings given.
static mrhiResult Declared(uint32_t index, mrhiAccess access, const mrhiBinding* texture)
{
    BeginFrame();
    mrhiAccess accesses[5];
    uint32_t count = DefaultAccesses(accesses);
    const mrhiResourceId resources[] = {s_u, s_w, s_r, s_c, s_t};
    access.resource = resources[index];
    accesses[index] = access;
    Passes(accesses, count);
    mrhiBinding bindings[4];
    Table0(bindings);
    mrhiBinding textures[2];
    Table1(textures);
    if (texture != nullptr)
    {
        textures[texture->slot] = *texture;
        textures[texture->slot].resource = texture->slot == 0 ? s_c : s_t;
    }
    mrhiResult status = mrhiSetBindings(s_device, s_pass, 0, bindings, 4);
    if (status == mrhi_success)
    {
        status = mrhiSetBindings(s_device, s_pass, 1, textures, 2);
    }
    Drop();
    return status;
}

static mrhiAccess Kind(mrhiAccessKind kind)
{
    return Access((mrhiResourceId){0}, kind);
}

static void TestAccesses(void)
{
    Open();
    CHECK(Declared(0, Kind(mrhi_accessUniform), nullptr) == mrhi_success, "as declared");
    CHECK(Declared(0, Kind(mrhi_accessStorageRead), nullptr) == mrhi_errorInvalid,
          "a uniform buffer declared as storage");
    CHECK(Declared(1, Kind(mrhi_accessStorageRead), nullptr) == mrhi_errorInvalid,
          "a storage buffer declared read");
    CHECK(Declared(1, Kind(mrhi_accessStorageReadWrite), nullptr) == mrhi_success,
          "or read and written");
    CHECK(Declared(2, Kind(mrhi_accessStorageWrite), nullptr) == mrhi_errorInvalid,
          "a read-only one declared written");
    CHECK(Declared(2, Kind(mrhi_accessStorageReadWrite), nullptr) == mrhi_success,
          "or read and written");
    CHECK(Declared(4, Kind(mrhi_accessStorageRead), nullptr) == mrhi_errorInvalid,
          "a write-only storage texture declared read");
    mrhiAccess all = Kind(mrhi_accessStorageReadWrite);
    all.range.mipCount = 1;
    CHECK(Declared(4, all, nullptr) == mrhi_success, "or read and written");
    mrhiAccess mip = Kind(mrhi_accessSampled);
    mip.range.mipCount = 1;
    CHECK(Declared(3, mip, nullptr) == mrhi_errorInvalid, "a sampled mip of two");
    mrhiBinding texture = {
        .slot = 0,
        .viewKind = mrhi_textureCube,
        .range = {.mipCount = 1, .layerCount = MRHI_REMAINING},
    };
    CHECK(Declared(3, mip, &texture) == mrhi_success, "that mip bound");
    texture.range.baseMip = 1;
    CHECK(Declared(3, mip, &texture) == mrhi_errorInvalid, "the other");
    mrhiAccess layers = Kind(mrhi_accessStorageWrite);
    layers.range.mipCount = 1;
    layers.range.layerCount = 2;
    CHECK(Declared(4, layers, nullptr) == mrhi_errorInvalid, "two layers of four written");
    texture = (mrhiBinding){
        .slot = 1,
        .viewKind = mrhi_texture2dArray,
        .range = {.mipCount = 1, .layerCount = 2},
    };
    CHECK(Declared(4, layers, &texture) == mrhi_success, "those bound");
    texture.range.baseLayer = 1;
    CHECK(Declared(4, layers, &texture) == mrhi_errorInvalid, "one past them");
    layers.range.baseMip = 1;
    layers.range.baseLayer = 0;
    layers.range.layerCount = MRHI_REMAINING;
    texture.range.baseLayer = 0;
    texture.range.layerCount = MRHI_REMAINING;
    CHECK(Declared(4, layers, &texture) == mrhi_errorInvalid, "another mip declared");
    layers = Kind(mrhi_accessStorageWrite);
    layers.range.mipCount = 1;
    layers.range.baseLayer = 1;
    texture.range.layerCount = 2;
    CHECK(Declared(4, layers, &texture) == mrhi_errorInvalid, "a layer before them");
    texture.range.baseLayer = 1;
    texture.range.layerCount = 3;
    CHECK(Declared(4, layers, &texture) == mrhi_success, "the layers declared");
    mrhiAccess mips = Kind(mrhi_accessStorageWrite);
    texture.range = (mrhiTextureRange){.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING};
    CHECK(Declared(4, mips, &texture) == mrhi_errorInvalid, "a storage view of two mips");
    CHECK(Declared(1, Kind(mrhi_accessUniform), nullptr) == mrhi_errorInvalid,
          "a storage buffer declared as uniform");
    CloseDevice();
}

// Sets table 1 with the sampled slot's type, dimension and multisampling
// changed, binding a view of a texture: the result.
static mrhiResult Sampled(mrhiSampleType type, mrhiTextureKind dimension, bool multisampled,
                          mrhiTextureId texture, mrhiTextureKind kind, mrhiTextureAspect aspect)
{
    Reset();
    uint8_t* slot = s_sections[BINDINGS].bytes + SAMPLED_SLOT * 24;
    slot[9] = type;
    slot[10] = dimension;
    slot[14] = multisampled ? 1 : 0;
    mrhiComputePipelineId pipeline = MakePipeline();
    BeginFrame();
    mrhiResourceId sampled = ImportTexture(texture);
    mrhiAccess accesses[6];
    uint32_t count = DefaultAccesses(accesses);
    accesses[count] = Access(sampled, mrhi_accessSampled);
    accesses[count].range.aspect = aspect;
    Passes(accesses, count + 1);
    CHECK(mrhiSetComputePipeline(s_device, s_pass, pipeline) == mrhi_success, "set");
    mrhiBinding binding = {
        .slot = 0,
        .resource = sampled,
        .viewKind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING, .aspect = aspect},
    };
    mrhiResult status = With1(0, binding);
    Drop();
    CHECK(mrhiDestroyComputePipeline(s_device, pipeline) == mrhi_success, "destroyed");
    return status;
}

static void TestTextures(void)
{
    Open();
    Frame();
    mrhiBinding textures[2];
    Table1(textures);
    mrhiBinding cube = textures[0];
    cube.viewKind = mrhi_texture2dArray;
    CHECK(With1(0, cube) == mrhi_errorInvalid, "another kind than the slot's");
    cube.viewKind = mrhi_textureCube;
    cube.range.layerCount = 5;
    CHECK(With1(0, cube) == mrhi_errorInvalid, "a cube of five layers");
    cube.range.layerCount = MRHI_REMAINING;
    cube.range.baseMip = 2;
    CHECK(With1(0, cube) == mrhi_errorInvalid, "past the mips");
    cube.range.baseMip = 0;
    cube.viewFormat = mrhi_formatRgba8UnormSrgb;
    CHECK(With1(0, cube) == mrhi_success, "a view format the texture was given");
    cube.viewFormat = mrhi_formatBgra8Unorm;
    CHECK(With1(0, cube) == mrhi_errorInvalid, "one it was not");
    cube.viewFormat = mrhi_formatNone;
    cube.range.aspect = mrhi_aspectDepthOnly;
    CHECK(With1(0, cube) == mrhi_errorInvalid, "an aspect the format lacks");
    cube.range.aspect = 7;
    CHECK(With1(0, cube) == mrhi_errorInvalid, "an unknown aspect");
    mrhiBinding array = textures[1];
    array.viewFormat = mrhi_formatRgba8UnormSrgb;
    CHECK(With1(1, array) == mrhi_errorInvalid, "a storage view of another format");
    array.viewFormat = mrhi_formatNone;
    array.viewKind = mrhi_texture2d;
    CHECK(With1(1, array) == mrhi_errorInvalid, "of another kind");
    CHECK(mrhiGetDeviceMisuse(s_device) == 8, "each counted");
    Drop();
    mrhiTextureId depth =
        MakeTexture(mrhi_texture2d, mrhi_formatDepth32Float, 1, 1, mrhi_textureSampled);
    mrhiTextureId both =
        MakeTexture(mrhi_texture2d, mrhi_formatDepthStencil, 1, 1, mrhi_textureSampled);
    mrhiTextureId unfilterable =
        MakeTexture(mrhi_texture2d, mrhi_formatR32Float, 1, 1, mrhi_textureSampled);
    mrhiTextureId sint = MakeTexture(mrhi_texture2d, mrhi_formatR32Sint, 1, 1, mrhi_textureSampled);
    mrhiTextureId uint = MakeTexture(mrhi_texture2d, mrhi_formatR32Uint, 1, 1, mrhi_textureSampled);
    mrhiTextureId multi = MakeTexture(mrhi_texture2d, mrhi_formatRgba8Unorm, 1, 4,
                                      mrhi_textureSampled | mrhi_textureRenderTarget);
    mrhiTextureKind flat = mrhi_texture2d;
    mrhiTextureAspect all = mrhi_aspectAll;
    mrhiTextureAspect depthOnly = mrhi_aspectDepthOnly;
    mrhiTextureAspect stencilOnly = mrhi_aspectStencilOnly;
    CHECK(Sampled(mrhi_sampleFloat, mrhi_textureCube, false, s_cube, mrhi_textureCube, all) ==
              mrhi_success,
          "filterable float");
    CHECK(Sampled(mrhi_sampleUnfilterableFloat, mrhi_textureCube, false, s_cube, mrhi_textureCube,
                  all) == mrhi_success,
          "as unfilterable float");
    CHECK(Sampled(mrhi_sampleUint, mrhi_textureCube, false, s_cube, mrhi_textureCube, all) ==
              mrhi_errorInvalid,
          "not as uint");
    CHECK(Sampled(mrhi_sampleDepth, mrhi_textureCube, false, s_cube, mrhi_textureCube, all) ==
              mrhi_errorInvalid,
          "nor depth");
    CHECK(Sampled(mrhi_sampleFloat, flat, false, unfilterable, flat, all) == mrhi_errorInvalid,
          "r32float, unfilterable on the floor");
    CHECK(Sampled(mrhi_sampleUnfilterableFloat, flat, false, unfilterable, flat, all) ==
              mrhi_success,
          "as unfilterable float");
    CHECK(Sampled(mrhi_sampleSint, flat, false, sint, flat, all) == mrhi_success &&
              Sampled(mrhi_sampleUint, flat, false, uint, flat, all) == mrhi_success,
          "integers as their class");
    CHECK(Sampled(mrhi_sampleUint, flat, false, sint, flat, all) == mrhi_errorInvalid &&
              Sampled(mrhi_sampleSint, flat, false, uint, flat, all) == mrhi_errorInvalid &&
              Sampled(mrhi_sampleUnfilterableFloat, flat, false, sint, flat, all) ==
                  mrhi_errorInvalid,
          "not as another");
    CHECK(Sampled(mrhi_sampleDepth, flat, false, depth, flat, all) == mrhi_success &&
              Sampled(mrhi_sampleUnfilterableFloat, flat, false, depth, flat, all) == mrhi_success,
          "depth as depth or unfilterable float");
    CHECK(Sampled(mrhi_sampleFloat, flat, false, depth, flat, all) == mrhi_errorInvalid,
          "not filtered");
    CHECK(Sampled(mrhi_sampleDepth, flat, false, both, flat, all) == mrhi_errorInvalid,
          "depth and stencil seen at once");
    CHECK(Sampled(mrhi_sampleDepth, flat, false, both, flat, depthOnly) == mrhi_success,
          "the depth of both");
    CHECK(Sampled(mrhi_sampleUint, flat, false, both, flat, stencilOnly) == mrhi_success,
          "the stencil as uint");
    CHECK(Sampled(mrhi_sampleDepth, flat, false, both, flat, stencilOnly) == mrhi_errorInvalid,
          "not as depth");
    CHECK(Sampled(mrhi_sampleUnfilterableFloat, flat, true, multi, flat, all) == mrhi_success,
          "a multisampled texture for a multisampled slot");
    CHECK(Sampled(mrhi_sampleUnfilterableFloat, flat, false, multi, flat, all) == mrhi_errorInvalid,
          "not for another");
    CHECK(Sampled(mrhi_sampleUnfilterableFloat, flat, true, unfilterable, flat, all) ==
              mrhi_errorInvalid,
          "nor a single sample for it");
    CloseDevice();
}

// Sets table 1 with a view of a texture in the sampled slot, of a
// dimension and type, the pass declaring the texture's aspect with a
// kind: the result.
static mrhiResult DeclaredAs(mrhiTextureId texture, mrhiTextureKind dimension, mrhiSampleType type,
                             mrhiAccessKind kind, mrhiTextureAspect declared,
                             mrhiTextureAspect bound)
{
    Reset();
    uint8_t* slot = s_sections[BINDINGS].bytes + SAMPLED_SLOT * 24;
    slot[9] = type;
    slot[10] = dimension;
    mrhiComputePipelineId pipeline = MakePipeline();
    BeginFrame();
    mrhiResourceId resource = ImportTexture(texture);
    mrhiAccess accesses[6];
    uint32_t count = DefaultAccesses(accesses);
    accesses[count] = Access(resource, kind);
    accesses[count].range.aspect = declared;
    Passes(accesses, count + 1);
    CHECK(mrhiSetComputePipeline(s_device, s_pass, pipeline) == mrhi_success, "set");
    mrhiBinding binding = {
        .slot = 0,
        .resource = resource,
        .viewKind = dimension,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING, .aspect = bound},
    };
    mrhiResult status = With1(0, binding);
    Drop();
    CHECK(mrhiDestroyComputePipeline(s_device, pipeline) == mrhi_success, "destroyed");
    return status;
}

// Sets table 1 with the storage slot's access and format changed,
// binding the first mip of a 2D array texture the pass declares with a
// kind: the result.
static mrhiResult Storage(mrhiStorageAccess access, mrhiFormat format, mrhiTextureId texture,
                          mrhiAccessKind kind)
{
    Reset();
    uint8_t* slot = s_sections[BINDINGS].bytes + 5 * 24;
    slot[11] = access;
    Put16(slot + 12, format);
    mrhiComputePipelineId pipeline = MakePipeline();
    BeginFrame();
    mrhiResourceId resource = ImportTexture(texture);
    mrhiAccess accesses[5];
    uint32_t count = DefaultAccesses(accesses);
    accesses[4] = Access(resource, kind);
    accesses[4].range.mipCount = 1;
    Passes(accesses, count);
    CHECK(mrhiSetComputePipeline(s_device, s_pass, pipeline) == mrhi_success, "set");
    mrhiBinding binding = {
        .slot = 1,
        .resource = resource,
        .viewKind = mrhi_texture2dArray,
        .range = {.mipCount = 1, .layerCount = MRHI_REMAINING},
    };
    mrhiResult status = With1(1, binding);
    Drop();
    CHECK(mrhiDestroyComputePipeline(s_device, pipeline) == mrhi_success, "destroyed");
    return status;
}

static void TestDeclaredKinds(void)
{
    Open();
    mrhiTextureId both =
        MakeTexture(mrhi_texture2d, mrhi_formatDepthStencil, 1, 1, mrhi_textureSampled);
    mrhiTextureAspect depth = mrhi_aspectDepthOnly;
    mrhiTextureAspect stencil = mrhi_aspectStencilOnly;
    CHECK(DeclaredAs(both, mrhi_texture2d, mrhi_sampleUint, mrhi_accessSampled, stencil, stencil) ==
              mrhi_success,
          "the stencil declared and bound");
    CHECK(DeclaredAs(both, mrhi_texture2d, mrhi_sampleUint, mrhi_accessSampled, depth, stencil) ==
              mrhi_errorInvalid,
          "the depth declared, the stencil bound");
    mrhiTextureAspect all = mrhi_aspectAll;
    mrhiTextureId array = MakeTexture(mrhi_texture2dArray, mrhi_formatRgba8Unorm, 4, 1,
                                      mrhi_textureStorage | mrhi_textureSampled);
    CHECK(DeclaredAs(array, mrhi_texture2dArray, mrhi_sampleFloat, mrhi_accessSampled, all, all) ==
              mrhi_success,
          "an array sampled");
    CHECK(DeclaredAs(array, mrhi_texture2dArray, mrhi_sampleFloat, mrhi_accessStorageRead, all,
                     all) == mrhi_errorInvalid,
          "not when declared read as storage");
    mrhiTextureId single =
        MakeTexture(mrhi_texture2dArray, mrhi_formatR32Float, 4, 1, mrhi_textureStorage);
    CHECK(Storage(mrhi_storageReadWrite, mrhi_formatR32Float, single,
                  mrhi_accessStorageReadWrite) == mrhi_success,
          "read and written, declared so");
    CHECK(Storage(mrhi_storageReadWrite, mrhi_formatR32Float, single, mrhi_accessStorageWrite) ==
              mrhi_errorInvalid,
          "declared written only");
    CHECK(Storage(mrhi_storageReadWrite, mrhi_formatR32Float, single, mrhi_accessStorageRead) ==
              mrhi_errorInvalid,
          "declared read only");
    CHECK(Storage(mrhi_storageReadOnly, mrhi_formatRgba8Unorm, s_array, mrhi_accessStorageRead) ==
              mrhi_success,
          "read, declared read");
    CHECK(Storage(mrhi_storageReadOnly, mrhi_formatRgba8Unorm, s_array,
                  mrhi_accessStorageReadWrite) == mrhi_success,
          "or read and written");
    CHECK(Storage(mrhi_storageReadOnly, mrhi_formatRgba8Unorm, s_array, mrhi_accessStorageWrite) ==
              mrhi_errorInvalid,
          "not declared written");
    CloseDevice();
}

// Sets table 0 in a pipeline whose sampler slot is of a kind, with a
// sampler: the result.
static mrhiResult Sampler(mrhiSamplerBinding kind, mrhiSamplerId sampler)
{
    Reset();
    s_sections[BINDINGS].bytes[SAMPLER_SLOT * 24 + 8] = kind;
    mrhiComputePipelineId pipeline = MakePipeline();
    Frame();
    CHECK(mrhiSetComputePipeline(s_device, s_pass, pipeline) == mrhi_success, "set");
    mrhiBinding binding = {.slot = SAMPLER_SLOT, .sampler = sampler};
    mrhiResult status = With0(3, binding);
    Drop();
    CHECK(mrhiDestroyComputePipeline(s_device, pipeline) == mrhi_success, "destroyed");
    return status;
}

static void TestSamplers(void)
{
    Open();
    CHECK(Sampler(mrhi_samplerFiltering, s_linear) == mrhi_success &&
              Sampler(mrhi_samplerFiltering, s_nearest) == mrhi_success,
          "a filtering slot takes either");
    CHECK(Sampler(mrhi_samplerFiltering, s_compare) == mrhi_errorInvalid, "but not a comparison");
    CHECK(Sampler(mrhi_samplerNonFiltering, s_nearest) == mrhi_success, "nearest");
    CHECK(Sampler(mrhi_samplerNonFiltering, s_linear) == mrhi_errorInvalid, "not linear");
    mrhiSamplerDef def = mrhiDefaultSamplerDef();
    def.mipFilter = mrhi_filterLinear;
    mrhiSamplerId mip = {0};
    CHECK(mrhiCreateSampler(s_device, &def, &mip) == mrhi_success, "linear between mips");
    CHECK(Sampler(mrhi_samplerNonFiltering, mip) == mrhi_errorInvalid, "not that either");
    def = mrhiDefaultSamplerDef();
    def.minFilter = mrhi_filterLinear;
    mrhiSamplerId minified = {0};
    CHECK(mrhiCreateSampler(s_device, &def, &minified) == mrhi_success, "linear minified");
    CHECK(Sampler(mrhi_samplerNonFiltering, minified) == mrhi_errorInvalid, "nor that");
    def = mrhiDefaultSamplerDef();
    def.magFilter = mrhi_filterLinear;
    mrhiSamplerId magnified = {0};
    CHECK(mrhiCreateSampler(s_device, &def, &magnified) == mrhi_success, "linear magnified");
    CHECK(Sampler(mrhi_samplerNonFiltering, magnified) == mrhi_errorInvalid, "nor this");
    CHECK(Sampler(mrhi_samplerComparison, s_compare) == mrhi_success, "a comparison");
    CHECK(Sampler(mrhi_samplerComparison, s_nearest) == mrhi_errorInvalid, "only a comparison");
    CloseDevice();
}

static void TestArena(void)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.deviceLimits.frameCommandBytes = MRHI_CHUNK_BYTES;
    s_device = OpenWith(def, true);
    Reset();
    s_pipeline = MakePipeline();
    s_uniform = MakeBuffer(1u << 17);
    s_written = MakeBuffer(1024);
    s_read = MakeBuffer(1024);
    s_cube = MakeTexture(mrhi_textureCube, mrhi_formatRgba8Unorm, 6, 1, mrhi_textureSampled);
    s_array = MakeTexture(mrhi_texture2dArray, mrhi_formatRgba8Unorm, 4, 1,
                          mrhi_textureStorage | mrhi_textureSampled);
    s_linear = MakeSampler(mrhi_filterLinear, mrhi_compareNone);
    Frame();
    mrhiBinding bindings[4];
    Table0(bindings);
    // The pipeline's record and 25 tables of five fill 126 of 127 records.
    for (uint32_t i = 0; i < 25; ++i)
    {
        CHECK(mrhiSetBindings(s_device, s_pass, 0, bindings, 4) == mrhi_success, "fits");
    }
    CHECK(mrhiSetBindings(s_device, s_pass, 0, bindings, 4) == mrhi_errorCapacity, "full");
    CHECK(mrhiSetBindings(s_device, s_pass, 2, nullptr, 0) == mrhi_errorCapacity, "nothing more");
    bindings[0].slot = 9;
    CHECK(mrhiSetBindings(s_device, s_pass, 0, bindings, 4) == mrhi_errorInvalid,
          "checked before room is sought");
    Drop();
    CloseDevice();
}

int main(void)
{
    ResetAdapter();
    TestTables();
    TestPipeline();
    TestBuffers();
    TestKinds();
    TestAccesses();
    TestTextures();
    TestDeclaredKinds();
    TestSamplers();
    TestArena();
    return s_failures == 0 ? 0 : 1;
}
