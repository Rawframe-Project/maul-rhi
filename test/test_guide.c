// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The guide's C snippets (docs/guide.md), each as written there
// (tools/check_guide.py checks it, family record 0019), run on the first
// adapter the native drivers list: the device opened by the guide's
// OpenDevice, a triangle drawn and read back by its DrawAndRead, the
// pixels checked. Skips without an adapter unless MAUL_RHI_REQUIRE_VULKAN
// is set, as the samples do; not built for the web, where the guide's
// waits are a later frame's.

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "harness.h"

#include "maul-rhi/device.h"
#include "maul-rhi/instance.h"
#include "shaders/triangle_container.h"

#include <stdio.h>
#include <stdlib.h>

// Opens a device on the best adapter, software rasterizers allowed.
// The caller destroys what it got, the device first: *deviceOut when
// not NULL, then *instanceOut when not NULL.
static bool OpenDevice(mrhiInstance** instanceOut, mrhiDevice** deviceOut)
{
    *instanceOut = NULL;
    *deviceOut = NULL;
    mrhiInstanceDef instanceDef = mrhiDefaultInstanceDef();
    if (mrhiCreateInstance(&instanceDef, instanceOut) != mrhi_success)
    {
        return false;
    }
    mrhiInstance* instance = *instanceOut;

    // Natively each answer is queued by the time the call returns; on
    // the web it comes between the page's tasks, so a page looks again
    // on a later frame.
    mrhiAdapterRequestDef request = mrhiDefaultAdapterRequestDef();
    request.allowSoftware = true;
    mrhiRequestId searched;
    mrhiInstanceNotification record;
    mrhiAdapterId adapter;
    size_t count = 0;
    if (mrhiRequestAdapters(instance, &request, &searched) != mrhi_success ||
        mrhiNextInstanceNotification(instance, &record) != mrhi_success ||
        record.outcome != mrhi_success ||
        mrhiGetAdapters(instance, &adapter, 1, &count) != mrhi_success || count == 0)
    {
        return false;
    }

    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    mrhiRequestId opened;
    if (mrhiCreateDevice(instance, &deviceDef, deviceOut, &opened) != mrhi_success)
    {
        return false;
    }
    // mrhi_instanceDeviceReady answers the opening.
    return mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
           record.kind == mrhi_instanceDeviceReady && record.outcome == mrhi_success;
}

// Draws a triangle in a color its pipeline's root block takes into a
// 64x64 target the frame alone holds, and reads the target back: the
// pixels answer *pixelsOut once the frame *tokenOut has finished.
static bool DrawAndRead(mrhiDevice* device, mrhiGraphicsPipelineId pipeline, const float color[4],
                        mrhiRequestId* pixelsOut, mrhiRequestId* tokenOut)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    if (mrhiBeginFrame(device, &frame) != mrhi_success)
    {
        return false;
    }

    mrhiTextureDef targetDef = mrhiDefaultTextureDef();
    targetDef.format = mrhi_formatRgba8Unorm;
    targetDef.width = 64;
    targetDef.height = 64;
    mrhiResourceId target = {0};
    bool built = mrhiDeclareTexture(device, &targetDef, &target) == mrhi_success;

    mrhiPassDef drawDef = mrhiDefaultPassDef();
    drawDef.colorTargets[0] =
        (mrhiColorTarget){.resource = target, .load = mrhi_loadClear, .store = mrhi_storeKeep};
    drawDef.colorTargetCount = 1;
    mrhiPassId draw = {0};
    built = built && mrhiAddPass(device, &drawDef, &draw) == mrhi_success;

    const mrhiAccess read = {.resource = target,
                             .kind = mrhi_accessCopySource,
                             .range = {.mipCount = 1, .layerCount = 1}};
    mrhiPassDef readDef = mrhiDefaultPassDef();
    readDef.passClass = mrhi_passTransfer;
    readDef.accesses = &read;
    readDef.accessCount = 1;
    readDef.neverCull = true;
    mrhiPassId reading = {0};
    built = built && mrhiAddPass(device, &readDef, &reading) == mrhi_success &&
            mrhiCompileFrame(device) == mrhi_success;

    const mrhiTextureCopy source = {.resource = target};
    const mrhiExtent3d extent = {64, 64, 1};
    bool recorded = built && mrhiBeginPass(device, draw) == mrhi_success &&
                    mrhiSetGraphicsPipeline(device, draw, pipeline) == mrhi_success &&
                    mrhiSetRootBlock(device, draw, 0, color, 4 * sizeof(float)) == mrhi_success &&
                    mrhiDraw(device, draw, 3, 1, 0, 0) == mrhi_success &&
                    mrhiEndPass(device, draw) == mrhi_success &&
                    mrhiBeginPass(device, reading) == mrhi_success &&
                    mrhiReadTexture(device, reading, &source, &extent, pixelsOut) == mrhi_success &&
                    mrhiEndPass(device, reading) == mrhi_success;
    if (!recorded)
    {
        // Nothing of the frame runs.
        (void)mrhiDropFrame(device);
        return false;
    }
    return mrhiSubmitFrame(device, tokenOut) == mrhi_success;
}

// The pipeline of the samples' triangle, which draws in its root block's
// color.
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

int main(void)
{
    Sample sample = {0};
    if (!OpenDevice(&sample.instance, &sample.device))
    {
        const char* required = getenv("MAUL_RHI_REQUIRE_VULKAN");
        bool require = required != nullptr && required[0] != '\0';
        printf("%s: the guide's OpenDevice found no device\n", require ? "FAIL" : "skip");
        if (sample.device != nullptr)
        {
            mrhiDestroyDevice(sample.device);
        }
        if (sample.instance != nullptr)
        {
            mrhiDestroyInstance(sample.instance);
        }
        return require ? 1 : SAMPLE_SKIPPED;
    }
    mrhiShaderId shader = SampleShader(&sample, s_triangleContainer, sizeof(s_triangleContainer));
    mrhiGraphicsPipelineId pipeline = MakePipeline(&sample, shader);
    static const float color[4] = {1.0f, 0.6f, 0.2f, 1.0f};
    mrhiRequestId pixels = {0};
    mrhiRequestId token = {0};
    static uint8_t image[64 * 64 * 4];
    if (SampleCheck(&sample, DrawAndRead(sample.device, pipeline, color, &pixels, &token),
                    "the guide's DrawAndRead") &&
        SampleWait(&sample, token) && SampleTake(&sample, pixels, image, sizeof(image)))
    {
        static const uint8_t triangle[4] = {255, 153, 51, 255};
        static const uint8_t cleared[4] = {0, 0, 0, 0};
        SampleCheck(&sample, SampleNear(&image[(32 * 64 + 32) * 4], triangle, 1),
                    "the triangle in its color");
        SampleCheck(&sample, SampleNear(&image[(4 * 64 + 4) * 4], cleared, 0),
                    "the target cleared around it");
    }
    SampleCheck(&sample,
                mrhiDestroyGraphicsPipeline(sample.device, pipeline) == mrhi_success &&
                    mrhiDestroyShader(sample.device, shader) == mrhi_success,
                "the pipeline and shader destroyed");
    return SampleClose(&sample);
}
