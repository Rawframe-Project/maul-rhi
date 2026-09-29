// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Draws and dispatches on a test driver device: vertex and index buffers
// set per slot, and each draw or dispatch refused unless its pipeline,
// the tables that pipeline reads and buffers large enough for its
// elements are set, as WebGPU refuses them.

#include "device_core.h"
#include "test_container.h"
#include "test_device_setup.h"

#include "maul-rhi/encoder.h"

static mrhiDevice* s_device;
static mrhiBufferId s_vertices;
static mrhiBufferId s_instances;
static mrhiBufferId s_indices;
static mrhiBufferId s_uniform;
static mrhiTextureId s_target;
static mrhiVertexBufferLayout s_layouts[2];
static mrhiVertexAttribute s_attributes[2];

// The open frame's resources and passes: one rendering to the target,
// one dispatching, and one copying.
static mrhiResourceId s_v;
static mrhiResourceId s_i;
static mrhiResourceId s_x;
static mrhiResourceId s_u;
static mrhiPassId s_render;
static mrhiPassId s_compute;
static mrhiPassId s_copy;

// A shader of the sections as they stand.
static mrhiShaderId MakeShader(void)
{
    Assemble();
    mrhiShaderDef def = mrhiDefaultShaderDef();
    def.bytes = s_container;
    def.byteCount = s_size;
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(s_device, &def, &shader) == mrhi_success, "a shader");
    return shader;
}

// Resets the sections to the default container without bindings.
static void Unbound(void)
{
    Reset();
    s_sections[BINDINGS].size = 0;
}

static void Answered(void)
{
    mrhiDeviceNotification record;
    CHECK(mrhiNextDeviceNotification(s_device, &record) == mrhi_success &&
              record.outcome == mrhi_success,
          "ready");
}

// A graphics def of the sections' shader: buffer 0 steps per vertex, 12
// bytes feeding location 3; buffer 1 per instance, 16 bytes of which an
// element's attribute reaches 8, at location 5.
static mrhiGraphicsPipelineDef GraphicsDef(mrhiShaderId shader)
{
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    s_layouts[0] = (mrhiVertexBufferLayout){.stride = 12, .stepMode = mrhi_stepVertex};
    s_layouts[1] = (mrhiVertexBufferLayout){.stride = 16, .stepMode = mrhi_stepInstance};
    s_attributes[0] = (mrhiVertexAttribute){.location = 3, .format = mrhi_vertexFloat32x3};
    s_attributes[1] =
        (mrhiVertexAttribute){.buffer = 1, .location = 5, .format = mrhi_vertexFloat32x2};
    def.vertexBuffers = s_layouts;
    def.vertexBufferCount = 2;
    def.vertexAttributes = s_attributes;
    def.vertexAttributeCount = 2;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    def.colorTargetCount = 1;
    return def;
}

static mrhiGraphicsPipelineId Graphics(const mrhiGraphicsPipelineDef* def)
{
    mrhiGraphicsPipelineId pipeline = {0};
    mrhiRequestId request;
    CHECK(mrhiCreateGraphicsPipeline(s_device, def, &pipeline, &request) == mrhi_success,
          "a graphics pipeline");
    Answered();
    return pipeline;
}

// A graphics pipeline of the sections as they stand.
static mrhiGraphicsPipelineId MakeGraphics(void)
{
    mrhiGraphicsPipelineDef def = GraphicsDef(MakeShader());
    mrhiGraphicsPipelineId pipeline = Graphics(&def);
    CHECK(mrhiDestroyShader(s_device, def.shader) == mrhi_success, "its shader destroyed");
    return pipeline;
}

// A compute pipeline of the sections as they stand.
static mrhiComputePipelineId MakeCompute(void)
{
    mrhiComputePipelineDef def = mrhiDefaultComputePipelineDef();
    def.shader = MakeShader();
    def.entry = "cs";
    def.entryLength = 2;
    mrhiComputePipelineId pipeline = {0};
    mrhiRequestId request;
    CHECK(mrhiCreateComputePipeline(s_device, &def, &pipeline, &request) == mrhi_success,
          "a compute pipeline");
    Answered();
    CHECK(mrhiDestroyShader(s_device, def.shader) == mrhi_success, "its shader destroyed");
    return pipeline;
}

