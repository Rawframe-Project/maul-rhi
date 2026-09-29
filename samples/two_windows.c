// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Two windows on one device, each surface configured with its own
// swapchain in the color its platform prefers. Every frame acquires both
// images, clears each to its own color, which changes from frame to
// frame, and presents both; where the surfaces allow, the frame reads
// each image's first pixel back, which must be its window's color.

#include "harness.h"
#include "window.h"

#include <stdio.h>

#define WIDTH  64
#define HEIGHT 48
#define FRAMES 4

// Each window's color in each frame: red and blue, then swapped.
static mrhiClearColor ColorOf(int window, int frame)
{
    bool red = (window + frame) % 2 == 0;
    return (mrhiClearColor){red ? 1.0f : 0.0f, 0.0f, red ? 0.0f : 1.0f, 1.0f};
}

// Configures a window's surface in the color its caps list first: the
// color's format, and whether the image can be read back.
static bool Configure(Sample* sample, const SampleWindow* window, mrhiFormat* formatOut)
{
    mrhiSurfaceCaps caps;
    SampleCheck(sample,
                mrhiGetSurfaceCaps(sample->instance, window->surface, sample->adapter, &caps) ==
                        mrhi_success &&
                    caps.presentable,
                "the adapter presents to each window");
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    config.surface = window->surface;
    SampleCheck(sample,
                mrhiSuggestSurfaceColor(&caps, &caps.colors[0], &config.color) == mrhi_success,
                "the preferred color");
    config.usage = mrhi_textureRenderTarget | (caps.usages & mrhi_textureCopySource);
    config.width = WIDTH;
    config.height = HEIGHT;
    SampleCheck(sample, mrhiConfigureSurface(sample->device, &config) == mrhi_success,
                "each surface configured");
    *formatOut = config.color.format;
    return (caps.usages & mrhi_textureCopySource) != 0;
}

// Adds a pass clearing an image, and one reading its first pixel back
// when asked.
static void AddClear(Sample* sample, mrhiResourceId image, mrhiClearColor color, bool read,
                     mrhiPassId* clearOut, mrhiPassId* readOut)
{
    mrhiPassDef clearDef = mrhiDefaultPassDef();
    clearDef.colorTargets[0] = (mrhiColorTarget){
        .resource = image,
        .load = mrhi_loadClear,
        .store = mrhi_storeKeep,
        .clear = color,
    };
    clearDef.colorTargetCount = 1;
    SampleCheck(sample, mrhiAddPass(sample->device, &clearDef, clearOut) == mrhi_success,
                "a clearing pass");
    if (!read)
    {
        return;
    }
    const mrhiAccess copy = {
        .resource = image,
        .kind = mrhi_accessCopySource,
        .range = {.mipCount = 1, .layerCount = 1},
    };
    mrhiPassDef readDef = mrhiDefaultPassDef();
    readDef.passClass = mrhi_passTransfer;
    readDef.accesses = &copy;
    readDef.accessCount = 1;
    readDef.neverCull = true;
    SampleCheck(sample, mrhiAddPass(sample->device, &readDef, readOut) == mrhi_success,
                "a reading pass");
}

// Whether a pixel is a color, in an 8-bit format's channel order; other
// formats are not read.
static bool IsColor(const uint8_t* pixel, mrhiFormat format, mrhiClearColor color)
{
    uint8_t red = color.red > 0.5f ? 255 : 0;
    uint8_t blue = color.blue > 0.5f ? 255 : 0;
    bool bgra = format == mrhi_formatBgra8Unorm;
    const uint8_t expected[4] = {bgra ? blue : red, 0, bgra ? red : blue, 255};
    return SampleNear(pixel, expected, 0);
}

int main(void)
{
    Sample sample;
    int opened = SampleOpen(&sample, nullptr);
    if (opened != 0)
    {
        return opened;
    }
    SampleWindow windows[2];
    for (int i = 0; i < 2; ++i)
    {
        opened = SampleWindowOpen(&sample, &windows[i], i, WIDTH, HEIGHT);
        if (opened != 0)
        {
            for (int j = 0; j < i; ++j)
            {
                SampleWindowClose(&sample, &windows[j]);
            }
            SampleClose(&sample);
            return opened;
        }
    }
    mrhiFormat formats[2];
    bool read[2];
    for (int i = 0; i < 2; ++i)
    {
        read[i] = Configure(&sample, &windows[i], &formats[i]);
        read[i] =
            read[i] && (formats[i] == mrhi_formatRgba8Unorm || formats[i] == mrhi_formatBgra8Unorm);
    }
    mrhiDevice* device = sample.device;
    const mrhiTextureCopy none = {0};
    const mrhiExtent3d one = {1, 1, 1};
    for (int frame = 0; frame < FRAMES; ++frame)
    {
        mrhiFrameDef frameDef = mrhiDefaultFrameDef();
        SampleCheck(&sample, mrhiBeginFrame(device, &frameDef) == mrhi_success, "a frame");
        mrhiResourceId images[2] = {{0}};
        mrhiPassId clears[2] = {{0}};
        mrhiPassId reads[2] = {{0}};
        for (int i = 0; i < 2; ++i)
        {
            SampleCheck(&sample,
                        mrhiAcquireSurfaceImage(device, windows[i].surface, &images[i]) ==
                            mrhi_success,
                        "both images acquired");
            AddClear(&sample, images[i], ColorOf(i, frame), read[i], &clears[i], &reads[i]);
        }
        SampleCheck(&sample, mrhiCompileFrame(device) == mrhi_success, "the frame compiled");
        mrhiRequestId pixels[2] = {{0}};
        for (int i = 0; i < 2; ++i)
        {
            mrhiTextureCopy source = none;
            source.resource = images[i];
            SampleCheck(&sample,
                        mrhiBeginPass(device, clears[i]) == mrhi_success &&
                            mrhiEndPass(device, clears[i]) == mrhi_success &&
                            (!read[i] || (mrhiBeginPass(device, reads[i]) == mrhi_success &&
                                          mrhiReadTexture(device, reads[i], &source, &one,
                                                          &pixels[i]) == mrhi_success &&
                                          mrhiEndPass(device, reads[i]) == mrhi_success)),
                        "each image cleared");
        }
        SampleFinish(&sample);
        for (int i = 0; i < 2; ++i)
        {
            uint8_t pixel[4] = {0};
            char what[48];
            snprintf(what, sizeof(what), "window %d's color in frame %d", i, frame);
            SampleCheck(&sample,
                        !read[i] || (SampleTake(&sample, pixels[i], pixel, sizeof(pixel)) &&
                                     IsColor(pixel, formats[i], ColorOf(i, frame))),
                        what);
        }
    }
    for (int i = 0; i < 2; ++i)
    {
        SampleCheck(&sample, mrhiUnconfigureSurface(device, windows[i].surface) == mrhi_success,
                    "each surface unconfigured");
        SampleWindowClose(&sample, &windows[i]);
    }
    return SampleClose(&sample);
}
