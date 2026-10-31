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
static mrhiBufferId s_arguments;
static mrhiTextureId s_target;
static mrhiVertexBufferLayout s_layouts[2];
static mrhiVertexAttribute s_attributes[2];

// The open frame's resources and passes: one rendering to the target,
// one dispatching, and one copying.
static mrhiResourceId s_v;
static mrhiResourceId s_i;
static mrhiResourceId s_x;
static mrhiResourceId s_u;
static mrhiResourceId s_a;
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
    CHECK(mrhiDevice_TakeNotification(s_device, &record) == mrhi_success &&
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
// The check that refused the device's latest refusal, draining its
// diagnostics; 0 when there is none.
static mrhiDiagnosticCode Refusal(void)
{
    mrhiDiagnosticCode code = 0;
    mrhiDiagnostic record;
    while (mrhiDevice_TakeDiagnostic(s_device, &record) == mrhi_success)
    {
        code = record.code;
    }
    return code;
}

// instances, 64 bytes of indices, a uniform buffer and 64 bytes of
// indirect arguments; and a target.
static void OpenLimited(mrhiDeviceDef deviceDef)
{
    deviceDef.budgets.diagnostics = 8;
    s_device = OpenWith(deviceDef, true);
    s_vertices = MakeBuffer(120, mrhi_bufferVertex);
    s_instances = MakeBuffer(40, mrhi_bufferVertex);
    s_indices = MakeBuffer(64, mrhi_bufferIndex);
    s_uniform = MakeBuffer(256, mrhi_bufferUniform);
    s_arguments = MakeBuffer(64, mrhi_bufferIndirect);
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = 16;
    def.height = 16;
    def.usage = mrhi_textureRenderTarget;
    CHECK(mrhiCreateTexture(s_device, &def, &s_target) == mrhi_success, "a target");
}

static void Open(void)
{
    OpenLimited(mrhiDefaultDeviceDef());
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
    CHECK(mrhiDevice_BeginFrame(s_device, &frameDef) == mrhi_success, "begun");
    s_v = Import(s_vertices);
    s_i = Import(s_instances);
    s_x = Import(s_indices);
    s_u = Import(s_uniform);
    s_a = Import(s_arguments);
    mrhiResourceId target = {0};
    CHECK(mrhiImportTexture(s_device, s_target, &target) == mrhi_success, "imported");
    mrhiAccess accesses[] = {
        Access(s_v, mrhi_accessVertex),  Access(s_i, mrhi_accessVertex),
        Access(s_x, mrhi_accessIndex),   Access(s_a, mrhi_accessIndirect),
        Access(s_u, mrhi_accessUniform),
    };
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){.resource = target, .load = mrhi_loadClear};
    def.colorTargetCount = 1;
    def.accesses = accesses;
    def.accessCount = 5;
    CHECK(mrhiAddPass(s_device, &def, &s_render) == mrhi_success, "the render pass");
    // The compute pass declares the vertex, index and indirect buffers
    // too, so that only its lack of targets refuses them.
    def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.accesses = accesses;
    def.accessCount = 4;
    CHECK(mrhiAddPass(s_device, &def, &s_compute) == mrhi_success, "the compute pass");
    def.accesses = nullptr;
    def.accessCount = 0;
    def.passClass = mrhi_passTransfer;
    CHECK(mrhiAddPass(s_device, &def, &s_copy) == mrhi_success, "the copying pass");
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(mrhiPass_Begin(s_device, s_render) == mrhi_success &&
              mrhiPass_Begin(s_device, s_compute) == mrhi_success &&
              mrhiPass_Begin(s_device, s_copy) == mrhi_success,
          "begun");
}

static void Drop(void)
{
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success, "dropped");
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
    CHECK(mrhiPass_SetVertexBuffer(s_device, s_render, 0, s_v, 0, MRHI_WHOLE_SIZE) ==
                  mrhi_success &&
              mrhiPass_SetVertexBuffer(s_device, s_render, 1, s_i, 0, MRHI_WHOLE_SIZE) ==
                  mrhi_success,
          "the vertex buffers");
}

