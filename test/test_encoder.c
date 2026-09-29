// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Encoders on a test driver device: passes of a compiled frame begun
// once and ended before its submission, each command checked against
// its pass and written to the pass's chunks of the frame's arena, and a
// full arena refusing the frame.

#include "device_core.h"
#include "test_container.h"
#include "test_device_setup.h"

#include "maul-rhi/encoder.h"

#include <math.h>

static mrhiDevice* s_device;
static mrhiShaderId s_shader;
static mrhiGraphicsPipelineId s_graphics;
static mrhiComputePipelineId s_compute;
static mrhiVertexBufferLayout s_buffer;
static mrhiVertexAttribute s_attribute;
static mrhiTextureId s_depthTexture;

// The passes of the frame Frame opens: rendering to a 64 by 32 rgba8
// texture at mip 1, rendering to a depth texture only, dispatching, and
// copying.
static mrhiPassId s_render;
static mrhiPassId s_depth;
static mrhiPassId s_dispatch;
static mrhiPassId s_copy;
static mrhiPassId s_culled;

// A graphics def the render pass takes: one buffer feeding location 3
// of "vs", and "fs" writing an rgba8 target.
static mrhiGraphicsPipelineDef GraphicsDef(void)
{
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = s_shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    s_buffer = (mrhiVertexBufferLayout){.stride = 12, .stepMode = mrhi_stepVertex};
    s_attribute = (mrhiVertexAttribute){.location = 3, .format = mrhi_vertexFloat32x3};
    def.vertexBuffers = &s_buffer;
    def.vertexBufferCount = 1;
    def.vertexAttributes = &s_attribute;
    def.vertexAttributeCount = 1;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    def.colorTargetCount = 1;
    return def;
}

// A graphics pipeline of the def, answered.
static mrhiGraphicsPipelineId Graphics(const mrhiGraphicsPipelineDef* def)
{
    mrhiGraphicsPipelineId pipeline = {0};
    mrhiRequestId request;
    CHECK(mrhiCreateGraphicsPipeline(s_device, def, &pipeline, &request) == mrhi_success,
          "a graphics pipeline");
    mrhiDeviceNotification record;
    CHECK(mrhiNextDeviceNotification(s_device, &record) == mrhi_success, "answered");
    return pipeline;
}

// Opens a ready device whose frames hold chunks of commands, with the
// default container and a pipeline of each kind.
static void Open(uint32_t chunks)
{
    Reset();
    Assemble();
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.deviceLimits.frameCommandBytes = chunks * MRHI_CHUNK_BYTES;
    s_device = OpenWith(def, true);
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_container;
    shaderDef.byteCount = s_size;
    CHECK(mrhiCreateShader(s_device, &shaderDef, &s_shader) == mrhi_success, "the shader");
    mrhiGraphicsPipelineDef graphicsDef = GraphicsDef();
    s_graphics = Graphics(&graphicsDef);
    mrhiComputePipelineDef computeDef = mrhiDefaultComputePipelineDef();
    computeDef.shader = s_shader;
    computeDef.entry = "cs";
    computeDef.entryLength = 2;
    mrhiRequestId request;
    CHECK(mrhiCreateComputePipeline(s_device, &computeDef, &s_compute, &request) == mrhi_success,
          "a compute pipeline");
    mrhiDeviceNotification record;
    CHECK(mrhiNextDeviceNotification(s_device, &record) == mrhi_success, "answered");
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatDepthStencil;
    textureDef.width = 16;
    textureDef.height = 8;
    textureDef.usage = mrhi_textureRenderTarget;
    CHECK(mrhiCreateTexture(s_device, &textureDef, &s_depthTexture) == mrhi_success,
          "a depth texture");
}

// A declared rgba8 texture.
static mrhiResourceId Declare(uint32_t width, uint32_t height, uint32_t mips)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = width;
    def.height = height;
    def.mipLevels = mips;
    mrhiResourceId resource = {0};
    CHECK(mrhiDeclareTexture(s_device, &def, &resource) == mrhi_success, "declared");
    return resource;
}

static mrhiPassId Add(mrhiPassDef def)
{
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success, "a pass");
    return pass;
}

// Opens a frame of the five passes, compiled; the depth pass's target,
// the device's depth texture, read-only when asked.
static void Frame(bool readOnly)
{
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &frameDef) == mrhi_success, "begun");
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.colorTargets[0] = (mrhiColorTarget){
        .resource = Declare(128, 64, 2),
        .mip = 1,
        .load = mrhi_loadClear,
    };
    def.colorTargetCount = 1;
    s_render = Add(def);
    mrhiResourceId depth = {0};
    CHECK(mrhiImportTexture(s_device, s_depthTexture, &depth) == mrhi_success, "imported");
    def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.depthTarget = (mrhiDepthTarget){
        .resource = depth,
        .depthLoad = readOnly ? mrhi_loadKeep : mrhi_loadClear,
        .stencilLoad = readOnly ? mrhi_loadKeep : mrhi_loadClear,
        .readOnly = readOnly,
    };
    s_depth = Add(def);
    def = mrhiDefaultPassDef();
    def.neverCull = true;
    s_dispatch = Add(def);
    def.passClass = mrhi_passTransfer;
    s_copy = Add(def);
    def = mrhiDefaultPassDef();
    s_culled = Add(def);
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
}