static mrhiBufferId MakeBuffer(uint64_t size, mrhiBufferUsage usage)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage;
    mrhiBufferId buffer = {0};
    CHECK(mrhiCreateBuffer(s_device, &def, &buffer) == mrhi_success, "a buffer");
    return buffer;
}

// Opens a ready device with the buffers draws read: 10 vertices, 3
// instances, 64 bytes of indices, and a uniform buffer; and a target.
static void Open(void)
{
    s_device = OpenWith(mrhiDefaultDeviceDef(), true);
    s_vertices = MakeBuffer(120, mrhi_bufferVertex);
    s_instances = MakeBuffer(40, mrhi_bufferVertex);
    s_indices = MakeBuffer(64, mrhi_bufferIndex);
    s_uniform = MakeBuffer(256, mrhi_bufferUniform);
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = 16;
    def.height = 16;
    def.usage = mrhi_textureRenderTarget;
    CHECK(mrhiCreateTexture(s_device, &def, &s_target) == mrhi_success, "a target");
}

static mrhiResourceId Import(mrhiBufferId buffer)
{
    mrhiResourceId resource = {0};
    CHECK(mrhiImportBuffer(s_device, buffer, &resource) == mrhi_success, "imported");
    return resource;
}

static mrhiAccess Access(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){.resource = resource, .kind = kind};
}

// Opens a frame of the three passes, compiled and begun.
static void Frame(void)
{
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &frameDef) == mrhi_success, "begun");
    s_v = Import(s_vertices);
    s_i = Import(s_instances);
    s_x = Import(s_indices);
    s_u = Import(s_uniform);
    mrhiResourceId target = {0};
    CHECK(mrhiImportTexture(s_device, s_target, &target) == mrhi_success, "imported");
    mrhiAccess accesses[] = {
        Access(s_v, mrhi_accessVertex),
        Access(s_i, mrhi_accessVertex),
        Access(s_x, mrhi_accessIndex),
        Access(s_u, mrhi_accessUniform),
    };
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){.resource = target, .load = mrhi_loadClear};
    def.colorTargetCount = 1;
    def.accesses = accesses;
    def.accessCount = 4;
    CHECK(mrhiAddPass(s_device, &def, &s_render) == mrhi_success, "the render pass");
    // The compute pass declares the vertex and index buffers too, so that
    // only its lack of targets refuses them.
    def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.accesses = accesses;
    def.accessCount = 3;
    CHECK(mrhiAddPass(s_device, &def, &s_compute) == mrhi_success, "the compute pass");
    def.accesses = nullptr;
    def.accessCount = 0;
    def.passClass = mrhi_passTransfer;
    CHECK(mrhiAddPass(s_device, &def, &s_copy) == mrhi_success, "the copying pass");
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(mrhiBeginPass(s_device, s_render) == mrhi_success &&
              mrhiBeginPass(s_device, s_compute) == mrhi_success &&
              mrhiBeginPass(s_device, s_copy) == mrhi_success,
          "begun");
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

// The n-th record of a pass, walking its chunks: or NULL past them.
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

// Sets both vertex buffers whole in the render pass.
static void SetVertices(void)
{
    CHECK(mrhiSetVertexBuffer(s_device, s_render, 0, s_v, 0, MRHI_WHOLE_SIZE) == mrhi_success &&
              mrhiSetVertexBuffer(s_device, s_render, 1, s_i, 0, MRHI_WHOLE_SIZE) == mrhi_success,
          "the vertex buffers");
}

