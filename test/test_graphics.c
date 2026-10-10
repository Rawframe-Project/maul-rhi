// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Graphics pipelines on a test driver device: from one def that is
// valid, each rule of WebGPU's render pipeline validation broken once,
// invalid when it contradicts itself or the shader and unsupported when
// the device cannot do it.

#include "test_container.h"
#include "test_device_setup.h"

#include "maul-rhi/pipeline.h"

#include <math.h>
#include <stdlib.h>

// The default container's vertex entry "vs" reads a 3-component float
// at location 3 and writes a 2-component float at location 0; its
// fragment entry "fs" reads that, reads front_facing and writes a
// 4-component float at location 0.
static mrhiShaderId s_shader;
static mrhiVertexBufferLayout s_buffers[33];
static mrhiVertexAttribute s_attributes[33];

// A shader of the sections on the device.
static mrhiShaderId Shader(mrhiDevice* device)
{
    Assemble();
    mrhiShaderDef def = mrhiDefaultShaderDef();
    def.bytes = s_container;
    def.byteCount = s_size;
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_success, "the shader");
    return shader;
}

// A ready device holding the default container, or the sections as a
// test changed them after Reset.
static mrhiDevice* OpenWithShader(mrhiDeviceDef def)
{
    def.deviceLimits.diagnostics = 8;
    mrhiDevice* device = OpenWith(def, true);
    s_shader = Shader(device);
    return device;
}

static mrhiDevice* Open(void)
{
    Reset();
    return OpenWithShader(mrhiDefaultDeviceDef());
}

// A valid def: one buffer of 12-byte vertices feeding location 3, and
// one rgba8 target.
static mrhiGraphicsPipelineDef Def(void)
{
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = s_shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    s_buffers[0] = (mrhiVertexBufferLayout){.stride = 12, .stepMode = mrhi_stepVertex};
    s_attributes[0] = (mrhiVertexAttribute){.location = 3, .format = mrhi_vertexFloat32x3};
    def.vertexBuffers = s_buffers;
    def.vertexBufferCount = 1;
    def.vertexAttributes = s_attributes;
    def.vertexAttributeCount = 1;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    def.colorTargetCount = 1;
    return def;
}

// Creates a pipeline from the def, destroying it again: the result.
static mrhiResult Made(mrhiDevice* device, const mrhiGraphicsPipelineDef* def)
{
    mrhiGraphicsPipelineId pipeline;
    mrhiRequestId request;
    mrhiResult status = mrhiCreateGraphicsPipeline(device, def, &pipeline, &request);
    if (status == mrhi_success)
    {
        CHECK(mrhiDestroyGraphicsPipeline(device, pipeline) == mrhi_success, "destroyed");
        mrhiDeviceNotification record;
        CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_success &&
                  record.outcome == mrhi_errorStale,
              "answered stale");
    }
    return status;
}

// The check that refused the device's latest refusal, draining its
// diagnostics (mrhi-0027); 0 when there is none.
static mrhiDiagnosticCode Refusal(mrhiDevice* device)
{
    mrhiDiagnosticCode code = 0;
    mrhiDiagnostic record;
    while (mrhiNextDeviceDiagnostic(device, &record) == mrhi_success)
    {
        code = record.code;
    }
    return code;
}

// Each part of a def refused under its own code.
static void TestCodes(void)
{
    mrhiDevice* device = Open();
    mrhiGraphicsPipelineDef def = Def();
    def.vertexEntry = "nope";
    def.vertexEntryLength = 4;
    CHECK(Made(device, &def) == mrhi_errorInvalid &&
              Refusal(device) == mrhi_diagnosticPipelineEntry,
          "an entry the shader lacks");
    def = Def();
    def.constantCount = 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid &&
              Refusal(device) == mrhi_diagnosticPipelineConstants,
          "constants without values");
    def = Def();
    s_buffers[0].stride = 13;
    CHECK(Made(device, &def) == mrhi_errorInvalid &&
              Refusal(device) == mrhi_diagnosticGraphicsVertex,
          "a stride not a multiple of 4");
    def = Def();
    def.topology = 99;
    CHECK(Made(device, &def) == mrhi_errorInvalid &&
              Refusal(device) == mrhi_diagnosticGraphicsPrimitive,
          "an unknown topology");
    def = Def();
    def.depthBiasSlopeScale = INFINITY;
    CHECK(Made(device, &def) == mrhi_errorInvalid &&
              Refusal(device) == mrhi_diagnosticGraphicsDepthStencil,
          "an infinite bias");
    def = Def();
    def.colorTargetCount = MRHI_COLOR_TARGETS + 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid &&
              Refusal(device) == mrhi_diagnosticGraphicsTargets,
          "too many targets");
    def = Def();
    def.sampleCount = 3;
    CHECK(Made(device, &def) == mrhi_errorInvalid &&
              Refusal(device) == mrhi_diagnosticGraphicsMultisample,
          "three samples");
    mrhiComputePipelineDef compute = mrhiDefaultComputePipelineDef();
    compute.shader = s_shader;
    compute.entry = "vs";
    compute.entryLength = 2;
    mrhiComputePipelineId computeId;
    mrhiRequestId request;
    CHECK(mrhiCreateComputePipeline(device, &compute, &computeId, &request) == mrhi_errorInvalid &&
              Refusal(device) == mrhi_diagnosticPipelineEntry,
          "a vertex entry for compute");
    compute.entry = "cs";
    compute.constantCount = 1;
    CHECK(mrhiCreateComputePipeline(device, &compute, &computeId, &request) == mrhi_errorInvalid &&
              Refusal(device) == mrhi_diagnosticPipelineConstants,
          "compute constants without values");
    Close(device);
}