static void TestVertexBuffers(void)
{
    // An adapter of twice the device's vertex buffers.
    s_adapter.limits.vertexBuffers = 16;
    Open();
    Unbound();
    mrhiGraphicsPipelineId pipeline = MakeGraphics();
    Frame();
    CHECK(mrhiPass_SetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_success, "set");
    CHECK(mrhiPass_SetVertexBuffer(s_device, s_render, 0, s_v, 0, MRHI_WHOLE_SIZE) == mrhi_success,
          "buffer 0");
    const mrhiCommand* command = Nth(s_render, 1);
    CHECK(command != nullptr && command->type == mrhiCommandVertexBuffer && command->a == 0 &&
              command->b == s_v.index1 && command->c == 0 && command->d == 120,
          "recorded whole");
    CHECK(mrhiPass_Draw(s_device, s_render, 3, 1, 0, 0) == mrhi_errorState, "buffer 1 not set");
    CHECK(mrhiPass_SetVertexBuffer(s_device, s_render, 1, s_i, 0, 40) == mrhi_success, "buffer 1");
    command = Nth(s_render, 2);
    CHECK(command != nullptr && command->a == 1 && command->b == s_i.index1 && command->d == 40,
          "recorded in its slot");
    CHECK(mrhiPass_Draw(s_device, s_render, 10, 3, 0, 0) == mrhi_success,
          "every vertex and instance");
    command = Nth(s_render, 3);
    CHECK(command != nullptr && command->type == mrhiCommandDraw && command->a == 10 &&
              command->b == 3 && command->c == 0 && command->d == 0,
          "recorded");
    CHECK(mrhiPass_Draw(s_device, s_render, 5, 1, 5, 2) == mrhi_success, "the last of each");
    CHECK(Nth(s_render, 4)->c == 5 && Nth(s_render, 4)->d == 2, "recorded from them");
    CHECK(mrhiPass_Draw(s_device, s_render, 0, 0, 0, 0) == mrhi_success, "none");
    CHECK(mrhiPass_Draw(s_device, s_render, 0, 0, 10, 3) == mrhi_success, "none, from the end");
    CHECK(mrhiPass_Draw(s_device, s_render, 11, 1, 0, 0) == mrhi_errorInvalid &&
              mrhiPass_Draw(s_device, s_render, 5, 1, 6, 0) == mrhi_errorInvalid,
          "a vertex past the buffer");
    CHECK(mrhiPass_Draw(s_device, s_render, 1, 4, 0, 0) == mrhi_errorInvalid &&
              mrhiPass_Draw(s_device, s_render, 1, 1, 0, 3) == mrhi_errorInvalid &&
              mrhiPass_Draw(s_device, s_render, 0, 0, 0, 4) == mrhi_errorInvalid,
          "an instance past it");
    CHECK(mrhiPass_Draw(s_device, s_render, 1, 1, UINT32_MAX, 0) == mrhi_errorInvalid,
          "counted in 64 bits");
    CHECK(mrhiDevice_GetMisuse(s_device) == 6, "each counted");
    CHECK(mrhiPass_SetVertexBuffer(s_device, s_render, 0, s_v, 12, MRHI_WHOLE_SIZE) == mrhi_success,
          "from the second vertex");
    CHECK(Nth(s_render, 7)->c == 12 && Nth(s_render, 7)->d == 108, "recorded from its offset");
    CHECK(mrhiPass_Draw(s_device, s_render, 9, 1, 0, 0) == mrhi_success &&
              mrhiPass_Draw(s_device, s_render, 10, 1, 0, 0) == mrhi_errorInvalid,
          "nine left");
    CHECK(mrhiPass_SetVertexBuffer(s_device, s_render, 1, s_i, 0, 39) == mrhi_success, "39 bytes");
    CHECK(mrhiPass_Draw(s_device, s_render, 1, 2, 0, 0) == mrhi_success &&
              mrhiPass_Draw(s_device, s_render, 1, 3, 0, 0) == mrhi_errorInvalid,
          "the last instance needs its attribute's 8 bytes");
    CHECK(mrhiDevice_GetMisuse(s_device) == 8, "each counted");
    CHECK(mrhiPass_SetVertexBuffer(s_device, s_render, 8, s_v, 0, 12) == mrhi_errorCapacity &&
              Refusal() == mrhi_diagnosticLimitVertexBuffers,
          "past the device's vertex buffers, within the adapter's, named");
    CHECK(mrhiPass_SetVertexBuffer(s_device, s_render, 16, s_v, 0, 12) == mrhi_errorUnsupported &&
              Refusal() == mrhi_diagnosticLimitVertexBuffers,
          "past the adapter's");
    CHECK(mrhiPass_SetVertexBuffer(s_device, s_render, 0, s_v, 2, 12) == mrhi_errorInvalid &&
              Refusal() == mrhi_diagnosticTransferAlignment,
          "an offset not a multiple of 4");
    CHECK(mrhiPass_SetVertexBuffer(s_device, s_render, 0, s_v, 124, MRHI_WHOLE_SIZE) ==
              mrhi_errorInvalid,
          "an offset past it");
    CHECK(mrhiPass_SetVertexBuffer(s_device, s_render, 0, s_v, 120, MRHI_WHOLE_SIZE) ==
              mrhi_success,
          "nothing, at its end");
    CHECK(mrhiPass_SetVertexBuffer(s_device, s_render, 0, s_v, 4, 120) == mrhi_errorInvalid &&
              Refusal() == mrhi_diagnosticTransferRange,
          "a size past it");
    CHECK(mrhiPass_SetVertexBuffer(s_device, s_render, 0, s_x, 0, 12) == mrhi_errorInvalid &&
              mrhiPass_SetVertexBuffer(s_device, s_render, 0, s_x, 0, 0) == mrhi_errorInvalid &&
              Refusal() == mrhi_diagnosticUndeclaredAccess,
          "a buffer declared for indices");
    CHECK(mrhiPass_SetVertexBuffer(s_device, s_compute, 0, s_v, 0, 12) == mrhi_errorInvalid &&
              Refusal() == mrhi_diagnosticRenderStateOutsideRenderPass,
          "a pass without targets");
    CHECK(mrhiDevice_GetMisuse(s_device) == 14, "each invalid one counted");
    mrhiResourceId none = {0};
    CHECK(mrhiPass_SetVertexBuffer(s_device, s_render, 0, none, 0, 12) == mrhi_errorStale, "none");
    CHECK(mrhiPass_SetVertexBuffer(nullptr, s_render, 0, s_v, 0, 12) == mrhi_errorInvalid,
          "no device");
    CHECK(mrhiPass_End(s_device, s_render) == mrhi_success, "ended");
    CHECK(mrhiPass_SetVertexBuffer(s_device, s_render, 0, s_v, 0, 12) == mrhi_errorState &&
              mrhiPass_Draw(s_device, s_render, 0, 0, 0, 0) == mrhi_errorState,
          "not recording");
    Drop();
    Frame();
    CHECK(mrhiPass_SetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_success, "set again");
    CHECK(mrhiPass_Draw(s_device, s_render, 0, 0, 0, 0) == mrhi_errorState,
          "each pass starts without buffers");
    CHECK(mrhiPass_SetVertexBuffer(s_device, s_render, 1, s_i, 0, 8) == mrhi_success &&
              mrhiPass_Draw(s_device, s_render, 1, 3, 0, 0) == mrhi_errorState,
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
    CHECK(mrhiPass_SetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_success, "set");
    SetVertices();
    CHECK(mrhiPass_Draw(s_device, s_render, 1, 2, 0, 0) == mrhi_success,
          "two instances in 40 bytes");
    CHECK(mrhiPass_Draw(s_device, s_render, 1, 3, 0, 0) == mrhi_errorInvalid, "not three");
    Drop();
    CloseDevice();
}

static void TestIndices(void)
{
    Open();
    Unbound();
    mrhiGraphicsPipelineId pipeline = MakeGraphics();
    Frame();
    CHECK(mrhiPass_SetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_success, "set");
    SetVertices();
    CHECK(mrhiPass_DrawIndexed(s_device, s_render, 1, 1, 0, 0, 0) == mrhi_errorState, "no indices");
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 0, MRHI_WHOLE_SIZE) ==
              mrhi_success,
          "32 indices");
    const mrhiCommand* command = Nth(s_render, 3);
    CHECK(command != nullptr && command->type == mrhiCommandIndexBuffer &&
              command->a == mrhi_indexUint16 && command->b == s_x.index1 && command->d == 64,
          "recorded");
    CHECK(mrhiPass_DrawIndexed(s_device, s_render, 32, 3, 0, -5, 0) == mrhi_success, "all of them");
    command = Nth(s_render, 4);
    CHECK(command != nullptr && command->type == mrhiCommandDrawIndexed && command->a == 32 &&
              command->b == 3 && command->c == (0xFFFFFFFBull << 32) && command->d == 0,
          "recorded with the base vertex");
    CHECK(mrhiPass_DrawIndexed(s_device, s_render, 16, 1, 16, 0, 2) == mrhi_success,
          "the last half");
    CHECK(mrhiPass_DrawIndexed(s_device, s_render, 33, 1, 0, 0, 0) == mrhi_errorInvalid &&
              mrhiPass_DrawIndexed(s_device, s_render, 16, 1, 17, 0, 0) == mrhi_errorInvalid &&
              mrhiPass_DrawIndexed(s_device, s_render, 1, 1, UINT32_MAX, 0, 0) == mrhi_errorInvalid,
          "an index past them");
    CHECK(mrhiPass_DrawIndexed(s_device, s_render, 1, 4, 0, 0, 0) == mrhi_errorInvalid,
          "an instance past its buffer");
    CHECK(mrhiPass_SetVertexBuffer(s_device, s_render, 0, s_v, 0, 0) == mrhi_success,
          "no vertices");
    CHECK(mrhiPass_DrawIndexed(s_device, s_render, 32, 1, 0, 0, 0) == mrhi_success,
          "vertices are not counted for indices");
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint32, 4, MRHI_WHOLE_SIZE) ==
              mrhi_success,
          "15 wider indices");
    CHECK(mrhiPass_DrawIndexed(s_device, s_render, 15, 1, 0, 0, 0) == mrhi_success &&
              mrhiPass_DrawIndexed(s_device, s_render, 16, 1, 0, 0, 0) == mrhi_errorInvalid,
          "and not 16");
    CHECK(mrhiDevice_GetMisuse(s_device) == 5, "each counted");
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 2, 2) == mrhi_success,
          "an offset and size of the narrow width");
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 1, 2) ==
              mrhi_errorInvalid,
          "an offset off the width");
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint32, 2, 4) ==
              mrhi_errorInvalid,
          "off the wider width");
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 0, 3) ==
              mrhi_errorInvalid,
          "a size off it");
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint32, 0, 6) ==
              mrhi_errorInvalid,
          "off the wider width");
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexNone, 0, 4) ==
                  mrhi_errorInvalid &&
              mrhiPass_SetIndexBuffer(s_device, s_render, s_x, 3, 0, 4) == mrhi_errorInvalid,
          "no width");
    // Formats are int32_t: a check against the last format alone would let a
    // negative one through.
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, -1, 0, 4) == mrhi_errorInvalid &&
              mrhiPass_SetIndexBuffer(s_device, s_render, s_x, INT32_MIN, 0, 4) ==
                  mrhi_errorInvalid,
          "a negative format");
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 66, MRHI_WHOLE_SIZE) ==
                  mrhi_errorInvalid &&
              mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 2, 64) ==
                  mrhi_errorInvalid,
          "past it");
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_v, mrhi_indexUint16, 0, 4) ==
              mrhi_errorInvalid,
          "a buffer declared for vertices");
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_compute, s_x, mrhi_indexUint16, 0, 4) ==
              mrhi_errorInvalid,
          "a pass without targets");
    CHECK(mrhiDevice_GetMisuse(s_device) == 17, "each counted");
    mrhiResourceId none = {0};
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, none, mrhi_indexUint16, 0, 4) ==
              mrhi_errorStale,
          "none");
    CHECK(mrhiPass_SetIndexBuffer(nullptr, s_render, s_x, mrhi_indexUint16, 0, 4) ==
                  mrhi_errorInvalid &&
              mrhiPass_DrawIndexed(nullptr, s_render, 0, 0, 0, 0, 0) == mrhi_errorInvalid,
          "no device");
    CHECK(mrhiPass_End(s_device, s_copy) == mrhi_success, "the copying pass ended");
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_copy, s_x, mrhi_indexUint16, 0, 4) == mrhi_errorState,
          "not recording");
    Drop();
    // A strip with a strip index format takes indices of that width.
    mrhiGraphicsPipelineDef def = GraphicsDef(MakeShader());
    def.topology = mrhi_topologyTriangleStrip;
    def.stripIndexFormat = mrhi_indexUint32;
    mrhiGraphicsPipelineId strip = Graphics(&def);
    Frame();
    CHECK(mrhiPass_SetGraphicsPipeline(s_device, s_render, strip) == mrhi_success, "a strip");
    SetVertices();
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 0, MRHI_WHOLE_SIZE) ==
              mrhi_success,
          "narrow indices");
    CHECK(mrhiPass_DrawIndexed(s_device, s_render, 3, 1, 0, 0, 0) == mrhi_errorState,
          "not the strip's width");
    CHECK(mrhiPass_Draw(s_device, s_render, 3, 1, 0, 0) == mrhi_success, "drawn without them");
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint32, 0, MRHI_WHOLE_SIZE) ==
                  mrhi_success &&
              mrhiPass_DrawIndexed(s_device, s_render, 3, 1, 0, 0, 0) == mrhi_success,
          "its width");
    CHECK(mrhiPass_SetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_success &&
              mrhiPass_DrawIndexed(s_device, s_render, 3, 1, 0, 0, 0) == mrhi_success,
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
    CHECK(mrhiPass_SetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_success, "set");
    SetVertices();
    CHECK(mrhiPass_Draw(s_device, s_render, 1, 1, 0, 0) == mrhi_errorState, "its table not set");
    mrhiBinding binding = {.slot = 0, .resource = s_u, .size = 256};
    CHECK(mrhiPass_SetBindings(s_device, s_render, 2, &binding, 1) == mrhi_success, "set");
    CHECK(mrhiPass_Draw(s_device, s_render, 1, 1, 0, 0) == mrhi_success, "drawn");
    CHECK(mrhiPass_SetGraphicsPipeline(s_device, s_render, same) == mrhi_success &&
              mrhiPass_Draw(s_device, s_render, 1, 1, 0, 0) == mrhi_success,
          "by a pipeline of the same container");
    CHECK(mrhiPass_SetGraphicsPipeline(s_device, s_render, other) == mrhi_success &&
              mrhiPass_Draw(s_device, s_render, 1, 1, 0, 0) == mrhi_errorState,
          "not by one of another");
    CHECK(mrhiPass_SetBindings(s_device, s_render, 2, &binding, 1) == mrhi_success &&
              mrhiPass_Draw(s_device, s_render, 1, 1, 0, 0) == mrhi_success,
          "until set under it");
    CHECK(mrhiDestroyGraphicsPipeline(s_device, other) == mrhi_success, "destroyed");
    CHECK(mrhiPass_Draw(s_device, s_render, 1, 1, 0, 0) == mrhi_errorStale &&
              mrhiPass_DrawIndexed(s_device, s_render, 1, 1, 0, 0, 0) == mrhi_errorStale,
          "a destroyed pipeline");
    CHECK(mrhiPass_Draw(s_device, s_compute, 1, 1, 0, 0) == mrhi_errorInvalid &&
              mrhiPass_DrawIndexed(s_device, s_compute, 1, 1, 0, 0, 0) == mrhi_errorInvalid,
          "a pass without targets");
    CHECK(mrhiDevice_GetMisuse(s_device) == 2, "counted");
    Drop();
    Frame();
    CHECK(mrhiPass_Draw(s_device, s_render, 1, 1, 0, 0) == mrhi_errorState &&
              mrhiPass_DrawIndexed(s_device, s_render, 1, 1, 0, 0, 0) == mrhi_errorState,
          "no pipeline set");
    Drop();
    CloseDevice();
}

