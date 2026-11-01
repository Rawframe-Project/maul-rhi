// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The walk of a pipeline the core hands a driver: a valid pipeline
// breaks no rule, and each case starts from it and breaks one rule of
// its shader's reflection, which the walk counts.

#include "capabilities_core.h"
#include "pipeline_walk.h"
#include "test_harness.h"

#include <float.h>
#include <math.h>

// A shader with a vertex entry reading a float input at location 0 and
// writing a variable at location 1, and a fragment entry reading it and
// writing four floats at location 0 from both blend sources; one binding
// in table 2; a required unsigned constant 7 and a boolean constant 8
// with a default.
typedef struct Fixture
{
    mrhiShaderEntry entries[2];
    mrhiShaderBinding binding;
    mrhiShaderVariable input;
    mrhiShaderVariable outputs[2];
    mrhiShaderVariable variables[2];
    mrhiShaderConstant constants[2];
    mrhiReflection reflection;
    mrhiVertexBufferLayout buffer;
    mrhiVertexAttribute attribute;
    mrhiConstantValue values[2];
    mrhiGraphicsPipelineDef def;
    mrhiDriverGraphicsPipeline pipeline;
    mrhiLimits limits;
} Fixture;

static Fixture s_fixture;

// Resets the fixture to a valid pipeline and returns it.
static Fixture* Valid(void)
{
    Fixture* f = &s_fixture;
    *f = (Fixture){0};
    f->entries[0] =
        (mrhiShaderEntry){.stage = mrhi_stageVertex, .inputCount = 1, .variableCount = 1};
    f->entries[1] = (mrhiShaderEntry){
        .stage = mrhi_stageFragment, .outputCount = 2, .firstVariable = 1, .variableCount = 1};
    f->binding = (mrhiShaderBinding){.table = 2};
    f->input = (mrhiShaderVariable){.location = 0, .type = mrhi_scalarFloat32, .components = 3};
    f->outputs[0] = (mrhiShaderVariable){.type = mrhi_scalarFloat32, .components = 4};
    f->outputs[1] =
        (mrhiShaderVariable){.type = mrhi_scalarFloat32, .components = 4, .blendSource = 1};
    for (int i = 0; i < 2; ++i)
    {
        f->variables[i] = (mrhiShaderVariable){.location = 1,
                                               .type = mrhi_scalarFloat32,
                                               .components = 4,
                                               .interpolation = mrhi_interpolationPerspective,
                                               .sampling = mrhi_samplingCenter};
    }
    f->constants[0] = (mrhiShaderConstant){.id = 7, .type = mrhi_constantUint32, .required = true};
    f->constants[1] = (mrhiShaderConstant){.id = 8, .type = mrhi_constantBool};
    f->reflection = (mrhiReflection){
        .entryCount = 2,
        .bindingCount = 1,
        .inputCount = 1,
        .outputCount = 2,
        .variableCount = 2,
        .constantCount = 2,
        .entries = f->entries,
        .bindings = &f->binding,
        .inputs = &f->input,
        .outputs = f->outputs,
        .variables = f->variables,
        .constants = f->constants,
    };
    f->buffer = (mrhiVertexBufferLayout){.stride = 12};
    f->attribute = (mrhiVertexAttribute){.format = mrhi_vertexFloat32x3};
    f->values[0] = (mrhiConstantValue){.id = 7, .value = 3.0};
    f->def = mrhiDefaultGraphicsPipelineDef();
    f->def.vertexBuffers = &f->buffer;
    f->def.vertexBufferCount = 1;
    f->def.vertexAttributes = &f->attribute;
    f->def.vertexAttributeCount = 1;
    for (int i = 0; i < 2; ++i)
    {
        f->def.colorTargets[i] = (mrhiColorTargetState){.format = mrhi_formatRgba8Unorm,
                                                        .writeMask = mrhiColorWritesKnown};
    }
    f->def.colorTargetCount = 1;
    f->def.constants = f->values;
    f->def.constantCount = 1;
    f->pipeline = (mrhiDriverGraphicsPipeline){.shader = 1,
                                               .reflection = &f->reflection,
                                               .vertexEntry = 0,
                                               .fragmentEntry = 1,
                                               .def = &f->def};
    f->limits = mrhiDefaultLimits();
    return f;
}

static uint32_t Walk(const Fixture* f)
{
    return mrhiWalkGraphicsPipeline(&f->pipeline, &f->limits);
}

// The constant value 7 takes, or its count with a second value.
static uint32_t WalkValue(double value)
{
    Fixture* f = Valid();
    f->values[0].value = value;
    return Walk(f);
}