static void TestCreate(void)
{
    mrhiDevice* device = Open();
    mrhiGraphicsPipelineDef def = Def();
    def.label = "lit";
    def.labelLength = 3;
    mrhiGraphicsPipelineId pipeline = {0};
    mrhiRequestId request = {0};
    CHECK(mrhiCreateGraphicsPipeline(device, &def, &pipeline, &request) == mrhi_success,
          "a graphics pipeline");
    mrhiDeviceNotification record;
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_success &&
              record.kind == mrhi_devicePipelineReady && record.request.index1 == request.index1 &&
              record.outcome == mrhi_success,
          "answered ready");
    mrhiComputePipelineId other = {pipeline.index1, pipeline.generation};
    CHECK(mrhiDestroyComputePipeline(device, other) == mrhi_errorStale, "not a compute pipeline");
    mrhiComputePipelineDef computeDef = mrhiDefaultComputePipelineDef();
    computeDef.shader = s_shader;
    computeDef.entry = "cs";
    computeDef.entryLength = 2;
    mrhiComputePipelineId compute;
    CHECK(mrhiCreateComputePipeline(device, &computeDef, &compute, &request) == mrhi_success,
          "a compute pipeline beside it");
    mrhiGraphicsPipelineId wrong = {compute.index1, compute.generation};
    CHECK(mrhiDestroyGraphicsPipeline(device, wrong) == mrhi_errorStale, "not a graphics pipeline");
    CHECK(mrhiDestroyGraphicsPipeline(device, pipeline) == mrhi_success, "destroyed");
    CHECK(mrhiDestroyGraphicsPipeline(device, pipeline) == mrhi_errorStale, "its id ended");
    CHECK(mrhiDestroyGraphicsPipeline(nullptr, pipeline) == mrhi_errorInvalid, "no device");
    CHECK(mrhiCreateGraphicsPipeline(device, nullptr, &pipeline, &request) == mrhi_errorInvalid &&
              mrhiCreateGraphicsPipeline(device, &def, nullptr, &request) == mrhi_errorInvalid &&
              mrhiCreateGraphicsPipeline(device, &def, &pipeline, nullptr) == mrhi_errorInvalid &&
              mrhiCreateGraphicsPipeline(nullptr, &def, &pipeline, &request) == mrhi_errorInvalid,
          "NULL arguments");
    def.cookie = 0;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "no cookie");
    CHECK(mrhiGetDeviceMisuse(device) == 4, "each counted");
    def = Def();
    def.shader.generation += 2;
    CHECK(Made(device, &def) == mrhi_errorStale, "a stale shader");
    Close(device);
}

static void TestEntries(void)
{
    mrhiDevice* device = Open();
    mrhiGraphicsPipelineDef def = Def();
    def.vertexEntry = "fs";
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a fragment entry as the vertex one");
    def = Def();
    def.fragmentEntry = "vs";
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a vertex entry as the fragment one");
    def = Def();
    def.vertexEntry = nullptr;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "no vertex entry");
    def = Def();
    def.fragmentEntry = nullptr;
    def.colorTargetCount = 0;
    def.depthStencilFormat = mrhi_formatDepth32Float;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "no fragment entry with a length");
    def.colorTargetCount = 1;
    def.depthStencilFormat = mrhi_formatNone;
    def.fragmentEntryLength = 0;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "color targets without a fragment entry");
    def.colorTargetCount = 0;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "no attachment at all");
    def.depthStencilFormat = mrhi_formatDepth32Float;
    CHECK(Made(device, &def) == mrhi_success, "depth only");
    def = Def();
    def.colorTargetCount = 0;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a fragment entry with no attachment");
    def.colorTargets[0].format = mrhi_formatNone;
    def.colorTargets[0].writeMask = 0;
    def.colorTargetCount = 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a target of no format is no attachment");
    Close(device);
}

static void TestVertexBuffers(void)
{
    mrhiDevice* device = Open();
    mrhiLimits limits = mrhiDefaultLimits();
    mrhiGraphicsPipelineDef def = Def();
    def.vertexBuffers = nullptr;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "no buffers with a count");
    def = Def();
    s_buffers[0].stride = 14;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a stride not a multiple of 4");
    def = Def();
    s_buffers[0].stride = limits.vertexStride + 4;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "a stride past the limit");
    def = Def();
    s_buffers[0].stride = limits.vertexStride;
    CHECK(Made(device, &def) == mrhi_success, "a stride at the limit");
    def = Def();
    s_buffers[0].stride = limits.vertexStride + 4;
    s_attributes[0].buffer = 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "invalid input wins over unsupported");
    def = Def();
    s_buffers[0].stepMode = mrhi_stepInstance + 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an unknown step mode");
    def = Def();
    s_buffers[0].stepMode = mrhi_stepInstance;
    CHECK(Made(device, &def) == mrhi_success, "per instance");
    def = Def();
    for (uint32_t i = 1; i <= limits.vertexBuffers; ++i)
    {
        s_buffers[i] = (mrhiVertexBufferLayout){.stride = 4};
    }
    def.vertexBufferCount = limits.vertexBuffers;
    CHECK(Made(device, &def) == mrhi_success, "as many buffers as the limit");
    def.vertexBufferCount = limits.vertexBuffers + 1;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "a buffer past the limit");
    Close(device);
    // More buffers than the floor, so that tables and buffers pass their
    // limit together: two tables and 23 buffers exceed 24.
    ResetAdapter();
    s_adapter.limits.vertexBuffers = 32;
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.limits.vertexBuffers = 32;
    Reset();
    device = OpenWithShader(deviceDef);
    def = Def();
    for (uint32_t i = 1; i < 23; ++i)
    {
        s_buffers[i] = (mrhiVertexBufferLayout){.stride = 4};
    }
    def.vertexBufferCount = 22;
    CHECK(Made(device, &def) == mrhi_success, "tables and buffers at their limit");
    def.vertexBufferCount = 23;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "tables and buffers past their limit");
    Close(device);
}