// The command the n-th record of a pass holds, walking its chunks: or
// NULL past its records.
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

static void Close2(void)
{
    Close(s_device);
    s_device = nullptr;
}

static void TestPhases(void)
{
    Open(4);
    mrhiPassId early = {1, 1};
    CHECK(mrhiBeginPass(s_device, early) == mrhi_errorState, "no frame open");
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &frameDef) == mrhi_success, "begun");
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    mrhiPassId pass = Add(def);
    CHECK(mrhiBeginPass(s_device, pass) == mrhi_errorState, "not compiled");
    mrhiPassId past = {pass.index1 + 5, pass.generation};
    CHECK(mrhiBeginPass(s_device, past) == mrhi_errorState, "the phase checked before the id");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    Frame(false);
    CHECK(mrhiEndPass(s_device, s_dispatch) == mrhi_errorState, "not begun");
    CHECK(mrhiBeginPass(s_device, s_culled) == mrhi_errorState, "culled");
    CHECK(mrhiBeginPass(s_device, s_dispatch) == mrhi_success, "begun");
    CHECK(mrhiBeginPass(s_device, s_dispatch) == mrhi_errorState, "begun once");
    mrhiRequestId token;
    CHECK(mrhiSubmitFrame(s_device, &token) == mrhi_errorState, "a pass still recording");
    CHECK(mrhiEndPass(s_device, s_dispatch) == mrhi_success, "ended");
    CHECK(mrhiEndPass(s_device, s_dispatch) == mrhi_errorState, "ended once");
    CHECK(mrhiBeginPass(s_device, s_dispatch) == mrhi_errorState, "never begun again");
    CHECK(mrhiSetComputePipeline(s_device, s_dispatch, s_compute) == mrhi_errorState,
          "no command after its end");
    mrhiPassId stale = {s_dispatch.index1, s_dispatch.generation + 1};
    CHECK(mrhiBeginPass(s_device, stale) == mrhi_errorStale, "another frame's pass");
    stale = (mrhiPassId){0, s_dispatch.generation};
    CHECK(mrhiBeginPass(s_device, stale) == mrhi_errorStale, "a null pass");
    stale = (mrhiPassId){6, s_dispatch.generation};
    CHECK(mrhiBeginPass(s_device, stale) == mrhi_errorStale, "past the passes");
    CHECK(mrhiSubmitFrame(s_device, &token) == mrhi_success, "submitted");
    CHECK(mrhiBeginPass(s_device, s_render) == mrhi_errorState, "the frame is gone");
    Frame(false);
    CHECK(mrhiBeginPass(s_device, s_dispatch) == mrhi_success, "begun in the next frame");
    CHECK(Nth(s_dispatch, 0) == nullptr, "recording nothing yet");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped while recording");
    CHECK(mrhiBeginPass(nullptr, s_dispatch) == mrhi_errorInvalid &&
              mrhiEndPass(nullptr, s_dispatch) == mrhi_errorInvalid,
          "no device");
    CHECK(mrhiGetDeviceMisuse(s_device) == 0, "none of it misuse");
    Close2();
}