static void TestVertexBuffers(void)
{
    Open();
    Unbound();
    mrhiGraphicsPipelineId pipeline = MakeGraphics();
    Frame();
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_success, "set");
    CHECK(mrhiSetVertexBuffer(s_device, s_render, 0, s_v, 0, MRHI_WHOLE_SIZE) == mrhi_success,
          "buffer 0");
    const mrhiCommand* command = Nth(s_render, 1);
    CHECK(command != nullptr && command->type == mrhiCommandVertexBuffer && command->a == 0 &&
              command->b == s_v.index1 && command->c == 0 && command->d == 120,
          "recorded whole");
    CHECK(mrhiDraw(s_device, s_render, 3, 1, 0, 0) == mrhi_errorState, "buffer 1 not set");
    CHECK(mrhiSetVertexBuffer(s_device, s_render, 1, s_i, 0, 40) == mrhi_success, "buffer 1");
    command = Nth(s_render, 2);
    CHECK(command != nullptr && command->a == 1 && command->b == s_i.index1 && command->d == 40,
          "recorded in its slot");
    CHECK(mrhiDraw(s_device, s_render, 10, 3, 0, 0) == mrhi_success, "every vertex and instance");
    command = Nth(s_render, 3);
    CHECK(command != nullptr && command->type == mrhiCommandDraw && command->a == 10 &&
              command->b == 3 && command->c == 0 && command->d == 0,
          "recorded");
    CHECK(mrhiDraw(s_device, s_render, 5, 1, 5, 2) == mrhi_success, "the last of each");
    CHECK(Nth(s_render, 4)->c == 5 && Nth(s_render, 4)->d == 2, "recorded from them");
    CHECK(mrhiDraw(s_device, s_render, 0, 0, 0, 0) == mrhi_success, "none");
    CHECK(mrhiDraw(s_device, s_render, 0, 0, 10, 3) == mrhi_success, "none, from the end");
    CHECK(mrhiDraw(s_device, s_render, 11, 1, 0, 0) == mrhi_errorInvalid &&
              mrhiDraw(s_device, s_render, 5, 1, 6, 0) == mrhi_errorInvalid,
          "a vertex past the buffer");
    CHECK(mrhiDraw(s_device, s_render, 1, 4, 0, 0) == mrhi_errorInvalid &&
              mrhiDraw(s_device, s_render, 1, 1, 0, 3) == mrhi_errorInvalid &&
              mrhiDraw(s_device, s_render, 0, 0, 0, 4) == mrhi_errorInvalid,
          "an instance past it");
    CHECK(mrhiDraw(s_device, s_render, 1, 1, UINT32_MAX, 0) == mrhi_errorInvalid,
          "counted in 64 bits");
    CHECK(mrhiGetDeviceMisuse(s_device) == 6, "each counted");
    CHECK(mrhiSetVertexBuffer(s_device, s_render, 0, s_v, 12, MRHI_WHOLE_SIZE) == mrhi_success,
          "from the second vertex");
    CHECK(Nth(s_render, 7)->c == 12 && Nth(s_render, 7)->d == 108, "recorded from its offset");
    CHECK(mrhiDraw(s_device, s_render, 9, 1, 0, 0) == mrhi_success &&
              mrhiDraw(s_device, s_render, 10, 1, 0, 0) == mrhi_errorInvalid,
          "nine left");
    CHECK(mrhiSetVertexBuffer(s_device, s_render, 1, s_i, 0, 39) == mrhi_success, "39 bytes");
    CHECK(mrhiDraw(s_device, s_render, 1, 2, 0, 0) == mrhi_success &&
              mrhiDraw(s_device, s_render, 1, 3, 0, 0) == mrhi_errorInvalid,
          "the last instance needs its attribute's 8 bytes");
    CHECK(mrhiGetDeviceMisuse(s_device) == 8, "each counted");
    CHECK(mrhiSetVertexBuffer(s_device, s_render, 8, s_v, 0, 12) == mrhi_errorInvalid,
          "past the device's vertex buffers");
    CHECK(mrhiSetVertexBuffer(s_device, s_render, 0, s_v, 2, 12) == mrhi_errorInvalid,
          "an offset not a multiple of 4");
    CHECK(mrhiSetVertexBuffer(s_device, s_render, 0, s_v, 124, MRHI_WHOLE_SIZE) ==
              mrhi_errorInvalid,
          "an offset past it");
    CHECK(mrhiSetVertexBuffer(s_device, s_render, 0, s_v, 120, MRHI_WHOLE_SIZE) == mrhi_success,
          "nothing, at its end");
    CHECK(mrhiSetVertexBuffer(s_device, s_render, 0, s_v, 4, 120) == mrhi_errorInvalid,
          "a size past it");
    CHECK(mrhiSetVertexBuffer(s_device, s_render, 0, s_x, 0, 12) == mrhi_errorInvalid &&
              mrhiSetVertexBuffer(s_device, s_render, 0, s_x, 0, 0) == mrhi_errorInvalid,
          "a buffer declared for indices");
    CHECK(mrhiSetVertexBuffer(s_device, s_compute, 0, s_v, 0, 12) == mrhi_errorInvalid,
          "a pass without targets");
    CHECK(mrhiGetDeviceMisuse(s_device) == 15, "each counted");
    mrhiResourceId none = {0};
    CHECK(mrhiSetVertexBuffer(s_device, s_render, 0, none, 0, 12) == mrhi_errorStale, "none");
    CHECK(mrhiSetVertexBuffer(nullptr, s_render, 0, s_v, 0, 12) == mrhi_errorInvalid, "no device");
    CHECK(mrhiEndPass(s_device, s_render) == mrhi_success, "ended");
    CHECK(mrhiSetVertexBuffer(s_device, s_render, 0, s_v, 0, 12) == mrhi_errorState &&
              mrhiDraw(s_device, s_render, 0, 0, 0, 0) == mrhi_errorState,
          "not recording");
    Drop();
    Frame();
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_success, "set again");
    CHECK(mrhiDraw(s_device, s_render, 0, 0, 0, 0) == mrhi_errorState,
          "each pass starts without buffers");
    CHECK(mrhiSetVertexBuffer(s_device, s_render, 1, s_i, 0, 8) == mrhi_success &&
              mrhiDraw(s_device, s_render, 1, 3, 0, 0) == mrhi_errorState,
          "a buffer not set before one too small");
    Drop();
    CloseDevice();
}