static void TestAttributes(void)
{
    mrhiDevice* device = Open();
    mrhiLimits limits = mrhiDefaultLimits();
    mrhiGraphicsPipelineDef def = Def();
    def.vertexAttributes = nullptr;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "no attributes with a count");
    def = Def();
    s_attributes[0].buffer = 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an attribute past the buffers");
    def = Def();
    s_attributes[0].format = mrhi_vertexNone;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "no format");
    def = Def();
    s_attributes[0].format = mrhi_vertexUnorm8x4Bgra + 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an unknown format");
    def = Def();
    s_attributes[0].offset = 2;
    s_buffers[0].stride = 16;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a misaligned offset");
    def = Def();
    s_attributes[0].offset = 4;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an attribute past its stride");
    def = Def();
    s_buffers[0].stride = 16;
    s_attributes[0].offset = 4;
    CHECK(Made(device, &def) == mrhi_success, "an attribute ending at its stride");
    s_attributes[1] =
        (mrhiVertexAttribute){.location = 5, .format = mrhi_vertexUint8, .offset = 16};
    def.vertexAttributeCount = 2;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a byte past its stride");
    def = Def();
    s_buffers[0].stride = 0;
    s_attributes[0].offset = limits.vertexStride - 12;
    CHECK(Made(device, &def) == mrhi_success, "a shared element up to the stride limit");
    s_attributes[0].offset = limits.vertexStride - 8;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "a shared element past it");
    def = Def();
    s_attributes[1] = (mrhiVertexAttribute){.location = 3, .format = mrhi_vertexUint8};
    def.vertexAttributeCount = 2;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a location twice");
    s_attributes[1].location = 4;
    s_attributes[1].offset = 3;
    CHECK(Made(device, &def) == mrhi_success, "an attribute the shader does not read");
    s_attributes[1].location = limits.vertexAttributes;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "a location past the limit");
    def = Def();
    s_attributes[0].location = 4;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an input without an attribute");
    def = Def();
    s_attributes[0].format = mrhi_vertexUint32x3;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "unsigned integers for a float input");
    def = Def();
    s_attributes[0].format = mrhi_vertexUnorm8x4;
    CHECK(Made(device, &def) == mrhi_success, "normalized bytes for a float input");
    s_attributes[0].format = mrhi_vertexFloat16;
    CHECK(Made(device, &def) == mrhi_success, "a 16-bit float for a float input");
    def = Def();
    for (uint32_t i = 1; i <= limits.vertexAttributes; ++i)
    {
        s_attributes[i] = (mrhiVertexAttribute){.location = i + 3, .format = mrhi_vertexUint8};
    }
    def.vertexAttributeCount = limits.vertexAttributes + 1;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "attributes past the limit");
    // Arrays past their limit are refused unread: an array of 16 with a
    // count of 17 is never read past its end.
    mrhiVertexAttribute* exact = malloc(sizeof(mrhiVertexAttribute) * limits.vertexAttributes);
    for (uint32_t i = 0; i < limits.vertexAttributes; ++i)
    {
        exact[i] = s_attributes[i];
    }
    def.vertexAttributes = exact;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "an array past the limit, unread");
    free(exact);
    def.vertexAttributes = nullptr;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "no array, whatever its count");
    Close(device);
    // An unsigned input takes only unsigned formats.
    Reset();
    Record(INPUTS, 0, 8)[4] = mrhi_scalarUint32;
    device = OpenWithShader(mrhiDefaultDeviceDef());
    def = Def();
    s_attributes[0].format = mrhi_vertexUint16x4;
    s_buffers[0].stride = 8;
    CHECK(Made(device, &def) == mrhi_success, "unsigned shorts for an unsigned input");
    s_attributes[0].format = mrhi_vertexSint16x4;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "signed shorts for an unsigned input");
    s_attributes[0].format = mrhi_vertexUnorm16x4;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "normalized shorts for an unsigned input");
    Close(device);
}

