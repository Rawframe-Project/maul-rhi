// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Compute with an indirect dispatch: a first pass writes, on the GPU,
// the dispatch that covers a count of elements; a second dispatches
// through those arguments and squares each element's index into a
// buffer. The frame declares both buffers, and the graph orders the
// arguments' write before their use as indirect arguments. Past the
// count the buffer keeps what was uploaded.

#include "harness.h"

#include "shaders/compute_plan_container.h"
#include "shaders/compute_square_container.h"

#include <stdio.h>
#include <string.h>

// The elements squared, and the buffer's elements.
#define COUNT    200u
#define ELEMENTS 1024u

// Whole buffers: a buffer's range is not read.
static mrhiAccess Access(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){.resource = resource, .kind = kind};
}

static mrhiPassId AddPass(Sample* sample, mrhiPassClass passClass, const mrhiAccess* accesses,
                          uint32_t count)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = passClass;
    def.accesses = accesses;
    def.accessCount = count;
    mrhiPassId pass = {0};
    SampleCheck(sample, mrhiAddPass(sample->device, &def, &pass) == mrhi_success, "a pass");
    return pass;
}

static mrhiComputePipelineId MakePipeline(Sample* sample, mrhiShaderId shader, const char* entry)
{
    mrhiComputePipelineDef def = mrhiDefaultComputePipelineDef();
    def.shader = shader;
    def.entry = entry;
    def.entryLength = strlen(entry);
    return SampleCompute(sample, &def);
}

// Records the frame: the buffer filled, the dispatch planned, the
// elements squared, the buffer read back.
static mrhiRequestId Record(Sample* sample, mrhiComputePipelineId plan,
                            mrhiComputePipelineId square)
{
    mrhiDevice* device = sample->device;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiBufferDef argumentsDef = mrhiDefaultBufferDef();
    argumentsDef.size = 16;
    mrhiBufferDef valuesDef = mrhiDefaultBufferDef();
    valuesDef.size = ELEMENTS * sizeof(uint32_t);
    mrhiResourceId arguments = {0};
    mrhiResourceId values = {0};
    SampleCheck(sample,
                mrhiBeginFrame(device, &frame) == mrhi_success &&
                    mrhiDeclareBuffer(device, &argumentsDef, &arguments) == mrhi_success &&
                    mrhiDeclareBuffer(device, &valuesDef, &values) == mrhi_success,
                "a frame and its buffers");
    const mrhiAccess fill = Access(values, mrhi_accessCopyDestination);
    mrhiPassId filling = AddPass(sample, mrhi_passTransfer, &fill, 1);
    const mrhiAccess planning = Access(arguments, mrhi_accessStorageWrite);
    mrhiPassId planned = AddPass(sample, mrhi_passGraphics, &planning, 1);
    const mrhiAccess squaring[2] = {Access(arguments, mrhi_accessIndirect),
                                    Access(values, mrhi_accessStorageReadWrite)};
    mrhiPassId squared = AddPass(sample, mrhi_passGraphics, squaring, 2);
    const mrhiAccess reading = Access(values, mrhi_accessCopySource);
    mrhiPassDef readDef = mrhiDefaultPassDef();
    readDef.passClass = mrhi_passTransfer;
    readDef.accesses = &reading;
    readDef.accessCount = 1;
    readDef.neverCull = true;
    mrhiPassId read = {0};
    SampleCheck(sample,
                mrhiAddPass(device, &readDef, &read) == mrhi_success &&
                    mrhiCompileFrame(device) == mrhi_success,
                "the frame compiled");
    static uint32_t ones[ELEMENTS];
    memset(ones, 0xFF, sizeof(ones));
    const uint32_t count = COUNT;
    const mrhiBinding argumentsBinding = {
        .slot = 0, .resource = arguments, .size = MRHI_WHOLE_SIZE};
    const mrhiBinding valuesBinding = {.slot = 0, .resource = values, .size = MRHI_WHOLE_SIZE};
    mrhiRequestId request = {0};
    SampleCheck(sample,
                mrhiBeginPass(device, filling) == mrhi_success &&
                    mrhiWriteBuffer(device, filling, values, 0, ones, sizeof(ones)) ==
                        mrhi_success &&
                    mrhiEndPass(device, filling) == mrhi_success,
                "the buffer filled");
    SampleCheck(sample,
                mrhiBeginPass(device, planned) == mrhi_success &&
                    mrhiSetComputePipeline(device, planned, plan) == mrhi_success &&
                    mrhiSetRootBlock(device, planned, 0, &count, sizeof(count)) == mrhi_success &&
                    mrhiSetBindings(device, planned, 0, &argumentsBinding, 1) == mrhi_success &&
                    mrhiDispatch(device, planned, 1, 1, 1) == mrhi_success &&
                    mrhiEndPass(device, planned) == mrhi_success,
                "the dispatch planned");
    SampleCheck(sample,
                mrhiBeginPass(device, squared) == mrhi_success &&
                    mrhiSetComputePipeline(device, squared, square) == mrhi_success &&
                    mrhiSetRootBlock(device, squared, 0, &count, sizeof(count)) == mrhi_success &&
                    mrhiSetBindings(device, squared, 0, &valuesBinding, 1) == mrhi_success &&
                    mrhiDispatchIndirect(device, squared, arguments, 0) == mrhi_success &&
                    mrhiEndPass(device, squared) == mrhi_success,
                "the elements squared");
    SampleCheck(sample,
                mrhiBeginPass(device, read) == mrhi_success &&
                    mrhiReadBuffer(device, read, values, 0, valuesDef.size, &request) ==
                        mrhi_success &&
                    mrhiEndPass(device, read) == mrhi_success,
                "the buffer read");
    return request;
}

int main(void)
{
    Sample sample;
    int opened = SampleOpen(&sample, nullptr);
    if (opened != 0)
    {
        return opened;
    }
    mrhiShaderId planShader =
        SampleShader(&sample, s_compute_planContainer, sizeof(s_compute_planContainer));
    mrhiShaderId squareShader =
        SampleShader(&sample, s_compute_squareContainer, sizeof(s_compute_squareContainer));
    mrhiComputePipelineId plan = MakePipeline(&sample, planShader, "plan");
    mrhiComputePipelineId square = MakePipeline(&sample, squareShader, "square");
    mrhiRequestId request = Record(&sample, plan, square);
    static uint32_t values[ELEMENTS];
    if (SampleFinish(&sample) && SampleTake(&sample, request, values, sizeof(values)))
    {
        uint32_t wrong = ELEMENTS;
        for (uint32_t i = 0; i < ELEMENTS && wrong == ELEMENTS; ++i)
        {
            wrong = values[i] == (i < COUNT ? i * i : UINT32_MAX) ? ELEMENTS : i;
        }
        char what[64];
        snprintf(what, sizeof(what), "element %u", wrong);
        SampleCheck(&sample, wrong == ELEMENTS, what);
    }
    mrhiDevice* device = sample.device;
    SampleCheck(&sample,
                mrhiDestroyComputePipeline(device, plan) == mrhi_success &&
                    mrhiDestroyComputePipeline(device, square) == mrhi_success &&
                    mrhiDestroyShader(device, planShader) == mrhi_success &&
                    mrhiDestroyShader(device, squareShader) == mrhi_success,
                "the pipelines and shaders destroyed");
    return SampleClose(&sample);
}