// An instance buffer whose attributes reach 16 bytes of each element,
// the first listed reaching furthest.
static void TestAttributeReach(void)
{
    Open();
    Unbound();
    mrhiGraphicsPipelineDef def = GraphicsDef(MakeShader());
    mrhiVertexAttribute attributes[] = {
        s_attributes[0],
        {.buffer = 1, .offset = 8, .location = 5, .format = mrhi_vertexFloat32x2},
        {.buffer = 1, .offset = 0, .location = 6, .format = mrhi_vertexFloat32},
    };
    def.vertexAttributes = attributes;
    def.vertexAttributeCount = 3;
    mrhiGraphicsPipelineId pipeline = Graphics(&def);
    Frame();
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_success, "set");
    SetVertices();
    CHECK(mrhiDraw(s_device, s_render, 1, 2, 0, 0) == mrhi_success, "two instances in 40 bytes");
    CHECK(mrhiDraw(s_device, s_render, 1, 3, 0, 0) == mrhi_errorInvalid, "not three");
    Drop();
    CloseDevice();
}

static void TestIndices(void)
{
    Open();
    Unbound();
    mrhiGraphicsPipelineId pipeline = MakeGraphics();
    Frame();
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_success, "set");
    SetVertices();
    CHECK(mrhiDrawIndexed(s_device, s_render, 1, 1, 0, 0, 0) == mrhi_errorState, "no indices");
    CHECK(mrhiSetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 0, MRHI_WHOLE_SIZE) ==
              mrhi_success,
          "32 indices");
    const mrhiCommand* command = Nth(s_render, 3);
    CHECK(command != nullptr && command->type == mrhiCommandIndexBuffer &&
              command->a == mrhi_indexUint16 && command->b == s_x.index1 && command->d == 64,
          "recorded");
    CHECK(mrhiDrawIndexed(s_device, s_render, 32, 3, 0, -5, 0) == mrhi_success, "all of them");
    command = Nth(s_render, 4);
    CHECK(command != nullptr && command->type == mrhiCommandDrawIndexed && command->a == 32 &&
              command->b == 3 && command->c == (0xFFFFFFFBull << 32) && command->d == 0,
          "recorded with the base vertex");
    CHECK(mrhiDrawIndexed(s_device, s_render, 16, 1, 16, 0, 2) == mrhi_success, "the last half");
    CHECK(mrhiDrawIndexed(s_device, s_render, 33, 1, 0, 0, 0) == mrhi_errorInvalid &&
              mrhiDrawIndexed(s_device, s_render, 16, 1, 17, 0, 0) == mrhi_errorInvalid &&
              mrhiDrawIndexed(s_device, s_render, 1, 1, UINT32_MAX, 0, 0) == mrhi_errorInvalid,
          "an index past them");
    CHECK(mrhiDrawIndexed(s_device, s_render, 1, 4, 0, 0, 0) == mrhi_errorInvalid,
          "an instance past its buffer");
    CHECK(mrhiSetVertexBuffer(s_device, s_render, 0, s_v, 0, 0) == mrhi_success, "no vertices");
    CHECK(mrhiDrawIndexed(s_device, s_render, 32, 1, 0, 0, 0) == mrhi_success,
          "vertices are not counted for indices");
    CHECK(mrhiSetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint32, 4, MRHI_WHOLE_SIZE) ==
              mrhi_success,
          "15 wider indices");
    CHECK(mrhiDrawIndexed(s_device, s_render, 15, 1, 0, 0, 0) == mrhi_success &&
              mrhiDrawIndexed(s_device, s_render, 16, 1, 0, 0, 0) == mrhi_errorInvalid,
          "and not 16");
    CHECK(mrhiGetDeviceMisuse(s_device) == 5, "each counted");
    CHECK(mrhiSetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 2, 2) == mrhi_success,
          "an offset and size of the narrow width");
    CHECK(mrhiSetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 1, 2) == mrhi_errorInvalid,
          "an offset off the width");
    CHECK(mrhiSetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint32, 2, 4) == mrhi_errorInvalid,
          "off the wider width");
    CHECK(mrhiSetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 0, 3) == mrhi_errorInvalid,
          "a size off it");
    CHECK(mrhiSetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint32, 0, 6) == mrhi_errorInvalid,
          "off the wider width");
    CHECK(mrhiSetIndexBuffer(s_device, s_render, s_x, mrhi_indexNone, 0, 4) == mrhi_errorInvalid &&
              mrhiSetIndexBuffer(s_device, s_render, s_x, 3, 0, 4) == mrhi_errorInvalid,
          "no width");
    CHECK(mrhiSetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 66, MRHI_WHOLE_SIZE) ==
                  mrhi_errorInvalid &&
              mrhiSetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 2, 64) ==
                  mrhi_errorInvalid,
          "past it");
    CHECK(mrhiSetIndexBuffer(s_device, s_render, s_v, mrhi_indexUint16, 0, 4) == mrhi_errorInvalid,
          "a buffer declared for vertices");
    CHECK(mrhiSetIndexBuffer(s_device, s_compute, s_x, mrhi_indexUint16, 0, 4) == mrhi_errorInvalid,
          "a pass without targets");
    CHECK(mrhiGetDeviceMisuse(s_device) == 15, "each counted");
    mrhiResourceId none = {0};
    CHECK(mrhiSetIndexBuffer(s_device, s_render, none, mrhi_indexUint16, 0, 4) == mrhi_errorStale,
          "none");
    CHECK(mrhiSetIndexBuffer(nullptr, s_render, s_x, mrhi_indexUint16, 0, 4) == mrhi_errorInvalid &&
              mrhiDrawIndexed(nullptr, s_render, 0, 0, 0, 0, 0) == mrhi_errorInvalid,
          "no device");
    CHECK(mrhiEndPass(s_device, s_copy) == mrhi_success, "the copying pass ended");
    CHECK(mrhiSetIndexBuffer(s_device, s_copy, s_x, mrhi_indexUint16, 0, 4) == mrhi_errorState,
          "not recording");
    Drop();
    // A strip with a strip index format takes indices of that width.
    mrhiGraphicsPipelineDef def = GraphicsDef(MakeShader());
    def.topology = mrhi_topologyTriangleStrip;
    def.stripIndexFormat = mrhi_indexUint32;
    mrhiGraphicsPipelineId strip = Graphics(&def);
    Frame();
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, strip) == mrhi_success, "a strip");
    SetVertices();
    CHECK(mrhiSetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 0, MRHI_WHOLE_SIZE) ==
              mrhi_success,
          "narrow indices");
    CHECK(mrhiDrawIndexed(s_device, s_render, 3, 1, 0, 0, 0) == mrhi_errorState,
          "not the strip's width");
    CHECK(mrhiDraw(s_device, s_render, 3, 1, 0, 0) == mrhi_success, "drawn without them");
    CHECK(mrhiSetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint32, 0, MRHI_WHOLE_SIZE) ==
                  mrhi_success &&
              mrhiDrawIndexed(s_device, s_render, 3, 1, 0, 0, 0) == mrhi_success,
          "its width");
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_success &&
              mrhiDrawIndexed(s_device, s_render, 3, 1, 0, 0, 0) == mrhi_success,
          "any width for a list");
    Drop();
    CloseDevice();
}