static void TestPrimitive(void)
{
    mrhiDevice* device = Open();
    mrhiGraphicsPipelineDef def = Def();
    def.topology = mrhi_topologyTriangleStrip + 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an unknown topology");
    def = Def();
    def.stripIndexFormat = mrhi_indexUint16;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a strip index format for a list");
    def.topology = mrhi_topologyTriangleStrip;
    CHECK(Made(device, &def) == mrhi_success, "a strip index format for a triangle strip");
    def.topology = mrhi_topologyLineStrip;
    def.stripIndexFormat = mrhi_indexUint32;
    CHECK(Made(device, &def) == mrhi_success, "and for a line strip");
    def.stripIndexFormat = mrhi_indexUint32 + 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an unknown index format");
    def = Def();
    def.frontFace = mrhi_frontClockwise + 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an unknown winding");
    def = Def();
    def.cullMode = mrhi_cullBack + 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an unknown cull mode");
    def = Def();
    def.frontFace = mrhi_frontClockwise;
    def.cullMode = mrhi_cullBack;
    CHECK(Made(device, &def) == mrhi_success, "clockwise, culling back faces");
    def = Def();
    def.unclippedDepth = true;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "unclipped depth without its feature");
    Close(device);
    ResetAdapter();
    s_adapter.features.unclippedDepth = true;
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.features.unclippedDepth = true;
    Reset();
    device = OpenWithShader(deviceDef);
    def = Def();
    def.unclippedDepth = true;
    CHECK(Made(device, &def) == mrhi_success, "unclipped depth with it");
    def = Def();
    def.viewCount = 2;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "two views without multiview");
    def.viewCount = 1;
    CHECK(Made(device, &def) == mrhi_success, "one view");
    Close(device);
    // Views within the device's multiview limit (mrhi-0020).
    ResetAdapter();
    s_adapter.features.multiview = true;
    s_adapter.limits.multiviewViews = 4;
    deviceDef = mrhiDefaultDeviceDef();
    deviceDef.features.multiview = true;
    deviceDef.limits.multiviewViews = 2;
    Reset();
    device = OpenWithShader(deviceDef);
    def = Def();
    def.viewCount = 2;
    CHECK(Made(device, &def) == mrhi_success, "two views with multiview");
    Close(device);
    ResetAdapter();
    s_adapter.limits.multiviewViews = 2;
    deviceDef = mrhiDefaultDeviceDef();
    deviceDef.limits.multiviewViews = 2;
    Reset();
    device = OpenWithShader(deviceDef);
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "two views in the limit, without multiview");
    Close(device);
    ResetAdapter();
    s_adapter.features.multiview = true;
    s_adapter.limits.multiviewViews = 4;
    deviceDef = mrhiDefaultDeviceDef();
    deviceDef.features.multiview = true;
    deviceDef.limits.multiviewViews = 2;
    Reset();
    device = OpenWithShader(deviceDef);
    def.viewCount = 3;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "past the device's limit");
    CHECK(mrhiGetDeviceMisuse(device) == 0, "neither is misuse");
    Close(device);
}

static void TestDepthStencil(void)
{
    mrhiDevice* device = Open();
    mrhiGraphicsPipelineDef def = Def();
    def.depthStencilFormat = mrhi_formatRgba8Unorm;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a color format for depth");
    def = Def();
    def.depthStencilFormat = mrhi_formatDepth32Float;
    def.depthWrite = true;
    def.depthCompare = mrhi_compareLess;
    CHECK(Made(device, &def) == mrhi_success, "depth written and tested");
    def.depthCompare = mrhi_compareNone;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "no depth test");
    def = Def();
    def.depthWrite = true;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "depth written without a depth format");
    def = Def();
    def.depthCompare = mrhi_compareLess;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "depth tested without a depth format");
    def = Def();
    def.depthBias = 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a bias without a depth format");
    def = Def();
    def.depthBiasSlopeScale = 1.0f;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a slope without one");
    def = Def();
    def.depthBiasClamp = 1.0f;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a clamp without one");
    def = Def();
    def.depthStencilFormat = mrhi_formatDepth32Float;
    def.topology = mrhi_topologyPointList;
    def.depthBiasSlopeScale = 1.0f;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a slope for points");
    def.depthBiasSlopeScale = 0.0f;
    def.depthBiasClamp = 1.0f;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a clamp for points");
    def = Def();
    def.stencilWriteMask = 0xFF;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a stencil write mask without one");
    def = Def();
    def.depthStencilFormat = mrhi_formatDepth32Float;
    def.depthBias = 1;
    def.depthBiasSlopeScale = 1.5f;
    def.depthBiasClamp = 0.25f;
    CHECK(Made(device, &def) == mrhi_success, "a bias with one");
    def.topology = mrhi_topologyLineList;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a bias for lines");
    def.topology = mrhi_topologyTriangleList;
    def.depthBiasSlopeScale = NAN;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a slope that is not a number");
    def.depthBiasSlopeScale = 0.0f;
    def.depthBiasClamp = INFINITY;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an infinite clamp");
    def = Def();
    def.depthStencilFormat = mrhi_formatDepth32Float;
    def.stencilFront.passOp = mrhi_stencilReplace;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a stencil test without a stencil format");
    def.depthStencilFormat = mrhi_formatDepthStencil;
    CHECK(Made(device, &def) == mrhi_success, "a stencil test with one");
    def.stencilBack.compare = mrhi_compareNone;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a stencil test of no comparison");
    def.stencilBack.compare = mrhi_compareEqual;
    def.stencilBack.depthFailOp = mrhi_stencilDecrementWrap + 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an unknown stencil operation");
    def.stencilBack.depthFailOp = mrhi_stencilDecrementWrap;
    def.stencilBack.failOp = mrhi_stencilDecrementWrap + 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an unknown failing operation");
    def.stencilBack.failOp = mrhi_stencilDecrementWrap;
    def.stencilBack.passOp = mrhi_stencilDecrementWrap + 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an unknown passing operation");
    def.stencilBack.passOp = mrhi_stencilIncrementClamp;
    CHECK(Made(device, &def) == mrhi_success, "every operation known");
    mrhiStencilFace changed[] = {
        {mrhi_compareLess, mrhi_stencilKeep, mrhi_stencilKeep, mrhi_stencilKeep},
        {mrhi_compareAlways, mrhi_stencilZero, mrhi_stencilKeep, mrhi_stencilKeep},
        {mrhi_compareAlways, mrhi_stencilKeep, mrhi_stencilZero, mrhi_stencilKeep},
    };
    for (size_t i = 0; i < sizeof(changed) / sizeof(changed[0]); ++i)
    {
        def = Def();
        def.depthStencilFormat = mrhi_formatDepth32Float;
        def.stencilBack = changed[i];
        CHECK(Made(device, &def) == mrhi_errorInvalid, "a back face without a stencil format");
        def.stencilBack = def.stencilFront;
        def.stencilFront = changed[i];
        CHECK(Made(device, &def) == mrhi_errorInvalid, "a front face without one");
    }
    def = Def();
    def.depthStencilFormat = mrhi_formatDepth32Float;
    def.stencilReadMask = 0xFF;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a stencil mask without a stencil format");
    def.depthStencilFormat = mrhi_formatDepthStencil;
    def.stencilWriteMask = 0x0F;
    CHECK(Made(device, &def) == mrhi_success, "stencil masks with one");
    Close(device);
    Reset();
    Put32(Record(ENTRIES, 1, 48) + 36, mrhi_builtinFragDepth);
    device = OpenWithShader(mrhiDefaultDeviceDef());
    def = Def();
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a fragment depth without a depth format");
    def.depthStencilFormat = mrhi_formatDepthStencil;
    CHECK(Made(device, &def) == mrhi_success, "a fragment depth with one");
    Close(device);
}