static void TestPipelines(void)
{
    Open(4);
    Frame(false);
    CHECK(mrhiBeginPass(s_device, s_render) == mrhi_success &&
              mrhiBeginPass(s_device, s_dispatch) == mrhi_success &&
              mrhiBeginPass(s_device, s_copy) == mrhi_success &&
              mrhiBeginPass(s_device, s_depth) == mrhi_success,
          "four passes begun");
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, s_graphics) == mrhi_success,
          "a graphics pipeline for its targets");
    const mrhiCommand* command = Nth(s_render, 0);
    CHECK(command != nullptr && command->type == mrhiCommandGraphicsPipeline &&
              command->a == s_device->pipelineSlots[s_graphics.index1 - 1].handle &&
              command->payload == 0,
          "recorded");
    CHECK(mrhiSetComputePipeline(s_device, s_dispatch, s_compute) == mrhi_success,
          "a compute pipeline");
    command = Nth(s_dispatch, 0);
    CHECK(command != nullptr && command->type == mrhiCommandComputePipeline &&
              command->a == s_device->pipelineSlots[s_compute.index1 - 1].handle,
          "recorded");
    CHECK(mrhiSetComputePipeline(s_device, s_render, s_compute) == mrhi_errorInvalid,
          "no dispatch in a render pass");
    mrhiGraphicsPipelineId unknown = {0};
    CHECK(mrhiSetGraphicsPipeline(s_device, s_dispatch, s_graphics) == mrhi_errorInvalid &&
              mrhiSetGraphicsPipeline(s_device, s_dispatch, unknown) == mrhi_errorInvalid,
          "no draw without targets, whatever the pipeline");
    CHECK(mrhiSetComputePipeline(s_device, s_copy, s_compute) == mrhi_errorInvalid &&
              mrhiSetGraphicsPipeline(s_device, s_copy, s_graphics) == mrhi_errorInvalid,
          "neither in a transfer pass");
    CHECK(mrhiSetGraphicsPipeline(s_device, s_depth, s_graphics) == mrhi_errorInvalid,
          "another pass's targets");
    CHECK(mrhiGetDeviceMisuse(s_device) == 6, "each counted");
    mrhiGraphicsPipelineId wrongKind = {s_compute.index1, s_compute.generation};
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, wrongKind) == mrhi_errorStale,
          "a compute pipeline's id");
    mrhiComputePipelineId dead = {s_compute.index1, s_compute.generation + 2};
    CHECK(mrhiSetComputePipeline(s_device, s_dispatch, dead) == mrhi_errorStale, "a dead id");
    mrhiComputePipelineId none = {0};
    CHECK(mrhiSetComputePipeline(s_device, s_dispatch, none) == mrhi_errorStale, "a null id");
    mrhiGraphicsPipelineDef def = GraphicsDef();
    mrhiGraphicsPipelineId pending;
    mrhiRequestId request;
    CHECK(mrhiCreateGraphicsPipeline(s_device, &def, &pending, &request) == mrhi_success,
          "a pipeline still compiling");
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, pending) == mrhi_errorState, "not ready");
    mrhiDeviceNotification record;
    CHECK(mrhiNextDeviceNotification(s_device, &record) == mrhi_success, "answered");
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, pending) == mrhi_success, "ready now");
    CHECK(Nth(s_render, 1) != nullptr && Nth(s_render, 2) == nullptr, "two records");
    // The stream keeps the handle: a slot freed and taken again while the
    // frame records names another pipeline.
    uint64_t handle = Nth(s_render, 1)->a;
    CHECK(mrhiDestroyGraphicsPipeline(s_device, pending) == mrhi_success &&
              mrhiCreateGraphicsPipeline(s_device, &def, &pending, &request) == mrhi_success,
          "destroyed and its slot taken again");
    CHECK(pending.index1 != 0 && s_device->pipelineSlots[pending.index1 - 1].handle != handle &&
              Nth(s_render, 1)->a == handle,
          "the recorded handle unchanged");
    CHECK(mrhiNextDeviceNotification(s_device, &record) == mrhi_success, "answered");
    CHECK(mrhiGetDeviceMisuse(s_device) == 6, "none of that misuse");
    CHECK(mrhiSetGraphicsPipeline(nullptr, s_render, s_graphics) == mrhi_errorInvalid &&
              mrhiSetComputePipeline(nullptr, s_dispatch, s_compute) == mrhi_errorInvalid,
          "no device");
    CHECK(mrhiEndPass(s_device, s_render) == mrhi_success, "ended");
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, s_graphics) == mrhi_errorState &&
              mrhiSetComputePipeline(s_device, s_render, s_compute) == mrhi_errorState,
          "not recording");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    Close2();
}

// Sets a graphics pipeline of the def in the depth pass of a frame
// whose depth target is read-only when asked: the result.
static mrhiResult InDepthPass(const mrhiGraphicsPipelineDef* def, bool readOnly)
{
    mrhiGraphicsPipelineId pipeline = Graphics(def);
    Frame(readOnly);
    CHECK(mrhiBeginPass(s_device, s_depth) == mrhi_success, "begun");
    mrhiResult status = mrhiSetGraphicsPipeline(s_device, s_depth, pipeline);
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    CHECK(mrhiDestroyGraphicsPipeline(s_device, pipeline) == mrhi_success, "destroyed");
    return status;
}

