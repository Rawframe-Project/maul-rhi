// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A triangle: one pipeline from a shader container, a target the frame
// declares, cleared and drawn in one pass, then read back in a second.
// The triangle's color comes from the root block. Windowless, as every
// sample is unless presenting is its subject.

#include "harness.h"

#include "shaders/triangle_container.h"

#include <stdio.h>

// The target's width and height.
#define SIZE 64

static const mrhiClearColor kBackground = {0.2f, 0.4f, 0.6f, 1.0f};
static const float kTriangle[4] = {1.0f, 0.6f, 0.2f, 1.0f};

static mrhiGraphicsPipelineId MakePipeline(Sample* sample, mrhiShaderId shader)
{
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    def.colorTargetCount = 1;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    return SampleGraphics(sample, &def);
}

// Records the frame: the draw, and the target's readback.
static mrhiRequestId Record(Sample* sample, mrhiGraphicsPipelineId pipeline)
{
    mrhiDevice* device = sample->device;
    mrhiTextureDef targetDef = mrhiDefaultTextureDef();
    targetDef.format = mrhi_formatRgba8Unorm;
    targetDef.width = SIZE;
    targetDef.height = SIZE;
    mrhiResourceId target = {0};
    SampleCheck(sample, mrhiDeclareTexture(device, &targetDef, &target) == mrhi_success,
                "a target");
    mrhiPassDef drawDef = mrhiDefaultPassDef();
    drawDef.colorTargets[0] = (mrhiColorTarget){
        .resource = target,
        .load = mrhi_loadClear,
        .store = mrhi_storeKeep,
        .clear = kBackground,
    };
    drawDef.colorTargetCount = 1;
    mrhiPassId draw = {0};
    SampleCheck(sample, mrhiAddPass(device, &drawDef, &draw) == mrhi_success, "a drawing pass");
    const mrhiAccess read = {
        .resource = target,
        .kind = mrhi_accessCopySource,
        .range = {.mipCount = 1, .layerCount = 1},
    };
    mrhiPassDef readDef = mrhiDefaultPassDef();
    readDef.passClass = mrhi_passTransfer;
    readDef.accesses = &read;
    readDef.accessCount = 1;
    readDef.neverCull = true;
    mrhiPassId reading = {0};
    SampleCheck(sample, mrhiAddPass(device, &readDef, &reading) == mrhi_success, "a reading pass");
    SampleCheck(sample, mrhiCompileFrame(device) == mrhi_success, "the frame compiled");
    SampleCheck(sample,
                mrhiBeginPass(device, draw) == mrhi_success &&
                    mrhiSetGraphicsPipeline(device, draw, pipeline) == mrhi_success &&
                    mrhiSetRootBlock(device, draw, 0, kTriangle, sizeof(kTriangle)) ==
                        mrhi_success &&
                    mrhiDraw(device, draw, 3, 1, 0, 0) == mrhi_success &&
                    mrhiEndPass(device, draw) == mrhi_success,
                "the triangle drawn");
    const mrhiTextureCopy source = {.resource = target};
    const mrhiExtent3d extent = {SIZE, SIZE, 1};
    mrhiRequestId pixels = {0};
    SampleCheck(sample,
                mrhiBeginPass(device, reading) == mrhi_success &&
                    mrhiReadTexture(device, reading, &source, &extent, &pixels) == mrhi_success &&
                    mrhiEndPass(device, reading) == mrhi_success,
                "the target read");
    return pixels;
}

// Probes the image well inside and outside the triangle, whose apex is
// up: the rows run down from the top.
static void Check(Sample* sample, const uint8_t* image)
{
    static const uint8_t background[4] = {51, 102, 153, 255};
    static const uint8_t triangle[4] = {255, 153, 51, 255};
    static const struct
    {
        int x;
        int y;
        bool inside;
    } probes[] = {
        {32, 32, true},  {32, 20, true}, {20, 44, true},  {44, 44, true}, {20, 20, false},
        {44, 20, false}, {4, 4, false},  {60, 60, false}, {32, 8, false}, {32, 56, false},
    };
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i)
    {
        const uint8_t* pixel = &image[(probes[i].y * SIZE + probes[i].x) * 4];
        char what[64];
        snprintf(what, sizeof(what), "pixel (%d, %d) %s", probes[i].x, probes[i].y,
                 probes[i].inside ? "the triangle's" : "the background's");
        SampleCheck(sample, SampleNear(pixel, probes[i].inside ? triangle : background, 1), what);
    }
}

int main(void)
{
    Sample sample;
    int opened = SampleOpen(&sample, nullptr);
    if (opened != 0)
    {
        return opened;
    }
    mrhiShaderId shader = SampleShader(&sample, s_triangleContainer, sizeof(s_triangleContainer));
    mrhiGraphicsPipelineId pipeline = MakePipeline(&sample, shader);
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    SampleCheck(&sample, mrhiBeginFrame(sample.device, &frame) == mrhi_success, "a frame");
    mrhiRequestId pixels = Record(&sample, pipeline);
    static uint8_t image[SIZE * SIZE * 4];
    if (SampleFinish(&sample) && SampleTake(&sample, pixels, image, sizeof(image)))
    {
        Check(&sample, image);
    }
    SampleCheck(&sample,
                mrhiDestroyGraphicsPipeline(sample.device, pipeline) == mrhi_success &&
                    mrhiDestroyShader(sample.device, shader) == mrhi_success,
                "the pipeline and shader destroyed");
    return SampleClose(&sample);
}