static void TestTargets(void)
{
    mrhiDevice* device = Open();
    mrhiGraphicsPipelineDef def = Def();
    def.colorTargetCount = MRHI_COLOR_TARGETS + 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "more targets than there are");
    def = Def();
    def.colorTargets[0].format = mrhi_formatDepth32Float;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a depth format as color");
    def = Def();
    def.colorTargets[1].format = mrhi_formatDepth32Float;
    def.colorTargets[1].writeMask = 0;
    def.colorTargetCount = 2;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a depth format as an unwritten color");
    def = Def();
    def.fragmentEntry = nullptr;
    def.fragmentEntryLength = 0;
    def.colorTargets[0].writeMask = 0;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an unwritten target without a fragment entry");
    def = Def();
    def.colorTargets[0].format = mrhi_formatBc1RgbaUnorm;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a compressed format as color");
    def.colorTargets[0].format = mrhi_formatRg11b10Ufloat;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "a format the device cannot render");
    def = Def();
    def.colorTargets[0].writeMask = 0x10;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an unknown channel");
    def = Def();
    def.colorTargets[1].format = mrhi_formatRgba8Unorm;
    def.colorTargetCount = 2;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a written target without an output");
    def.colorTargets[1].writeMask = 0;
    CHECK(Made(device, &def) == mrhi_success, "an unwritten target without an output");
    def = Def();
    def.colorTargets[1].blend = true;
    def.colorTargetCount = 2;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a blend on no target");
    def = Def();
    def.colorTargets[0].format = mrhi_formatR32Uint;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "floats for an unsigned target");
    def.colorTargets[0].format = mrhi_formatR32Sint;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "floats for a signed target");
    def.colorTargets[0].format = mrhi_formatR32Float;
    CHECK(Made(device, &def) == mrhi_success, "four floats for one channel");
    Close(device);
    Reset();
    Record(OUTPUTS, 0, 8)[4] = mrhi_scalarUint32;
    device = OpenWithShader(mrhiDefaultDeviceDef());
    def = Def();
    def.colorTargets[0].format = mrhi_formatR32Uint;
    CHECK(Made(device, &def) == mrhi_success, "unsigned integers for an unsigned target");
    Close(device);
    Reset();
    Record(OUTPUTS, 0, 8)[5] = 3;
    device = OpenWithShader(mrhiDefaultDeviceDef());
    def = Def();
    CHECK(Made(device, &def) == mrhi_errorInvalid, "three components for four channels");
    def.colorTargets[0].format = mrhi_formatRg8Unorm;
    CHECK(Made(device, &def) == mrhi_success, "three components for two channels");
    def.colorTargets[0].blend = true;
    def.colorTargets[0].color.srcFactor = mrhi_blendSrcAlpha;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "blending by an alpha the output lacks");
    def.colorTargets[0].color.srcFactor = mrhi_blendOne;
    def.colorTargets[0].color.dstFactor = mrhi_blendOneMinusSrcAlpha;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "and by its complement");
    def.colorTargets[0].color.dstFactor = mrhi_blendSrcAlphaSaturated;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "and saturated");
    def.colorTargets[0].color.dstFactor = mrhi_blendOneMinusSrc;
    def.colorTargets[0].alpha.srcFactor = mrhi_blendSrcAlpha;
    CHECK(Made(device, &def) == mrhi_success, "blending without reading the source alpha");
    Close(device);
}