static void TestTargets(void)
{
    Open(4);
    // A depth-only pipeline: "fs" writes location 0, so no fragment entry.
    mrhiGraphicsPipelineDef def = GraphicsDef();
    def.fragmentEntry = nullptr;
    def.fragmentEntryLength = 0;
    def.colorTargetCount = 0;
    def.colorTargets[0].format = mrhi_formatNone;
    def.depthStencilFormat = mrhi_formatDepthStencil;
    def.depthCompare = mrhi_compareLess;
    CHECK(InDepthPass(&def, false) == mrhi_success, "a depth-only pipeline");
    CHECK(InDepthPass(&def, true) == mrhi_success, "tests read-only depth");
    def.depthWrite = true;
    CHECK(InDepthPass(&def, false) == mrhi_success, "writes depth");
    CHECK(InDepthPass(&def, true) == mrhi_errorInvalid, "but not read-only depth");
    def.depthWrite = false;
    def.stencilWriteMask = 0xFF;
    def.stencilFront.passOp = mrhi_stencilReplace;
    CHECK(InDepthPass(&def, false) == mrhi_success, "writes stencil");
    CHECK(InDepthPass(&def, true) == mrhi_errorInvalid, "but not read-only stencil");
    def.cullMode = mrhi_cullFront;
    CHECK(InDepthPass(&def, true) == mrhi_success, "unless its writing face is culled");
    def.cullMode = mrhi_cullBack;
    def.stencilFront.passOp = mrhi_stencilKeep;
    def.stencilBack.depthFailOp = mrhi_stencilZero;
    CHECK(InDepthPass(&def, true) == mrhi_success, "the back face culled");
    def.cullMode = mrhi_cullNone;
    CHECK(InDepthPass(&def, true) == mrhi_errorInvalid, "the back face written");
    def.stencilBack.depthFailOp = mrhi_stencilKeep;
    def.stencilBack.failOp = mrhi_stencilInvert;
    CHECK(InDepthPass(&def, true) == mrhi_errorInvalid, "on a failed test");
    def.stencilWriteMask = 0;
    CHECK(InDepthPass(&def, true) == mrhi_success, "masked");
    def = GraphicsDef();
    def.fragmentEntry = nullptr;
    def.fragmentEntryLength = 0;
    def.colorTargetCount = 0;
    def.colorTargets[0].format = mrhi_formatNone;
    def.depthStencilFormat = mrhi_formatDepth32Float;
    def.depthCompare = mrhi_compareLess;
    CHECK(InDepthPass(&def, false) == mrhi_errorInvalid, "another depth format");
    def.depthStencilFormat = mrhi_formatDepthStencil;
    def.sampleCount = 4;
    CHECK(InDepthPass(&def, false) == mrhi_errorInvalid, "another sample count");
    def = GraphicsDef();
    def.depthStencilFormat = mrhi_formatDepthStencil;
    def.depthCompare = mrhi_compareLess;
    CHECK(InDepthPass(&def, false) == mrhi_errorInvalid, "a color target the pass lacks");
    Frame(false);
    CHECK(mrhiBeginPass(s_device, s_render) == mrhi_success, "the render pass");
    mrhiGraphicsPipelineId pipeline = Graphics(&def);
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_errorInvalid,
          "a depth target the pass lacks");
    def = GraphicsDef();
    def.colorTargets[0].format = mrhi_formatRg8Unorm;
    pipeline = Graphics(&def);
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_errorInvalid,
          "another color format");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    Close2();
}

static void TestViewport(void)
{
    Open(4);
    Frame(false);
    CHECK(mrhiBeginPass(s_device, s_render) == mrhi_success &&
              mrhiBeginPass(s_device, s_dispatch) == mrhi_success,
          "begun");
    mrhiViewport viewport = {.width = 64, .height = 32, .maxDepth = 1};
    CHECK(mrhiSetViewport(s_device, s_render, &viewport) == mrhi_success, "the targets");
    const mrhiCommand* command = Nth(s_render, 0);
    const mrhiViewport* stored = (const mrhiViewport*)Nth(s_render, 1);
    CHECK(command != nullptr && command->type == mrhiCommandViewport && command->payload == 1 &&
              stored != nullptr && memcmp(stored, &viewport, sizeof(viewport)) == 0,
          "recorded with the viewport after it");
    float limit = 8192.0f;
    mrhiViewport edges[] = {
        {.x = -2 * limit, .y = -2 * limit, .width = limit, .height = limit, .maxDepth = 1},
        {.x = limit - 1, .y = limit - 1, .width = limit, .height = limit},
        {.minDepth = 0.5f, .maxDepth = 0.5f},
        {.minDepth = 0, .maxDepth = 1},
    };
    for (size_t i = 0; i < sizeof(edges) / sizeof(edges[0]); ++i)
    {
        CHECK(mrhiSetViewport(s_device, s_render, &edges[i]) == mrhi_success, "at an edge");
    }
    mrhiViewport past[] = {
        {.x = -2 * limit - 1, .width = 1, .height = 1, .maxDepth = 1},
        {.y = -2 * limit - 1, .width = 1, .height = 1, .maxDepth = 1},
        {.width = limit + 1, .height = 1, .maxDepth = 1},
        {.width = 1, .height = limit + 1, .maxDepth = 1},
        {.width = -1, .height = 1, .maxDepth = 1},
        {.width = 1, .height = -1, .maxDepth = 1},
        {.x = limit, .width = limit, .height = 1, .maxDepth = 1},
        {.y = limit, .width = 1, .height = limit, .maxDepth = 1},
        {.width = 1, .height = 1, .minDepth = -0.25f, .maxDepth = 1},
        {.width = 1, .height = 1, .maxDepth = 1.25f},
        {.width = 1, .height = 1, .minDepth = 0.75f, .maxDepth = 0.5f},
        {.x = NAN, .width = 1, .height = 1, .maxDepth = 1},
        {.y = NAN, .width = 1, .height = 1, .maxDepth = 1},
        {.width = INFINITY, .height = 1, .maxDepth = 1},
        {.width = 1, .height = NAN, .maxDepth = 1},
        {.width = 1, .height = 1, .minDepth = NAN, .maxDepth = 1},
        {.width = 1, .height = 1, .maxDepth = NAN},
    };
    uint32_t count = (uint32_t)(sizeof(past) / sizeof(past[0]));
    for (uint32_t i = 0; i < count; ++i)
    {
        CHECK(mrhiSetViewport(s_device, s_render, &past[i]) == mrhi_errorInvalid, "past a range");
    }
    CHECK(mrhiGetDeviceMisuse(s_device) == count, "each counted");
    CHECK(mrhiSetViewport(s_device, s_dispatch, &viewport) == mrhi_errorInvalid,
          "no viewport without targets");
    CHECK(mrhiSetViewport(s_device, s_render, nullptr) == mrhi_errorInvalid &&
              mrhiSetViewport(nullptr, s_render, &viewport) == mrhi_errorInvalid,
          "NULL arguments");
    CHECK(mrhiSetViewport(s_device, s_copy, &viewport) == mrhi_errorState, "not begun");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    Close2();
}