static void TestDispatch(void)
{
    // An adapter of twice the device's workgroups.
    s_adapter.limits.workgroupsPerDimension = 2 * mrhiDefaultLimits().workgroupsPerDimension;
    Open();
    Unbound();
    mrhiComputePipelineId pipeline = MakeCompute();
    Binding(0, 3, 0, mrhi_bindingUniformBuffer, mrhi_stageCompute);
    mrhiComputePipelineId bound = MakeCompute();
    Frame();
    CHECK(mrhiPass_Dispatch(s_device, s_compute, 1, 1, 1) == mrhi_errorState, "no pipeline set");
    CHECK(mrhiPass_SetComputePipeline(s_device, s_compute, pipeline) == mrhi_success, "set");
    CHECK(mrhiPass_Dispatch(s_device, s_compute, 4, 2, 1) == mrhi_success, "dispatched");
    const mrhiCommand* command = Nth(s_compute, 1);
    CHECK(command != nullptr && command->type == mrhiCommandDispatch && command->a == 4 &&
              command->b == 2 && command->c == 1,
          "recorded");
    uint32_t most = mrhiDefaultLimits().workgroupsPerDimension;
    CHECK(mrhiPass_Dispatch(s_device, s_compute, most, most, most) == mrhi_success, "at the limit");
    CHECK(mrhiPass_Dispatch(s_device, s_compute, 0, 0, 0) == mrhi_success, "nothing");
    CHECK(mrhiPass_Dispatch(s_device, s_compute, most + 1, 1, 1) == mrhi_errorCapacity &&
              mrhiPass_Dispatch(s_device, s_compute, 1, most + 1, 1) == mrhi_errorCapacity &&
              mrhiPass_Dispatch(s_device, s_compute, 1, 1, most + 1) == mrhi_errorCapacity &&
              Refusal() == mrhi_diagnosticLimitWorkgroupsPerDimension,
          "past the device's limit, within the adapter's");
    CHECK(mrhiPass_Dispatch(s_device, s_compute, 2 * most + 1, 1, 1) == mrhi_errorUnsupported &&
              mrhiPass_Dispatch(s_device, s_compute, 1, 2 * most + 1, most + 1) ==
                  mrhi_errorUnsupported &&
              mrhiPass_Dispatch(s_device, s_compute, 1, 1, UINT32_MAX) == mrhi_errorUnsupported &&
              Refusal() == mrhi_diagnosticLimitWorkgroupsPerDimension,
          "past the adapter's");
    CHECK(mrhiPass_Dispatch(s_device, s_render, 1, 1, 1) == mrhi_errorInvalid &&
              mrhiPass_Dispatch(s_device, s_copy, 1, 1, 1) == mrhi_errorInvalid,
          "a pass with targets or copying");
    CHECK(mrhiDevice_GetMisuse(s_device) == 2, "each invalid one counted");
    CHECK(mrhiPass_SetComputePipeline(s_device, s_compute, bound) == mrhi_success &&
              mrhiPass_Dispatch(s_device, s_compute, 1, 1, 1) == mrhi_errorState,
          "its table not set");
    CHECK(mrhiDestroyComputePipeline(s_device, bound) == mrhi_success &&
              mrhiPass_Dispatch(s_device, s_compute, 1, 1, 1) == mrhi_errorStale,
          "destroyed");
    CHECK(mrhiPass_Dispatch(nullptr, s_compute, 1, 1, 1) == mrhi_errorInvalid, "no device");
    CHECK(mrhiPass_End(s_device, s_compute) == mrhi_success &&
              mrhiPass_Dispatch(s_device, s_compute, 1, 1, 1) == mrhi_errorState,
          "not recording");
    Drop();
    CloseDevice();
}

