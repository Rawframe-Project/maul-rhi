// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Multisampling with a resolve: the triangle sample's triangle drawn
// into a four-sample target the frame declares, resolved at the pass's
// end into a single-sample target, which is read back. Inside the
// triangle and outside it the pixels keep their colors; along its
// slanted edges some pixels are partly covered, and resolve to a blend
// of the two, which one sample per pixel never gives.

#include "harness.h"

#include "shaders/triangle_container.h"

#include <stdio.h>

// The targets' width and height, and the samples per pixel.
#define SIZE    64
#define SAMPLES 4

static const mrhiClearColor kBackground = {0.0f, 0.0f, 0.0f, 1.0f};
static const float kTriangle[4] = {1.0f, 1.0f, 1.0f, 1.0f};

static mrhiGraphicsPipelineId MakePipeline(Sample* sample, mrhiShaderId shader)
{
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    def.sampleCount = SAMPLES;
    def.colorTargetCount = 1;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    return SampleGraphics(sample, &def);
}

static mrhiResourceId Declare(Sample* sample, uint32_t samples)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = SIZE;
    def.height = SIZE;
    def.sampleCount = samples;
    mrhiResourceId texture = {0};
    SampleCheck(sample, mrhiDeclareTexture(sample->device, &def, &texture) == mrhi_success,
                "a target");
    return texture;
}

// Records the frame: the multisampled draw resolved, then read back.
static mrhiRequestId Record(Sample* sample, mrhiGraphicsPipelineId pipeline)
{
    mrhiDevice* device = sample->device;
    mrhiResourceId samples = Declare(sample, SAMPLES);
    mrhiResourceId resolved = Declare(sample, 1);
    mrhiPassDef drawDef = mrhiDefaultPassDef();
    // The samples themselves are needed only until they are resolved.
    drawDef.colorTargets[0] = (mrhiColorTarget){
        .resource = samples,
        .load = mrhi_loadClear,
        .store = mrhi_storeDiscard,
        .clear = kBackground,
        .resolve = resolved,
    };
    drawDef.colorTargetCount = 1;
    const mrhiAccess read = {
        .resource = resolved,
        .kind = mrhi_accessCopySource,
        .range = {.mipCount = 1, .layerCount = 1},
    };
    mrhiPassDef readDef = mrhiDefaultPassDef();
    readDef.passClass = mrhi_passTransfer;
    readDef.accesses = &read;
    readDef.accessCount = 1;
    readDef.neverCull = true;
    mrhiPassId draw = {0};
    mrhiPassId reading = {0};
    SampleCheck(sample,
                mrhiAddPass(device, &drawDef, &draw) == mrhi_success &&
                    mrhiAddPass(device, &readDef, &reading) == mrhi_success &&
                    mrhiCompileFrame(device) == mrhi_success,
                "the frame compiled");
    SampleCheck(sample,
                mrhiBeginPass(device, draw) == mrhi_success &&
                    mrhiSetGraphicsPipeline(device, draw, pipeline) == mrhi_success &&
                    mrhiSetRootBlock(device, draw, 0, kTriangle, sizeof(kTriangle)) ==
                        mrhi_success &&
                    mrhiDraw(device, draw, 3, 1, 0, 0) == mrhi_success &&
                    mrhiEndPass(device, draw) == mrhi_success,
                "the triangle drawn and resolved");
    const mrhiTextureCopy source = {.resource = resolved};
    const mrhiExtent3d extent = {SIZE, SIZE, 1};
    mrhiRequestId pixels = {0};
    SampleCheck(sample,
                mrhiBeginPass(device, reading) == mrhi_success &&
                    mrhiReadTexture(device, reading, &source, &extent, &pixels) == mrhi_success &&
                    mrhiEndPass(device, reading) == mrhi_success,
                "the resolved target read");
    return pixels;
}

// The inside white, the outside black, and blends along the edges: every
// pixel is one of the two, or a gray between them, of which there are
// some.
static void Check(Sample* sample, const uint8_t* image)
{
    static const uint8_t black[4] = {0, 0, 0, 255};
    static const uint8_t white[4] = {255, 255, 255, 255};
    SampleCheck(sample, SampleNear(&image[(32 * SIZE + 32) * 4], white, 1), "the inside white");
    SampleCheck(sample, SampleNear(&image[(4 * SIZE + 4) * 4], black, 1), "the outside black");
    int blends = 0;
    int others = 0;
    for (int i = 0; i < SIZE * SIZE; ++i)
    {
        const uint8_t* pixel = &image[i * 4];
        bool gray = pixel[0] == pixel[1] && pixel[1] == pixel[2] && pixel[3] == 255;
        bool plain = SampleNear(pixel, black, 1) || SampleNear(pixel, white, 1);
        blends += gray && !plain ? 1 : 0;
        others += gray ? 0 : 1;
    }
    char what[64];
    snprintf(what, sizeof(what), "blended edge pixels (%d)", blends);
    SampleCheck(sample, blends > 0, what);
    SampleCheck(sample, others == 0, "every pixel a gray");
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