// Dual-source blending: a factor reading the second source needs the
// feature, a second source in the fragment entry and one color target.
static void TestDualSource(void)
{
    mrhiDevice* device = Open();
    mrhiGraphicsPipelineDef def = Def();
    def.colorTargets[0].blend = true;
    def.colorTargets[0].color =
        (mrhiBlendComponent){mrhi_blendSrc1, mrhi_blendOneMinusSrc1, mrhi_blendAdd};
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "a second source without the feature");
    def.colorTargets[0].color = (mrhiBlendComponent){mrhi_blendOne, mrhi_blendZero, mrhi_blendAdd};
    def.colorTargets[0].alpha =
        (mrhiBlendComponent){mrhi_blendZero, mrhi_blendOneMinusSrc1Alpha, mrhi_blendAdd};
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "its alpha without the feature");
    def.colorTargets[0].blend = false;
    CHECK(Made(device, &def) == mrhi_success, "the factors of a target that does not blend");
    Close(device);
    // A second source in the container, but not the feature.
    Reset();
    Variable(OUTPUTS, 1, 0, mrhi_scalarFloat32, 4, 1, 0);
    Put16(Record(ENTRIES, 1, 48) + 30, 2);
    device = OpenWithShader(mrhiDefaultDeviceDef());
    def = Def();
    def.colorTargets[0].blend = true;
    def.colorTargets[0].color = (mrhiBlendComponent){mrhi_blendSrc1, mrhi_blendZero, mrhi_blendAdd};
    CHECK(Made(device, &def) == mrhi_errorUnsupported,
          "a second source in the shader without the feature");
    Close(device);
    ResetAdapter();
    s_adapter.features.dualSourceBlending = true;
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.features.dualSourceBlending = true;
    Reset();
    device = OpenWithShader(deviceDef);
    def = Def();
    def.colorTargets[0].blend = true;
    def.colorTargets[0].color =
        (mrhiBlendComponent){mrhi_blendSrc1Alpha, mrhi_blendOneMinusSrc1, mrhi_blendAdd};
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "a fragment entry without a second source");
    Close(device);
    ResetAdapter();
    s_adapter.features.dualSourceBlending = true;
    Reset();
    Variable(OUTPUTS, 1, 0, mrhi_scalarFloat32, 4, 1, 0);
    Put16(Record(ENTRIES, 1, 48) + 30, 2);
    device = OpenWithShader(deviceDef);
    CHECK(Made(device, &def) == mrhi_success, "with the feature and a second source");
    mrhiGraphicsPipelineDef plain = Def();
    plain.colorTargets[0].blend = true;
    plain.colorTargets[0].color =
        (mrhiBlendComponent){mrhi_blendSrc1, mrhi_blendZero, mrhi_blendAdd};
    CHECK(Made(device, &plain) == mrhi_success, "the second source as the source factor");
    def.colorTargets[1].format = mrhi_formatRgba8Unorm;
    def.colorTargets[1].writeMask = 0;
    def.colorTargetCount = 2;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "beside a second color target");
    plain.colorTargetCount = 2;
    plain.colorTargets[1].format = mrhi_formatRgba8Unorm;
    plain.colorTargets[1].writeMask = 0;
    CHECK(Made(device, &plain) == mrhi_errorUnsupported,
          "the second source alone as a factor, beside a second target");
    Close(device);
    // Blending by the second source's alpha needs outputs with alpha.
    ResetAdapter();
    s_adapter.features.dualSourceBlending = true;
    Reset();
    Variable(OUTPUTS, 0, 0, mrhi_scalarFloat32, 3, 0, 0);
    Variable(OUTPUTS, 1, 0, mrhi_scalarFloat32, 3, 1, 0);
    Put16(Record(ENTRIES, 1, 48) + 30, 2);
    device = OpenWithShader(deviceDef);
    def = Def();
    def.colorTargets[0].format = mrhi_formatRg8Unorm;
    def.colorTargets[0].blend = true;
    def.colorTargets[0].color = (mrhiBlendComponent){mrhi_blendSrc1, mrhi_blendZero, mrhi_blendAdd};
    CHECK(Made(device, &def) == mrhi_success, "three components blended by the second source");
    def.colorTargets[0].color =
        (mrhiBlendComponent){mrhi_blendSrc1Alpha, mrhi_blendZero, mrhi_blendAdd};
    CHECK(Made(device, &def) == mrhi_errorInvalid, "by its alpha, which they lack");
    def.colorTargets[0].color =
        (mrhiBlendComponent){mrhi_blendOne, mrhi_blendOneMinusSrc1Alpha, mrhi_blendAdd};
    CHECK(Made(device, &def) == mrhi_errorInvalid, "by its alpha's complement");
    Close(device);
}

static void TestBlend(void)
{
    mrhiDevice* device = Open();
    mrhiGraphicsPipelineDef def = Def();
    def.colorTargets[0].blend = true;
    def.colorTargets[0].color =
        (mrhiBlendComponent){mrhi_blendSrcAlpha, mrhi_blendOneMinusSrcAlpha, mrhi_blendAdd};
    CHECK(Made(device, &def) == mrhi_success, "alpha blending");
    def.colorTargets[0].color.srcFactor = mrhi_blendOneMinusSrc1Alpha + 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an unknown source factor");
    def.colorTargets[0].color.srcFactor = mrhi_blendOne;
    def.colorTargets[0].alpha.dstFactor = mrhi_blendOneMinusSrc1Alpha + 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an unknown alpha factor");
    def.colorTargets[0].alpha =
        (mrhiBlendComponent){mrhi_blendOne, mrhi_blendOne, mrhi_blendMax + 1};
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an unknown operation");
    def.colorTargets[0].alpha.operation = mrhi_blendReverseSubtract;
    def.colorTargets[0].alpha = (mrhiBlendComponent){mrhi_blendOne, mrhi_blendOne, mrhi_blendMin};
    CHECK(Made(device, &def) == mrhi_success, "a minimum weighed by one");
    def.colorTargets[0].alpha.dstFactor = mrhi_blendZero;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a minimum weighed otherwise");
    def.colorTargets[0].alpha = (mrhiBlendComponent){mrhi_blendSrc, mrhi_blendOne, mrhi_blendMax};
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a maximum weighed otherwise");
    def.colorTargets[0].alpha = (mrhiBlendComponent){mrhi_blendOne, mrhi_blendOne, mrhi_blendMax};
    CHECK(Made(device, &def) == mrhi_success, "a maximum weighed by one");
    def = Def();
    def.colorTargets[0].format = mrhi_formatR32Float;
    def.colorTargets[0].blend = true;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "blending a format that cannot blend");
    Close(device);
}

// Sets the targets after the first to formats, unwritten.
static void Targets(mrhiGraphicsPipelineDef* def, const mrhiFormat* formats, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        def->colorTargets[i + 1].format = formats[i];
        def->colorTargets[i + 1].writeMask = 0;
    }
    def->colorTargetCount = count + 1;
}