static void TestIndirectDraws(void)
{
    Open();
    Unbound();
    mrhiGraphicsPipelineId pipeline = MakeGraphics();
    Frame();
    CHECK(mrhiPass_DrawIndirect(s_device, s_render, s_a, 0) == mrhi_errorState &&
              mrhiPass_DrawIndexedIndirect(s_device, s_render, s_a, 0) == mrhi_errorState,
          "no pipeline set");
    CHECK(mrhiPass_SetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_success, "set");
    CHECK(mrhiPass_DrawIndirect(s_device, s_render, s_a, 0) == mrhi_errorState,
          "no vertex buffers");
    SetVertices();
    CHECK(mrhiPass_DrawIndexedIndirect(s_device, s_render, s_a, 0) == mrhi_errorState,
          "no indices");
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 0, MRHI_WHOLE_SIZE) ==
              mrhi_success,
          "indices");
    CHECK(mrhiPass_DrawIndirect(s_device, s_render, s_a, 48) == mrhi_success, "the last arguments");
    const mrhiCommand* command = Nth(s_render, 4);
    CHECK(command != nullptr && command->type == mrhiCommandDrawIndirect &&
              command->a == s_a.index1 && command->b == 0 && command->c == 48 && command->d == 0,
          "recorded");
    CHECK(mrhiPass_DrawIndexedIndirect(s_device, s_render, s_a, 44) == mrhi_success,
          "the last indexed arguments");
    command = Nth(s_render, 5);
    CHECK(command != nullptr && command->type == mrhiCommandDrawIndexedIndirect &&
              command->a == s_a.index1 && command->c == 44,
          "recorded");
    CHECK(mrhiPass_DrawIndirect(s_device, s_render, s_a, 52) == mrhi_errorInvalid &&
              mrhiPass_DrawIndexedIndirect(s_device, s_render, s_a, 48) == mrhi_errorInvalid,
          "arguments past the buffer");
    CHECK(mrhiPass_DrawIndirect(s_device, s_render, s_a, UINT64_MAX - 3) == mrhi_errorInvalid &&
              mrhiPass_DrawIndexedIndirect(s_device, s_render, s_a, 68) == mrhi_errorInvalid,
          "an offset past it");
    CHECK(mrhiPass_DrawIndirect(s_device, s_render, s_a, 2) == mrhi_errorInvalid &&
              mrhiPass_DrawIndexedIndirect(s_device, s_render, s_a, 6) == mrhi_errorInvalid,
          "an offset off 4");
    CHECK(mrhiPass_DrawIndirect(s_device, s_render, s_v, 0) == mrhi_errorInvalid &&
              mrhiPass_DrawIndexedIndirect(s_device, s_render, s_u, 0) == mrhi_errorInvalid,
          "a buffer declared for another access");
    CHECK(mrhiPass_DrawIndirect(s_device, s_compute, s_a, 0) == mrhi_errorInvalid &&
              mrhiPass_DrawIndexedIndirect(s_device, s_copy, s_a, 0) == mrhi_errorInvalid,
          "a pass without targets");
    CHECK(mrhiDevice_GetMisuse(s_device) == 10, "each counted");
    mrhiResourceId none = {0};
    CHECK(mrhiPass_DrawIndirect(s_device, s_render, none, 0) == mrhi_errorStale &&
              mrhiPass_DrawIndexedIndirect(s_device, s_render, none, 0) == mrhi_errorStale,
          "none");
    CHECK(mrhiPass_DrawIndirect(nullptr, s_render, s_a, 0) == mrhi_errorInvalid &&
              mrhiPass_DrawIndexedIndirect(nullptr, s_render, s_a, 0) == mrhi_errorInvalid,
          "no device");
    CHECK(mrhiDevice_GetMisuse(s_device) == 10, "neither counted");
    CHECK(mrhiPass_End(s_device, s_render) == mrhi_success &&
              mrhiPass_DrawIndirect(s_device, s_render, s_a, 0) == mrhi_errorState &&
              mrhiPass_DrawIndexedIndirect(s_device, s_render, s_a, 0) == mrhi_errorState,
          "not recording");
    Drop();
    // A strip with a strip index format takes indices of that width.
    mrhiGraphicsPipelineDef def = GraphicsDef(MakeShader());
    def.topology = mrhi_topologyTriangleStrip;
    def.stripIndexFormat = mrhi_indexUint32;
    mrhiGraphicsPipelineId strip = Graphics(&def);
    Frame();
    CHECK(mrhiPass_SetGraphicsPipeline(s_device, s_render, strip) == mrhi_success, "a strip");
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint32, 0, MRHI_WHOLE_SIZE) ==
                  mrhi_success &&
              mrhiPass_DrawIndexedIndirect(s_device, s_render, s_a, 0) == mrhi_errorState,
          "indices of its width but no vertex buffers");
    SetVertices();
    CHECK(mrhiPass_DrawIndexedIndirect(s_device, s_render, s_a, 0) == mrhi_success, "its width");
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 0, MRHI_WHOLE_SIZE) ==
                  mrhi_success &&
              mrhiPass_DrawIndexedIndirect(s_device, s_render, s_a, 0) == mrhi_errorState,
          "not the strip's width");
    CHECK(mrhiDestroyGraphicsPipeline(s_device, strip) == mrhi_success &&
              mrhiPass_DrawIndirect(s_device, s_render, s_a, 0) == mrhi_errorStale &&
              mrhiPass_DrawIndexedIndirect(s_device, s_render, s_a, 0) == mrhi_errorStale,
          "a destroyed pipeline");
    Drop();
    CloseDevice();
}