static void TestTables(void)
{
    Open();
    // A container whose table 2 holds a uniform buffer.
    Unbound();
    Binding(0, 2, 0, mrhi_bindingUniformBuffer, mrhi_stageFragment);
    mrhiGraphicsPipelineId pipeline = MakeGraphics();
    mrhiGraphicsPipelineId same = MakeGraphics();
    Put64(s_sections[BINDINGS].bytes + 16, 16);
    mrhiGraphicsPipelineId other = MakeGraphics();
    Frame();
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_success, "set");
    SetVertices();
    CHECK(mrhiDraw(s_device, s_render, 1, 1, 0, 0) == mrhi_errorState, "its table not set");
    mrhiBinding binding = {.slot = 0, .resource = s_u, .size = 256};
    CHECK(mrhiSetBindings(s_device, s_render, 2, &binding, 1) == mrhi_success, "set");
    CHECK(mrhiDraw(s_device, s_render, 1, 1, 0, 0) == mrhi_success, "drawn");
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, same) == mrhi_success &&
              mrhiDraw(s_device, s_render, 1, 1, 0, 0) == mrhi_success,
          "by a pipeline of the same container");
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, other) == mrhi_success &&
              mrhiDraw(s_device, s_render, 1, 1, 0, 0) == mrhi_errorState,
          "not by one of another");
    CHECK(mrhiSetBindings(s_device, s_render, 2, &binding, 1) == mrhi_success &&
              mrhiDraw(s_device, s_render, 1, 1, 0, 0) == mrhi_success,
          "until set under it");
    CHECK(mrhiDestroyGraphicsPipeline(s_device, other) == mrhi_success, "destroyed");
    CHECK(mrhiDraw(s_device, s_render, 1, 1, 0, 0) == mrhi_errorStale &&
              mrhiDrawIndexed(s_device, s_render, 1, 1, 0, 0, 0) == mrhi_errorStale,
          "a destroyed pipeline");
    CHECK(mrhiDraw(s_device, s_compute, 1, 1, 0, 0) == mrhi_errorInvalid &&
              mrhiDrawIndexed(s_device, s_compute, 1, 1, 0, 0, 0) == mrhi_errorInvalid,
          "a pass without targets");
    CHECK(mrhiGetDeviceMisuse(s_device) == 2, "counted");
    Drop();
    Frame();
    CHECK(mrhiDraw(s_device, s_render, 1, 1, 0, 0) == mrhi_errorState &&
              mrhiDrawIndexed(s_device, s_render, 1, 1, 0, 0, 0) == mrhi_errorState,
          "no pipeline set");
    Drop();
    CloseDevice();
}