static void TestScissorAndState(void)
{
    Open(4);
    Frame(false);
    CHECK(mrhiBeginPass(s_device, s_render) == mrhi_success &&
              mrhiBeginPass(s_device, s_depth) == mrhi_success &&
              mrhiBeginPass(s_device, s_dispatch) == mrhi_success,
          "begun");
    mrhiScissorRect rect = {.x = 60, .y = 30, .width = 4, .height = 2};
    CHECK(mrhiSetScissor(s_device, s_render, &rect) == mrhi_success, "the mip's corner");
    const mrhiCommand* command = Nth(s_render, 0);
    CHECK(command != nullptr && command->type == mrhiCommandScissor && command->a == 60 &&
              command->b == 30 && command->c == 4 && command->d == 2 && command->payload == 0,
          "recorded");
    rect.width = 5;
    CHECK(mrhiSetScissor(s_device, s_render, &rect) == mrhi_errorInvalid, "past its width");
    rect.width = 4;
    rect.height = 3;
    CHECK(mrhiSetScissor(s_device, s_render, &rect) == mrhi_errorInvalid, "past its height");
    rect = (mrhiScissorRect){.x = UINT32_MAX, .width = 2, .height = 1};
    CHECK(mrhiSetScissor(s_device, s_render, &rect) == mrhi_errorInvalid, "wrapping across");
    rect = (mrhiScissorRect){.y = UINT32_MAX, .width = 1, .height = 2};
    CHECK(mrhiSetScissor(s_device, s_render, &rect) == mrhi_errorInvalid, "wrapping down");
    rect = (mrhiScissorRect){.width = 16, .height = 8};
    CHECK(mrhiSetScissor(s_device, s_depth, &rect) == mrhi_success, "a depth target's size");
    rect.width = 17;
    CHECK(mrhiSetScissor(s_device, s_depth, &rect) == mrhi_errorInvalid, "past it");
    rect = (mrhiScissorRect){0};
    CHECK(mrhiSetScissor(s_device, s_depth, &rect) == mrhi_success, "empty");
    CHECK(mrhiSetScissor(s_device, s_dispatch, &rect) == mrhi_errorInvalid,
          "no scissor without targets");
    CHECK(mrhiSetScissor(s_device, s_render, nullptr) == mrhi_errorInvalid &&
              mrhiSetScissor(nullptr, s_render, &rect) == mrhi_errorInvalid,
          "NULL arguments");
    CHECK(mrhiGetDeviceMisuse(s_device) == 7, "each counted");
    mrhiClearColor color = {0.25f, 0.5f, 0.75f, 1.0f};
    CHECK(mrhiSetBlendConstant(s_device, s_render, &color) == mrhi_success, "a blend constant");
    command = Nth(s_render, 1);
    const mrhiClearColor* stored = (const mrhiClearColor*)Nth(s_render, 2);
    CHECK(command != nullptr && command->type == mrhiCommandBlendConstant &&
              command->payload == 1 && stored != nullptr &&
              memcmp(stored, &color, sizeof(color)) == 0,
          "recorded with the color after it");
    color = (mrhiClearColor){-2.0f, 3.0f, 0.0f, 0.0f};
    CHECK(mrhiSetBlendConstant(s_device, s_render, &color) == mrhi_success, "out of 0 to 1");
    float bad[] = {NAN, INFINITY, -INFINITY};
    for (uint32_t i = 0; i < 12; ++i)
    {
        float channels[4] = {0};
        channels[i % 4] = bad[i / 4];
        color = (mrhiClearColor){channels[0], channels[1], channels[2], channels[3]};
        CHECK(mrhiSetBlendConstant(s_device, s_render, &color) == mrhi_errorInvalid, "not finite");
    }
    CHECK(mrhiSetBlendConstant(s_device, s_dispatch, &color) == mrhi_errorInvalid &&
              mrhiSetBlendConstant(s_device, s_render, nullptr) == mrhi_errorInvalid &&
              mrhiSetBlendConstant(nullptr, s_render, &color) == mrhi_errorInvalid,
          "no targets, no color, no device");
    CHECK(mrhiGetDeviceMisuse(s_device) == 7 + 12 + 2, "each counted");
    CHECK(mrhiSetStencilReference(s_device, s_depth, 0xAB) == mrhi_success, "a reference");
    command = Nth(s_depth, 2);
    CHECK(command != nullptr && command->type == mrhiCommandStencilReference && command->a == 0xAB,
          "recorded");
    CHECK(mrhiSetStencilReference(s_device, s_dispatch, 1) == mrhi_errorInvalid &&
              mrhiSetStencilReference(nullptr, s_depth, 1) == mrhi_errorInvalid,
          "no targets, no device");
    CHECK(mrhiSetStencilReference(s_device, s_copy, 1) == mrhi_errorState, "not begun");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    Close2();
}