// Opens a frame whose render pass alone declares the vertex and index
// buffers and two indirect ones, records and counts, compiled and begun.
static void CountedFrame(mrhiBufferId records, mrhiBufferId counts, mrhiResourceId* recordsOut,
                         mrhiResourceId* countsOut)
{
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frameDef) == mrhi_success, "begun");
    s_v = Import(s_vertices);
    s_i = Import(s_instances);
    s_x = Import(s_indices);
    *recordsOut = Import(records);
    *countsOut = Import(counts);
    mrhiResourceId target = {0};
    CHECK(mrhiImportTexture(s_device, s_target, &target) == mrhi_success, "imported");
    mrhiAccess accesses[] = {
        Access(s_v, mrhi_accessVertex),          Access(s_i, mrhi_accessVertex),
        Access(s_x, mrhi_accessIndex),           Access(*recordsOut, mrhi_accessIndirect),
        Access(*countsOut, mrhi_accessIndirect),
    };
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){.resource = target, .load = mrhi_loadClear};
    def.colorTargetCount = 1;
    def.accesses = accesses;
    def.accessCount = 5;
    CHECK(mrhiAddPass(s_device, &def, &s_render) == mrhi_success &&
              mrhiDevice_CompileFrame(s_device) == mrhi_success &&
              mrhiPass_Begin(s_device, s_render) == mrhi_success,
          "the render pass begun");
}