static void TestDispatch(void)
{
    Open();
    Unbound();
    mrhiComputePipelineId pipeline = MakeCompute();
    Binding(0, 3, 0, mrhi_bindingUniformBuffer, mrhi_stageCompute);
    mrhiComputePipelineId bound = MakeCompute();
    Frame();
    CHECK(mrhiDispatch(s_device, s_compute, 1, 1, 1) == mrhi_errorState, "no pipeline set");
    CHECK(mrhiSetComputePipeline(s_device, s_compute, pipeline) == mrhi_success, "set");
    CHECK(mrhiDispatch(s_device, s_compute, 4, 2, 1) == mrhi_success, "dispatched");
    const mrhiCommand* command = Nth(s_compute, 1);
    CHECK(command != nullptr && command->type == mrhiCommandDispatch && command->a == 4 &&
              command->b == 2 && command->c == 1,
          "recorded");
    uint32_t most = mrhiDefaultLimits().workgroupsPerDimension;
    CHECK(mrhiDispatch(s_device, s_compute, most, most, most) == mrhi_success, "at the limit");
    CHECK(mrhiDispatch(s_device, s_compute, 0, 0, 0) == mrhi_success, "nothing");
    CHECK(mrhiDispatch(s_device, s_compute, most + 1, 1, 1) == mrhi_errorInvalid &&
              mrhiDispatch(s_device, s_compute, 1, most + 1, 1) == mrhi_errorInvalid &&
              mrhiDispatch(s_device, s_compute, 1, 1, most + 1) == mrhi_errorInvalid,
          "past it");
    CHECK(mrhiDispatch(s_device, s_render, 1, 1, 1) == mrhi_errorInvalid &&
              mrhiDispatch(s_device, s_copy, 1, 1, 1) == mrhi_errorInvalid,
          "a pass with targets or copying");
    CHECK(mrhiGetDeviceMisuse(s_device) == 5, "each counted");
    CHECK(mrhiSetComputePipeline(s_device, s_compute, bound) == mrhi_success &&
              mrhiDispatch(s_device, s_compute, 1, 1, 1) == mrhi_errorState,
          "its table not set");
    CHECK(mrhiDestroyComputePipeline(s_device, bound) == mrhi_success &&
              mrhiDispatch(s_device, s_compute, 1, 1, 1) == mrhi_errorStale,
          "destroyed");
    CHECK(mrhiDispatch(nullptr, s_compute, 1, 1, 1) == mrhi_errorInvalid, "no device");
    CHECK(mrhiEndPass(s_device, s_compute) == mrhi_success &&
              mrhiDispatch(s_device, s_compute, 1, 1, 1) == mrhi_errorState,
          "not recording");
    Drop();
    CloseDevice();
}