static void TestRootBlock(void)
{
    Open(4);
    Frame(false);
    CHECK(mrhiBeginPass(s_device, s_render) == mrhi_success &&
              mrhiBeginPass(s_device, s_dispatch) == mrhi_success &&
              mrhiBeginPass(s_device, s_copy) == mrhi_success,
          "begun");
    uint8_t bytes[128];
    for (uint32_t i = 0; i < sizeof(bytes); ++i)
    {
        bytes[i] = (uint8_t)i;
    }
    CHECK(mrhiSetRootBlock(s_device, s_dispatch, 0, bytes, 64) == mrhi_success, "all of it");
    const mrhiCommand* command = Nth(s_dispatch, 0);
    CHECK(command != nullptr && command->type == mrhiCommandRootBlock && command->a == 0 &&
              command->b == 64 && command->payload == 2 &&
              memcmp(Nth(s_dispatch, 1), bytes, 32) == 0 &&
              memcmp(Nth(s_dispatch, 2), bytes + 32, 32) == 0,
          "recorded with its bytes after it");
    CHECK(mrhiSetRootBlock(s_device, s_render, 60, bytes, 4) == mrhi_success, "its last word");
    command = Nth(s_render, 0);
    CHECK(command != nullptr && command->a == 60 && command->b == 4 && command->payload == 1 &&
              memcmp(Nth(s_render, 1), bytes, 4) == 0,
          "in one payload record");
    CHECK(mrhiSetRootBlock(s_device, s_render, 60, bytes, 8) == mrhi_errorUnsupported,
          "past the limit");
    CHECK(mrhiSetRootBlock(s_device, s_render, 0, bytes, 68) == mrhi_errorUnsupported,
          "larger than it");
    CHECK(mrhiSetRootBlock(s_device, s_render, UINT32_MAX - 3, bytes, 4) == mrhi_errorUnsupported,
          "wrapping past it");
    CHECK(mrhiGetDeviceMisuse(s_device) == 0, "not misuse");
    CHECK(mrhiSetRootBlock(s_device, s_render, 2, bytes, 4) == mrhi_errorInvalid &&
              mrhiSetRootBlock(s_device, s_render, 0, bytes, 6) == mrhi_errorInvalid &&
              mrhiSetRootBlock(s_device, s_render, 0, bytes, 0) == mrhi_errorInvalid &&
              mrhiSetRootBlock(s_device, s_copy, 0, bytes, 4) == mrhi_errorInvalid &&
              mrhiSetRootBlock(s_device, s_render, 0, nullptr, 4) == mrhi_errorInvalid,
          "unaligned, empty, in a transfer pass, or without bytes");
    CHECK(mrhiGetDeviceMisuse(s_device) == 5, "each counted");
    CHECK(mrhiSetRootBlock(nullptr, s_render, 0, bytes, 4) == mrhi_errorInvalid, "no device");
    CHECK(mrhiSetRootBlock(s_device, s_depth, 0, bytes, 4) == mrhi_errorState, "not begun");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    Close2();
}