static void TestValid(void)
{
    CHECK(Walk(Valid()) == 0, "a valid pipeline breaks no rule");
    Fixture* f = Valid();
    f->values[1] = (mrhiConstantValue){.id = 8, .value = 1.0};
    f->def.constantCount = 2;
    CHECK(Walk(f) == 0, "a constant with a default given");
    f = Valid();
    f->outputs[0].type = mrhi_scalarFloat16;
    CHECK(Walk(f) == 0, "a 16-bit float output writes a float target");
    f = Valid();
    f->def.colorTargets[0].writeMask = 0;
    f->entries[1].outputCount = 0;
    CHECK(Walk(f) == 0, "a target written by no output with nothing to write");
    f = Valid();
    f->def.colorTargets[0].format = mrhi_formatNone;
    f->def.colorTargets[0].writeMask = mrhiColorWritesKnown;
    f->def.colorTargets[1] = f->def.colorTargets[0];
    f->def.colorTargets[1].format = mrhi_formatRgba8Unorm;
    f->def.colorTargets[1].writeMask = 0;
    f->def.colorTargetCount = 2;
    f->entries[1].outputCount = 0;
    CHECK(Walk(f) == 0, "no target at a location the fragment entry does not write");
    f->def.colorTargetCount = 1;
    f->def.depthStencilFormat = mrhi_formatDepth32Float;
    CHECK(Walk(f) == 1, "a last target without a format, which the core leaves out");
}

static void TestConstants(void)
{
    CHECK(WalkValue(4294967295.0) == 0 && WalkValue(0.0) == 0, "an unsigned value at its edges");
    CHECK(WalkValue(4294967296.0) != 0, "an unsigned value past its range");
    CHECK(WalkValue(-1.0) != 0, "a negative unsigned value");
    CHECK(WalkValue(0.5) != 0, "a fraction of an integer");
    CHECK(WalkValue((double)NAN) != 0, "NaN for an integer");
    Fixture* f = Valid();
    f->constants[0].type = mrhi_constantInt32;
    f->values[0].value = -2147483648.0;
    CHECK(Walk(f) == 0, "a signed value at its least");
    f->values[0].value = 2147483648.0;
    CHECK(Walk(f) != 0, "a signed value past its range");
    f = Valid();
    f->constants[0].type = mrhi_constantBool;
    f->values[0].value = 2.0;
    CHECK(Walk(f) != 0, "a boolean neither 0 nor 1");
    f = Valid();
    f->constants[0].type = mrhi_constantFloat32;
    f->values[0].value = (double)FLT_MAX;
    CHECK(Walk(f) == 0, "a float at the 32-bit greatest");
    f->values[0].value = 1e39;
    CHECK(Walk(f) != 0, "a float past 32 bits");
    f->values[0].value = (double)INFINITY;
    CHECK(Walk(f) != 0, "an infinite float");
    f = Valid();
    f->values[1] = f->values[0];
    f->def.constantCount = 2;
    CHECK(Walk(f) != 0, "a constant given twice");
    f = Valid();
    f->values[0].id = 9;
    CHECK(Walk(f) != 0, "a constant the shader lacks, and the required one missing");
    f = Valid();
    f->def.constantCount = 0;
    CHECK(Walk(f) != 0, "a required constant missing");
    f = Valid();
    f->values[1] = (mrhiConstantValue){.id = 9, .value = 0.0};
    f->def.constantCount = 2;
    CHECK(Walk(f) != 0, "a constant the shader lacks");
}

static void TestCompute(void)
{
    Fixture* f = Valid();
    f->entries[0].stage = mrhi_stageCompute;
    mrhiDriverComputePipeline pipeline = {
        .shader = 1, .reflection = &f->reflection, .constants = f->values, .constantCount = 1};
    CHECK(mrhiWalkComputePipeline(&pipeline) == 0, "a valid compute pipeline");
    pipeline.constantCount = 0;
    CHECK(mrhiWalkComputePipeline(&pipeline) != 0, "a compute pipeline missing a constant");
    f->values[0].value = -1.0;
    pipeline.constantCount = 1;
    CHECK(mrhiWalkComputePipeline(&pipeline) != 0, "a compute constant out of range");
}

static void TestVertex(void)
{
    Fixture* f = Valid();
    f->attribute.location = 1;
    CHECK(Walk(f) != 0, "a vertex input without an attribute");
    f = Valid();
    f->def.vertexAttributeCount = 0;
    CHECK(Walk(f) != 0, "no attributes for a vertex input");
    f = Valid();
    f->attribute.format = mrhi_vertexSint32;
    CHECK(Walk(f) != 0, "an attribute of another scalar class");
    f = Valid();
    f->attribute.format = mrhi_vertexUint32;
    f->input.type = mrhi_scalarUint32;
    CHECK(Walk(f) == 0, "an unsigned attribute for an unsigned input");
    f = Valid();
    f->limits.tablesPlusVertexBuffers = 4;
    CHECK(Walk(f) == 0, "three tables and a buffer at the limit");
    f->limits.tablesPlusVertexBuffers = 3;
    CHECK(Walk(f) != 0, "three tables and a buffer past the limit");
}