static void TestColorBytes(void)
{
    mrhiDevice* device = Open();
    mrhiGraphicsPipelineDef def = Def();
    def.colorTargets[0].format = mrhi_formatRgba32Float;
    Targets(&def, (mrhiFormat[]){mrhi_formatRgba32Float}, 1);
    CHECK(Made(device, &def) == mrhi_success, "32 bytes");
    Targets(&def, (mrhiFormat[]){mrhi_formatRgba32Float, mrhi_formatR8Unorm}, 2);
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "33 bytes");
    // 1, then 16 aligned to 4, 8, 1, and 4 aligned to 4: 36 bytes, where
    // the plain sum is 30.
    def = Def();
    def.colorTargets[0].format = mrhi_formatR8Unorm;
    Targets(&def,
            (mrhiFormat[]){mrhi_formatRgba32Float, mrhi_formatRg32Float, mrhi_formatR8Unorm,
                           mrhi_formatR32Float},
            4);
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "bytes past the limit once aligned");
    Targets(&def,
            (mrhiFormat[]){mrhi_formatRgba32Float, mrhi_formatRg32Float, mrhi_formatR8Unorm,
                           mrhi_formatR8Unorm},
            4);
    CHECK(Made(device, &def) == mrhi_success, "and within it: 1, 16 at 4, 8, 1, 1");
    def = Def();
    Targets(&def,
            (mrhiFormat[]){mrhi_formatRgba8Unorm, mrhi_formatRgba8Unorm, mrhi_formatRgba8Unorm}, 3);
    CHECK(Made(device, &def) == mrhi_success, "four rgba8 targets cost 32");
    Targets(&def,
            (mrhiFormat[]){mrhi_formatRgba8Unorm, mrhi_formatRgba8Unorm, mrhi_formatRgba8Unorm,
                           mrhi_formatR8Unorm},
            4);
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "and a fifth past the limit");
    Close(device);
}

static void TestMultisample(void)
{
    mrhiDevice* device = Open();
    mrhiGraphicsPipelineDef def = Def();
    uint32_t counts[] = {0, 3, 32};
    for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i)
    {
        def.sampleCount = counts[i];
        CHECK(Made(device, &def) == mrhi_errorInvalid, "not a power of two to 16");
    }
    def.sampleCount = 4;
    def.depthStencilFormat = mrhi_formatDepth32Float;
    CHECK(Made(device, &def) == mrhi_success, "four samples");
    def.sampleCount = 2;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "two samples the formats do not take");
    def = Def();
    def.sampleCount = 4;
    def.colorTargets[0].format = mrhi_formatRgba32Float;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "a target that cannot multisample");
    def = Def();
    def.sampleCount = 4;
    def.colorTargets[1].format = mrhi_formatRgba32Float;
    CHECK(def.colorTargetCount == 1 && Made(device, &def) == mrhi_success,
          "a format past the target count unread");
    def = Def();
    def.fragmentEntry = nullptr;
    def.fragmentEntryLength = 0;
    def.colorTargetCount = 0;
    def.depthStencilFormat = mrhi_formatDepth32Float;
    def.sampleCount = 2;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "two samples depth does not take");
    def.sampleCount = 4;
    CHECK(Made(device, &def) == mrhi_success, "four it does");
    def = Def();
    def.alphaToCoverage = true;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "alpha to coverage with one sample");
    def.sampleCount = 4;
    CHECK(Made(device, &def) == mrhi_success, "alpha to coverage with four");
    def.colorTargets[0].format = mrhi_formatRg8Unorm;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "alpha to coverage without alpha");
    def.colorTargets[0].format = mrhi_formatNone;
    def.depthStencilFormat = mrhi_formatDepth32Float;
    def.colorTargets[0].writeMask = 0;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "alpha to coverage without a first target");
    Close(device);
    Reset();
    Put32(Record(ENTRIES, 1, 48) + 36, mrhi_builtinSampleMaskOut);
    device = OpenWithShader(mrhiDefaultDeviceDef());
    def = Def();
    def.sampleCount = 4;
    CHECK(Made(device, &def) == mrhi_success, "a written sample mask");
    def.alphaToCoverage = true;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "with alpha to coverage");
    Close(device);
    // rgba32float that multisamples but cannot blend.
    ResetAdapter();
    s_adapter.limitedFormat = mrhi_formatRgba32Float;
    s_adapter.limitedCaps =
        (mrhiFormatCaps){.sampling = true, .rendering = true, .storage = true, .sampleCounts = 5};
    Reset();
    device = OpenWithShader(mrhiDefaultDeviceDef());
    def = Def();
    def.sampleCount = 4;
    def.colorTargets[0].format = mrhi_formatRgba32Float;
    CHECK(Made(device, &def) == mrhi_success, "a multisampled float target");
    def.alphaToCoverage = true;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "alpha to coverage that cannot blend");
    Close(device);
}

// Opens a device whose fragment entry reads variable 1 as given.
static mrhiDevice* WithInput(mrhiScalarType type, uint8_t components,
                             mrhiInterpolation interpolation, mrhiSampling sampling)
{
    Reset();
    Variable(VARIABLES, 1, 0, type, components, interpolation, sampling);
    return OpenWithShader(mrhiDefaultDeviceDef());
}