static void TestArena(void)
{
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.deviceLimits.frameCommandBytes = MRHI_CHUNK_BYTES;
    s_device = OpenWith(deviceDef, true);
    s_vertices = MakeBuffer(120, mrhi_bufferVertex);
    s_instances = MakeBuffer(40, mrhi_bufferVertex);
    s_indices = MakeBuffer(64, mrhi_bufferIndex);
    s_uniform = MakeBuffer(256, mrhi_bufferUniform);
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = 16;
    def.height = 16;
    def.usage = mrhi_textureRenderTarget;
    CHECK(mrhiCreateTexture(s_device, &def, &s_target) == mrhi_success, "a target");
    Unbound();
    mrhiGraphicsPipelineId graphics = MakeGraphics();
    mrhiComputePipelineId compute = MakeCompute();
    Frame();
    CHECK(mrhiSetGraphicsPipeline(s_device, s_render, graphics) == mrhi_success, "set");
    SetVertices();
    CHECK(mrhiSetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 0, MRHI_WHOLE_SIZE) ==
              mrhi_success,
          "indices");
    // Four records so far; 123 draws fill the chunk.
    for (uint32_t i = 0; i < 123; ++i)
    {
        CHECK(mrhiDraw(s_device, s_render, 1, 1, 0, 0) == mrhi_success, "fits");
    }
    CHECK(mrhiDraw(s_device, s_render, 1, 1, 0, 0) == mrhi_errorCapacity &&
              mrhiDrawIndexed(s_device, s_render, 1, 1, 0, 0, 0) == mrhi_errorCapacity &&
              mrhiSetVertexBuffer(s_device, s_render, 0, s_v, 0, 12) == mrhi_errorCapacity &&
              mrhiSetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 0, 4) ==
                  mrhi_errorCapacity,
          "full");
    CHECK(mrhiSetComputePipeline(s_device, s_compute, compute) == mrhi_errorCapacity &&
              mrhiDispatch(s_device, s_compute, 1, 1, 1) == mrhi_errorCapacity,
          "for every pass");
    Drop();
    CloseDevice();
}

int main(void)
{
    ResetAdapter();
    TestVertexBuffers();
    TestAttributeReach();
    TestIndices();
    TestTables();
    TestDispatch();
    TestArena();
    return s_failures == 0 ? 0 : 1;
}