static void TestDebugGroups(void)
{
    Open(4);
    Frame(false);
    CHECK(mrhiBeginPass(s_device, s_copy) == mrhi_success, "begun");
    CHECK(mrhiPopDebugGroup(s_device, s_copy) == mrhi_errorInvalid, "no group open");
    CHECK(mrhiPushDebugGroup(s_device, s_copy, "copies", 6) == mrhi_success &&
              mrhiPushDebugGroup(s_device, s_copy, "inner", 5) == mrhi_success,
          "two groups in a transfer pass");
    CHECK(mrhiInsertDebugMarker(s_device, s_copy, "here", 4) == mrhi_success, "a marker");
    const mrhiCommand* command = Nth(s_copy, 0);
    CHECK(command != nullptr && command->type == mrhiCommandPushDebugGroup && command->b == 6 &&
              command->payload == 1 && memcmp(Nth(s_copy, 1), "copies", 6) == 0,
          "recorded with its label");
    command = Nth(s_copy, 4);
    CHECK(command != nullptr && command->type == mrhiCommandDebugMarker && command->b == 4 &&
              memcmp(Nth(s_copy, 5), "here", 4) == 0,
          "the marker recorded");
    CHECK(mrhiEndPass(s_device, s_copy) == mrhi_errorInvalid, "groups still open");
    CHECK(mrhiPopDebugGroup(s_device, s_copy) == mrhi_success, "one closed");
    CHECK(mrhiEndPass(s_device, s_copy) == mrhi_errorInvalid, "one still open");
    CHECK(mrhiPopDebugGroup(s_device, s_copy) == mrhi_success, "both closed");
    command = Nth(s_copy, 7);
    CHECK(command != nullptr && command->type == mrhiCommandPopDebugGroup &&
              Nth(s_copy, 8) == nullptr,
          "the pops recorded");
    CHECK(mrhiEndPass(s_device, s_copy) == mrhi_success, "ended");
    CHECK(mrhiGetDeviceMisuse(s_device) == 3, "each refusal counted");
    CHECK(mrhiBeginPass(s_device, s_dispatch) == mrhi_success, "another pass");
    char label[257];
    memset(label, 'a', sizeof(label));
    CHECK(mrhiInsertDebugMarker(s_device, s_dispatch, label, 256) == mrhi_success,
          "the longest label");
    command = Nth(s_dispatch, 0);
    CHECK(command != nullptr && command->payload == 8, "in eight payload records");
    CHECK(mrhiInsertDebugMarker(s_device, s_dispatch, label, 257) == mrhi_errorInvalid &&
              mrhiPushDebugGroup(s_device, s_dispatch, label, 0) == mrhi_errorInvalid &&
              mrhiPushDebugGroup(s_device, s_dispatch, nullptr, 4) == mrhi_errorInvalid &&
              mrhiInsertDebugMarker(s_device, s_dispatch, "\xFF", 1) == mrhi_errorInvalid,
          "too long, empty, missing or not text");
    CHECK(mrhiGetDeviceMisuse(s_device) == 7, "each counted");
    CHECK(mrhiPushDebugGroup(nullptr, s_dispatch, "a", 1) == mrhi_errorInvalid &&
              mrhiInsertDebugMarker(nullptr, s_dispatch, "a", 1) == mrhi_errorInvalid &&
              mrhiPopDebugGroup(nullptr, s_dispatch) == mrhi_errorInvalid,
          "no device");
    CHECK(mrhiPushDebugGroup(s_device, s_copy, "a", 1) == mrhi_errorState &&
              mrhiPopDebugGroup(s_device, s_copy) == mrhi_errorState,
          "the pass ended");
    CHECK(mrhiEndPass(s_device, s_dispatch) == mrhi_success, "ended");
    mrhiRequestId token;
    CHECK(mrhiSubmitFrame(s_device, &token) == mrhi_success, "submitted");
    Close2();
}