static void TestTargets(void)
{
    Fixture* f = Valid();
    f->def.colorTargetCount = 2;
    CHECK(Walk(f) != 0, "a written target without an output");
    f = Valid();
    f->outputs[0].components = 3;
    CHECK(Walk(f) != 0, "an output with fewer components than its target");
    f = Valid();
    f->def.colorTargets[0].format = mrhi_formatR32Uint;
    CHECK(Walk(f) != 0, "a float output for an unsigned target");
    f = Valid();
    f->def.colorTargets[0].format = mrhi_formatR8Unorm;
    f->outputs[0].components = 3;
    CHECK(Walk(f) == 0, "three components for one channel");
    f->def.colorTargets[0].blend = true;
    f->def.colorTargets[0].color =
        (mrhiBlendComponent){.srcFactor = mrhi_blendSrcAlpha, .dstFactor = mrhi_blendOne};
    f->def.colorTargets[0].alpha =
        (mrhiBlendComponent){.srcFactor = mrhi_blendOne, .dstFactor = mrhi_blendZero};
    CHECK(Walk(f) != 0, "a color blend reading alpha from three components");
    f->def.colorTargets[0].color =
        (mrhiBlendComponent){.srcFactor = mrhi_blendOne, .dstFactor = mrhi_blendOneMinusSrcAlpha};
    CHECK(Walk(f) != 0, "a color blend reading alpha as its destination factor");
    f->def.colorTargets[0].blend = false;
    CHECK(Walk(f) == 0, "an alpha factor unread without blending");
    f = Valid();
    f->def.colorTargetCount = 1;
    f->pipeline.fragmentEntry = 2;
    f->def.colorTargets[0].writeMask = 0;
    CHECK(Walk(f) != 0, "a color target without a fragment entry");
}

static void TestSecondSource(void)
{
    static const mrhiBlendFactor seconds[] = {mrhi_blendSrc1, mrhi_blendOneMinusSrc1,
                                              mrhi_blendSrc1Alpha, mrhi_blendOneMinusSrc1Alpha};
    for (int i = 0; i < 4; ++i)
    {
        Fixture* f = Valid();
        f->def.colorTargets[0].blend = true;
        f->def.colorTargets[0].color =
            (mrhiBlendComponent){.srcFactor = mrhi_blendOne, .dstFactor = mrhi_blendZero};
        f->def.colorTargets[0].alpha = f->def.colorTargets[0].color;
        f->def.colorTargets[0].alpha.dstFactor = seconds[i];
        CHECK(Walk(f) == 0, "a second source the fragment entry writes");
        f->entries[1].outputCount = 1;
        CHECK(Walk(f) != 0, "an alpha blend from a second source never written");
        f->def.colorTargets[0].alpha.dstFactor = mrhi_blendZero;
        f->def.colorTargets[0].color.srcFactor = seconds[i];
        CHECK(Walk(f) != 0, "a color blend from a second source never written");
    }
}

static void TestSampleMask(void)
{
    Fixture* f = Valid();
    f->def.alphaToCoverage = true;
    f->def.sampleCount = 4;
    CHECK(Walk(f) == 0, "alpha to coverage without a sample mask");
    f->entries[1].builtins = mrhi_builtinSampleMaskOut;
    CHECK(Walk(f) != 0, "alpha to coverage with the sample mask written");
    f->def.alphaToCoverage = false;
    CHECK(Walk(f) == 0, "the sample mask written without alpha to coverage");
}

static void TestInterface(void)
{
    Fixture* f = Valid();
    f->variables[1].location = 2;
    CHECK(Walk(f) != 0, "a fragment input at another location");
    f = Valid();
    f->variables[1].type = mrhi_scalarSint32;
    CHECK(Walk(f) != 0, "a fragment input of another type");
    f = Valid();
    f->variables[1].components = 3;
    CHECK(Walk(f) != 0, "a fragment input of other components");
    f = Valid();
    f->variables[1].interpolation = mrhi_interpolationLinear;
    CHECK(Walk(f) != 0, "a fragment input interpolated otherwise");
    f = Valid();
    f->variables[1].sampling = mrhi_samplingCentroid;
    CHECK(Walk(f) != 0, "a fragment input sampled otherwise");
    f = Valid();
    f->entries[0].variableCount = 0;
    CHECK(Walk(f) != 0, "a fragment input the vertex entry does not write");
    f = Valid();
    f->limits.interStageVariables = 1;
    CHECK(Walk(f) == 0, "a vertex output at the limit");
    f->def.topology = mrhi_topologyPointList;
    CHECK(Walk(f) != 0, "a point list without room for its point size");
    f->limits.interStageVariables = 2;
    CHECK(Walk(f) == 0, "a point list with room for its point size");
}

int main(void)
{
    TestValid();
    TestConstants();
    TestCompute();
    TestVertex();
    TestTargets();
    TestSecondSource();
    TestSampleMask();
    TestInterface();
    return s_failures == 0 ? 0 : 1;
}