// Counted multi-draws: unsupported without their feature; with it, recorded
// with both buffers, the offsets and the most draws, and refused for records
// or a count outside their buffers, offsets off 4, a most of 0 or past
// MRHI_MAX_INDIRECT_DRAWS, and buffers the pass does not declare as indirect.
static void TestCountedDraws(void)
{
    Open();
    mrhiGraphicsPipelineId pipeline = MakeGraphics();
    Frame();
    CHECK(mrhiPass_SetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_success, "set");
    SetVertices();
    CHECK(mrhiPass_DrawIndirectCount(s_device, s_render, s_a, 0, s_a, 60, 1) ==
                  mrhi_errorUnsupported &&
              mrhiPass_DrawIndexedIndirectCount(s_device, s_render, s_a, 0, s_a, 60, 1) ==
                  mrhi_errorUnsupported &&
              mrhiDevice_GetMisuse(s_device) == 0,
          "unsupported without the feature, and no misuse");
    Drop();
    CloseDevice();
    s_adapter.features.multiDrawIndirectCount = true;
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.features.multiDrawIndirectCount = true;
    OpenLimited(deviceDef);
    pipeline = MakeGraphics();
    Frame();
    CHECK(mrhiPass_DrawIndirectCount(s_device, s_render, s_a, 0, s_a, 60, 1) == mrhi_errorState,
          "no pipeline set");
    CHECK(mrhiPass_SetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_success, "set");
    SetVertices();
    CHECK(mrhiPass_DrawIndexedIndirectCount(s_device, s_render, s_a, 0, s_a, 60, 1) ==
              mrhi_errorState,
          "no indices");
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 0, MRHI_WHOLE_SIZE) ==
              mrhi_success,
          "indices");
    CHECK(mrhiPass_DrawIndirectCount(s_device, s_render, s_a, 0, s_a, 60, 3) == mrhi_success,
          "three records and the count after them");
    const mrhiCommand* command = Nth(s_render, 4);
    CHECK(command != nullptr && command->type == mrhiCommandDrawIndirectCount &&
              command->a == s_a.index1 && command->b == ((uint64_t)3 << 32 | s_a.index1) &&
              command->c == 0 && command->d == 60,
          "recorded");
    CHECK(mrhiPass_DrawIndexedIndirectCount(s_device, s_render, s_a, 4, s_a, 0, 3) == mrhi_success,
          "three indexed records to the end");
    command = Nth(s_render, 5);
    CHECK(command != nullptr && command->type == mrhiCommandDrawIndexedIndirectCount &&
              command->a == s_a.index1 && command->b == ((uint64_t)3 << 32 | s_a.index1) &&
              command->c == 4 && command->d == 0,
          "recorded");
    CHECK(mrhiPass_DrawIndirectCount(s_device, s_render, s_a, 0, s_a, 60, 5) == mrhi_errorInvalid &&
              mrhiPass_DrawIndexedIndirectCount(s_device, s_render, s_a, 8, s_a, 0, 3) ==
                  mrhi_errorInvalid,
          "records past the buffer");
    CHECK(mrhiPass_DrawIndirectCount(s_device, s_render, s_a, 0, s_a, 64, 1) == mrhi_errorInvalid &&
              mrhiPass_DrawIndirectCount(s_device, s_render, s_a, 0, s_a, UINT64_MAX - 3, 1) ==
                  mrhi_errorInvalid,
          "a count past the buffer");
    CHECK(mrhiPass_DrawIndirectCount(s_device, s_render, s_a, 2, s_a, 60, 1) == mrhi_errorInvalid &&
              mrhiPass_DrawIndirectCount(s_device, s_render, s_a, 0, s_a, 58, 1) ==
                  mrhi_errorInvalid,
          "offsets off 4");
    CHECK(mrhiPass_DrawIndirectCount(s_device, s_render, s_a, 0, s_a, 60, 0) == mrhi_errorInvalid &&
              mrhiPass_DrawIndirectCount(s_device, s_render, s_a, 0, s_a, 60,
                                         MRHI_MAX_INDIRECT_DRAWS + 1) == mrhi_errorLimit,
          "no draws, or too many");
    CHECK(mrhiPass_DrawIndirectCount(s_device, s_render, s_v, 0, s_a, 60, 1) == mrhi_errorInvalid &&
              mrhiPass_DrawIndexedIndirectCount(s_device, s_render, s_a, 0, s_u, 0, 1) ==
                  mrhi_errorInvalid,
          "a buffer declared for another access");
    CHECK(mrhiPass_DrawIndirectCount(s_device, s_compute, s_a, 0, s_a, 60, 1) == mrhi_errorInvalid,
          "a pass without targets");
    CHECK(mrhiDevice_GetMisuse(s_device) == 11, "each counted");
    mrhiResourceId none = {0};
    CHECK(mrhiPass_DrawIndirectCount(s_device, s_render, none, 0, s_a, 60, 1) == mrhi_errorStale &&
              mrhiPass_DrawIndexedIndirectCount(s_device, s_render, s_a, 0, none, 0, 1) ==
                  mrhi_errorStale,
          "none");
    CHECK(mrhiPass_DrawIndirectCount(nullptr, s_render, s_a, 0, s_a, 60, 1) == mrhi_errorInvalid &&
              mrhiPass_DrawIndexedIndirectCount(nullptr, s_render, s_a, 0, s_a, 60, 1) ==
                  mrhi_errorInvalid,
          "no device");
    CHECK(mrhiDevice_GetMisuse(s_device) == 11, "neither counted");
    CHECK(mrhiPass_End(s_device, s_render) == mrhi_success &&
              mrhiPass_DrawIndirectCount(s_device, s_render, s_a, 0, s_a, 60, 1) == mrhi_errorState,
          "not recording");
    Drop();
    // The most draws, in a buffer with room for one more, and the count in
    // a buffer of its own.
    mrhiBufferId records =
        MakeBuffer((uint64_t)(MRHI_MAX_INDIRECT_DRAWS + 1) * 20 + 20, mrhi_bufferIndirect);
    mrhiBufferId counts = MakeBuffer(8, mrhi_bufferIndirect);
    mrhiResourceId r = {0};
    mrhiResourceId c = {0};
    CountedFrame(records, counts, &r, &c);
    CHECK(mrhiPass_SetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_success, "set");
    SetVertices();
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 0, MRHI_WHOLE_SIZE) ==
                  mrhi_success &&
              mrhiPass_DrawIndexedIndirectCount(s_device, s_render, r, 20, c, 4,
                                                MRHI_MAX_INDIRECT_DRAWS) == mrhi_success,
          "the most draws");
    command = Nth(s_render, 4);
    CHECK(command != nullptr && command->a == r.index1 &&
              command->b == ((uint64_t)MRHI_MAX_INDIRECT_DRAWS << 32 | c.index1) &&
              command->c == 20 && command->d == 4,
          "recorded with the count's own buffer");
    CHECK(mrhiPass_DrawIndexedIndirectCount(s_device, s_render, r, 0, c, 4,
                                            MRHI_MAX_INDIRECT_DRAWS + 1) == mrhi_errorLimit,
          "one past the most, though the buffer has room");
    Drop();
    CloseDevice();
    // A frame's counted draws in all stay within frameIndirectDraws, and
    // the next frame starts again.
    s_adapter.features.multiDrawIndirectCount = true;
    deviceDef.budgets.frameIndirectDraws = 5;
    OpenLimited(deviceDef);
    pipeline = MakeGraphics();
    for (int frame = 0; frame < 2; ++frame)
    {
        Frame();
        CHECK(mrhiPass_SetGraphicsPipeline(s_device, s_render, pipeline) == mrhi_success, "set");
        SetVertices();
        CHECK(mrhiPass_DrawIndirectCount(s_device, s_render, s_a, 0, s_a, 60, 3) == mrhi_success &&
                  mrhiPass_DrawIndirectCount(s_device, s_render, s_a, 0, s_a, 60, 3) ==
                      mrhi_errorCapacity &&
                  mrhiPass_DrawIndirectCount(s_device, s_render, s_a, 0, s_a, 60, 2) ==
                      mrhi_success &&
                  mrhiPass_DrawIndirectCount(s_device, s_render, s_a, 0, s_a, 60, 1) ==
                      mrhi_errorCapacity &&
                  Refusal() == mrhi_diagnosticFrameIndirectDraws,
              "five draws in a frame");
        mrhiFrameUsage usage = {0};
        CHECK(mrhiDevice_GetFrameUsage(s_device, &usage) == mrhi_success &&
                  usage.frameIndirectDraws == 5,
              "all of them taken");
        Drop();
    }
    CHECK(mrhiDevice_GetMisuse(s_device) == 0, "the limit is no misuse");
    CloseDevice();
}