static void TestInterface(void)
{
    struct
    {
        mrhiScalarType type;
        uint8_t components;
        mrhiInterpolation interpolation;
        mrhiSampling sampling;
        const char* what;
    } cases[] = {
        {mrhi_scalarUint32, 2, mrhi_interpolationFlat, mrhi_samplingFirst, "another type"},
        {mrhi_scalarFloat32, 3, mrhi_interpolationPerspective, mrhi_samplingCenter,
         "other components"},
        {mrhi_scalarFloat32, 2, mrhi_interpolationLinear, mrhi_samplingCenter,
         "another interpolation"},
        {mrhi_scalarFloat32, 2, mrhi_interpolationPerspective, mrhi_samplingCentroid,
         "another sampling"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        mrhiDevice* device = WithInput(cases[i].type, cases[i].components, cases[i].interpolation,
                                       cases[i].sampling);
        mrhiGraphicsPipelineDef def = Def();
        CHECK(Made(device, &def) == mrhi_errorInvalid &&
                  Refusal(device) == mrhi_diagnosticGraphicsInterface,
              cases[i].what);
        Close(device);
    }
    Reset();
    Variable(VARIABLES, 0, 0, mrhi_scalarSint32, 2, mrhi_interpolationFlat, mrhi_samplingFirst);
    Variable(VARIABLES, 1, 0, mrhi_scalarUint32, 2, mrhi_interpolationFlat, mrhi_samplingFirst);
    mrhiDevice* device = OpenWithShader(mrhiDefaultDeviceDef());
    mrhiGraphicsPipelineDef flat = Def();
    CHECK(Made(device, &flat) == mrhi_errorInvalid, "only the type differs");
    Close(device);
    Reset();
    Put32(Record(VARIABLES, 1, 8), 1);
    device = OpenWithShader(mrhiDefaultDeviceDef());
    mrhiGraphicsPipelineDef def = Def();
    CHECK(Made(device, &def) == mrhi_errorInvalid, "an input no output writes");
    Close(device);
    // A vertex entry writing as many variables as the limit leaves none
    // for a point's size.
    Reset();
    uint32_t limit = mrhiDefaultLimits().interStageVariables;
    for (uint32_t i = 0; i < limit; ++i)
    {
        Variable(VARIABLES, i + 1, i, mrhi_scalarFloat32, 4, mrhi_interpolationPerspective,
                 mrhi_samplingCenter);
    }
    Put16(Record(ENTRIES, 0, 48) + 32, 1);
    Put16(Record(ENTRIES, 0, 48) + 34, (uint16_t)limit);
    Put16(Record(ENTRIES, 1, 48) + 32, 0);
    Put16(Record(ENTRIES, 1, 48) + 34, 0);
    device = OpenWithShader(mrhiDefaultDeviceDef());
    def = Def();
    CHECK(Made(device, &def) == mrhi_success, "outputs the fragment entry does not read");
    def.topology = mrhi_topologyPointList;
    CHECK(Made(device, &def) == mrhi_errorUnsupported, "points with every variable used");
    Put16(Record(ENTRIES, 0, 48) + 34, (uint16_t)(limit - 1));
    Close(device);
    device = OpenWithShader(mrhiDefaultDeviceDef());
    def = Def();
    def.topology = mrhi_topologyPointList;
    CHECK(Made(device, &def) == mrhi_success, "points with one variable to spare");
    Close(device);
}

static void TestHalfFloats(void)
{
    ResetAdapter();
    s_adapter.features.shaderF16 = true;
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.features.shaderF16 = true;
    Reset();
    Record(OUTPUTS, 0, 8)[4] = mrhi_scalarFloat16;
    mrhiDevice* device = OpenWithShader(deviceDef);
    mrhiGraphicsPipelineDef def = Def();
    CHECK(Made(device, &def) == mrhi_success, "16-bit floats for a float target");
    def.colorTargets[0].format = mrhi_formatR32Uint;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "but not for an unsigned one");
    Close(device);
    ResetAdapter();
}

static void TestDriverFailure(void)
{
    s_adapter.objectsBeforeFailure = 1;
    mrhiDevice* device = Open();
    mrhiGraphicsPipelineDef def = Def();
    CHECK(Made(device, &def) == mrhi_errorPlatform, "the driver fails at once");
    CHECK(mrhiDestroyShader(device, s_shader) == mrhi_success, "the shader, its last hold");
    CHECK(mrhiNextDeviceNotification(device, &(mrhiDeviceNotification){0}) == mrhi_empty,
          "nothing to answer");
    Close(device);
    ResetAdapter();
}

static void TestConstantsAndMisuse(void)
{
    Reset();
    uint8_t* required = Record(CONSTANTS, 1, 16);
    Put32(required, 3);
    required[4] = mrhi_constantUint32;
    required[12] = 1;
    mrhiDevice* device = OpenWithShader(mrhiDefaultDeviceDef());
    mrhiGraphicsPipelineDef def = Def();
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a required constant unset");
    mrhiConstantValue value = {3, 7.0};
    def.constants = &value;
    def.constantCount = 1;
    CHECK(Made(device, &def) == mrhi_success, "set");
    value.value = -1.0;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "set out of range");
    def = Def();
    def.label = "\xC3";
    def.labelLength = 1;
    CHECK(Made(device, &def) == mrhi_errorInvalid, "a bad label");
    uint64_t misuse = mrhiGetDeviceMisuse(device);
    def = Def();
    value.value = 7.0;
    def.constants = &value;
    def.constantCount = 1;
    def.unclippedDepth = true;
    CHECK(Made(device, &def) == mrhi_errorUnsupported && mrhiGetDeviceMisuse(device) == misuse,
          "unsupported is not misuse");
    CHECK(misuse == 3, "invalid input is");
    Close(device);
}

int main(void)
{
    ResetAdapter();
    TestCodes();
    TestCreate();
    TestEntries();
    TestVertexBuffers();
    ResetAdapter();
    TestAttributes();
    TestPrimitive();
    ResetAdapter();
    TestDepthStencil();
    TestTargets();
    TestBlend();
    TestDualSource();
    TestColorBytes();
    TestMultisample();
    ResetAdapter();
    TestInterface();
    TestHalfFloats();
    TestDriverFailure();
    TestConstantsAndMisuse();
    return s_failures == 0 ? 0 : 1;
}