static void TestArena(void)
{
    Open(2);
    Frame(false);
    CHECK(mrhiBeginPass(s_device, s_render) == mrhi_success &&
              mrhiBeginPass(s_device, s_dispatch) == mrhi_success,
          "begun");
    // The render pass takes the first chunk, the dispatch pass the second.
    uint32_t perChunk = (uint32_t)MRHI_CHUNK_COMMANDS;
    for (uint32_t i = 0; i < perChunk - 2; ++i)
    {
        CHECK(mrhiSetStencilReference(s_device, s_render, i) == mrhi_success, "fills");
    }
    CHECK(mrhiSetComputePipeline(s_device, s_dispatch, s_compute) == mrhi_success, "a chunk");
    CHECK(Nth(s_render, perChunk - 3)->a == perChunk - 3, "the render pass's last record");
    uint8_t bytes[64] = {0};
    CHECK(mrhiSetRootBlock(s_device, s_render, 0, bytes, 64) == mrhi_errorCapacity,
          "three records past its chunk, and no third chunk");
    CHECK(mrhiSetStencilReference(s_device, s_render, 0) == mrhi_errorCapacity,
          "nothing after, though a record would fit");
    CHECK(Nth(s_render, perChunk - 2) == nullptr, "nothing recorded");
    // The dispatch pass's chunk holds a record: 63 more root blocks of a
    // record each and their payload fill it.
    for (uint32_t i = 0; i < (perChunk - 1) / 2; ++i)
    {
        CHECK(mrhiSetRootBlock(s_device, s_dispatch, 0, bytes, 4) == mrhi_success,
              "the other pass fills its chunk");
    }
    CHECK(mrhiSetRootBlock(s_device, s_dispatch, 0, bytes, 4) == mrhi_errorCapacity,
          "and finds the arena full");
    CHECK(mrhiEndPass(s_device, s_render) == mrhi_success &&
              mrhiEndPass(s_device, s_dispatch) == mrhi_success,
          "ended");
    mrhiRequestId token;
    CHECK(mrhiSubmitFrame(s_device, &token) == mrhi_errorCapacity, "refused");
    CHECK(mrhiGetDeviceMisuse(s_device) == 0, "not misuse");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    Frame(false);
    CHECK(mrhiBeginPass(s_device, s_copy) == mrhi_success, "the arena again");
    // Markers of two records: 63 to a chunk, the last record of each
    // left unused rather than splitting one.
    uint32_t markers = 2 * (perChunk / 2);
    for (uint32_t i = 0; i < markers; ++i)
    {
        CHECK(mrhiInsertDebugMarker(s_device, s_copy, "m", 1) == mrhi_success, "fills it");
    }
    const mrhiFramePass* copy = &s_device->framePasses[s_copy.index1 - 1];
    CHECK(copy->firstChunk == 1 && copy->lastChunk == 2 && s_device->frameChunks[0].next == 2 &&
              s_device->frameChunks[0].count == perChunk - 1 &&
              Nth(s_copy, 2 * markers - 1) != nullptr && Nth(s_copy, 2 * markers) == nullptr,
          "two chunks linked");
    CHECK(mrhiInsertDebugMarker(s_device, s_copy, "m", 1) == mrhi_errorCapacity, "full");
    CHECK(mrhiPushDebugGroup(s_device, s_copy, "g", 1) == mrhi_errorCapacity,
          "a group without room");
    CHECK(mrhiEndPass(s_device, s_copy) == mrhi_errorInvalid, "still counted open");
    CHECK(mrhiPopDebugGroup(s_device, s_copy) == mrhi_errorCapacity, "closed without room");
    CHECK(mrhiEndPass(s_device, s_copy) == mrhi_success, "ended");
    CHECK(mrhiSubmitFrame(s_device, &token) == mrhi_errorCapacity, "refused once more");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    Frame(false);
    CHECK(mrhiBeginPass(s_device, s_copy) == mrhi_success &&
              mrhiInsertDebugMarker(s_device, s_copy, "m", 1) == mrhi_success &&
              mrhiEndPass(s_device, s_copy) == mrhi_success,
          "each frame takes the arena afresh");
    CHECK(s_device->framePasses[s_copy.index1 - 1].firstChunk == 1 && Nth(s_copy, 2) == nullptr,
          "from its first chunk, linked to nothing");
    CHECK(mrhiSubmitFrame(s_device, &token) == mrhi_success, "submitted");
    Close2();
}

static void TestLimit(void)
{
    CHECK(mrhiDefaultDeviceDef().deviceLimits.frameCommandBytes == 1u << 20,
          "a megabyte by default");
    Open(1);
    Frame(false);
    CHECK(mrhiBeginPass(s_device, s_dispatch) == mrhi_success &&
              mrhiSetComputePipeline(s_device, s_dispatch, s_compute) == mrhi_success &&
              mrhiEndPass(s_device, s_dispatch) == mrhi_success,
          "one chunk records");
    mrhiRequestId token;
    CHECK(mrhiSubmitFrame(s_device, &token) == mrhi_success, "submitted");
    Close2();
}

// Pipelines whose driver failed them: never set.
static void TestFailed(void)
{
    s_adapter.pipelineOutcome = mrhi_errorPlatform;
    Open(1);
    Frame(false);
    CHECK(mrhiBeginPass(s_device, s_render) == mrhi_success &&
              mrhiBeginPass(s_device, s_dispatch) == mrhi_success,
          "begun");
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, s_graphics) == mrhi_errorState &&
              mrhiSetComputePipeline(s_device, s_dispatch, s_compute) == mrhi_errorState,
          "failed");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    Close2();
}

int main(void)
{
    ResetAdapter();
    TestPhases();
    TestPipelines();
    TestTargets();
    TestViewport();
    TestScissorAndState();
    TestRootBlock();
    TestDebugGroups();
    TestArena();
    TestLimit();
    TestFailed();
    return s_failures == 0 ? 0 : 1;
}