static void TestIndirectDispatch(void)
{
    Open();
    Unbound();
    mrhiComputePipelineId pipeline = MakeCompute();
    Binding(0, 3, 0, mrhi_bindingUniformBuffer, mrhi_stageCompute);
    mrhiComputePipelineId bound = MakeCompute();
    Frame();
    CHECK(mrhiPass_DispatchIndirect(s_device, s_compute, s_a, 0) == mrhi_errorState, "no pipeline");
    CHECK(mrhiPass_SetComputePipeline(s_device, s_compute, pipeline) == mrhi_success, "set");
    CHECK(mrhiPass_DispatchIndirect(s_device, s_compute, s_a, 52) == mrhi_success,
          "the last counts");
    const mrhiCommand* command = Nth(s_compute, 1);
    CHECK(command != nullptr && command->type == mrhiCommandDispatchIndirect &&
              command->a == s_a.index1 && command->c == 52,
          "recorded");
    CHECK(mrhiPass_DispatchIndirect(s_device, s_compute, s_a, 56) == mrhi_errorInvalid &&
              mrhiPass_DispatchIndirect(s_device, s_compute, s_a, 66) == mrhi_errorInvalid &&
              mrhiPass_DispatchIndirect(s_device, s_compute, s_a, 1) == mrhi_errorInvalid,
          "past the buffer or off 4");
    CHECK(mrhiPass_DispatchIndirect(s_device, s_compute, s_x, 0) == mrhi_errorInvalid,
          "a buffer declared for indices");
    CHECK(mrhiPass_DispatchIndirect(s_device, s_render, s_a, 0) == mrhi_errorInvalid &&
              mrhiPass_DispatchIndirect(s_device, s_copy, s_a, 0) == mrhi_errorInvalid,
          "a pass with targets or copying");
    CHECK(mrhiDevice_GetMisuse(s_device) == 6, "each counted");
    mrhiResourceId none = {0};
    CHECK(mrhiPass_DispatchIndirect(s_device, s_compute, none, 0) == mrhi_errorStale, "none");
    CHECK(mrhiPass_SetComputePipeline(s_device, s_compute, bound) == mrhi_success &&
              mrhiPass_DispatchIndirect(s_device, s_compute, s_a, 0) == mrhi_errorState,
          "its table not set");
    CHECK(mrhiDestroyComputePipeline(s_device, bound) == mrhi_success &&
              mrhiPass_DispatchIndirect(s_device, s_compute, s_a, 0) == mrhi_errorStale,
          "destroyed");
    CHECK(mrhiPass_DispatchIndirect(nullptr, s_compute, s_a, 0) == mrhi_errorInvalid, "no device");
    CHECK(mrhiPass_End(s_device, s_compute) == mrhi_success &&
              mrhiPass_DispatchIndirect(s_device, s_compute, s_a, 0) == mrhi_errorState,
          "not recording");
    Drop();
    CloseDevice();
}

// A frame of every draw and dispatch, submitted: the test driver walks
// each record as a driver would translate it.
static void TestSubmitted(void)
{
    mrhiTestFrameLog log = {0};
    s_adapter.frameLog = &log;
    Open();
    Unbound();
    mrhiGraphicsPipelineId graphics = MakeGraphics();
    mrhiComputePipelineId compute = MakeCompute();
    Frame();
    CHECK(mrhiPass_SetGraphicsPipeline(s_device, s_render, graphics) == mrhi_success, "set");
    SetVertices();
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 0, MRHI_WHOLE_SIZE) ==
              mrhi_success,
          "indices");
    CHECK(mrhiPass_Draw(s_device, s_render, 3, 1, 0, 0) == mrhi_success &&
              mrhiPass_DrawIndexed(s_device, s_render, 3, 1, 0, 0, 0) == mrhi_success &&
              mrhiPass_DrawIndirect(s_device, s_render, s_a, 0) == mrhi_success &&
              mrhiPass_DrawIndexedIndirect(s_device, s_render, s_a, 16) == mrhi_success,
          "four draws");
    CHECK(mrhiPass_SetComputePipeline(s_device, s_compute, compute) == mrhi_success &&
              mrhiPass_Dispatch(s_device, s_compute, 1, 1, 1) == mrhi_success &&
              mrhiPass_DispatchIndirect(s_device, s_compute, s_a, 32) == mrhi_success,
          "two dispatches");
    CHECK(mrhiPass_End(s_device, s_render) == mrhi_success &&
              mrhiPass_End(s_device, s_compute) == mrhi_success &&
              mrhiPass_End(s_device, s_copy) == mrhi_success,
          "ended");
    mrhiRequestId token;
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_success, "submitted");
    // And the library's first pass, which clears the five new buffers.
    CHECK(log.frames == 1 && log.passes == 4 && log.commands == 16 && log.records == 16,
          "every command walked");
    CloseDevice();
    s_adapter.frameLog = nullptr;
}

static void TestArena(void)
{
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.budgets.frameCommandBytes = MRHI_CHUNK_BYTES;
    OpenLimited(deviceDef);
    Unbound();
    mrhiGraphicsPipelineId graphics = MakeGraphics();
    mrhiComputePipelineId compute = MakeCompute();
    Frame();
    CHECK(mrhiPass_SetGraphicsPipeline(s_device, s_render, graphics) == mrhi_success, "set");
    SetVertices();
    CHECK(mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 0, MRHI_WHOLE_SIZE) ==
              mrhi_success,
          "indices");
    // Four records so far; 123 draws fill the chunk.
    for (uint32_t i = 0; i < 123; ++i)
    {
        CHECK(mrhiPass_Draw(s_device, s_render, 1, 1, 0, 0) == mrhi_success, "fits");
    }
    CHECK(mrhiPass_Draw(s_device, s_render, 1, 1, 0, 0) == mrhi_errorCapacity &&
              mrhiPass_DrawIndexed(s_device, s_render, 1, 1, 0, 0, 0) == mrhi_errorCapacity &&
              mrhiPass_SetVertexBuffer(s_device, s_render, 0, s_v, 0, 12) == mrhi_errorCapacity &&
              mrhiPass_SetIndexBuffer(s_device, s_render, s_x, mrhi_indexUint16, 0, 4) ==
                  mrhi_errorCapacity,
          "full");
    CHECK(mrhiPass_DrawIndirect(s_device, s_render, s_a, 0) == mrhi_errorCapacity &&
              mrhiPass_DrawIndexedIndirect(s_device, s_render, s_a, 0) == mrhi_errorCapacity,
          "for indirect draws");
    CHECK(mrhiPass_SetComputePipeline(s_device, s_compute, compute) == mrhi_errorCapacity &&
              mrhiPass_Dispatch(s_device, s_compute, 1, 1, 1) == mrhi_errorCapacity &&
              mrhiPass_DispatchIndirect(s_device, s_compute, s_a, 0) == mrhi_errorCapacity,
          "for every pass");
    CHECK(Refusal() == mrhi_diagnosticFrameCommandBytes, "the limit named");
    Drop();
    CloseDevice();
}

int main(void)
{
    ResetAdapter();
    TestVertexBuffers();
    ResetAdapter();
    TestAttributeReach();
    TestIndices();
    TestTables();
    TestDispatch();
    ResetAdapter();
    TestIndirectDraws();
    TestCountedDraws();
    TestIndirectDispatch();
    TestSubmitted();
    TestArena();
    return s_failures == 0 ? 0 : 1;
}
