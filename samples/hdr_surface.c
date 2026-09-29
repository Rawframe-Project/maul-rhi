// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// An HDR surface: the program asks for half floats in linear Rec. 709
// of extended range, the composition format HDR on desktops uses, and
// takes the library's suggestion from the surface's caps: that color
// where the surface offers it, 8-bit sRGB where it offers no better.
// Frames clear the image to a color past 1.0 in red and present it; the
// first frame reads its first pixel back where the surface allows,
// which keeps the value past 1.0 in half floats and is clamped in 8 bits.

#include "harness.h"
#include "window.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define WIDTH  64
#define HEIGHT 48

static const mrhiClearColor kColor = {2.0f, 0.5f, 0.25f, 1.0f};

// A half float's value; the sample reads only normal numbers.
static float HalfToFloat(uint16_t half)
{
    int exponent = (half >> 10) & 0x1F;
    float mantissa = 1.0f + (float)(half & 0x3FF) / 1024.0f;
    float value = ldexpf(mantissa, exponent - 15);
    return (half & 0x8000) != 0 ? -value : value;
}

// Configures the surface with the suggested color: the color.
static mrhiSurfaceColor Configure(Sample* sample, const SampleWindow* window,
                                  const mrhiSurfaceCaps* caps)
{
    const mrhiSurfaceColor asked = {mrhi_formatRgba16Float, mrhi_primariesBt709,
                                    mrhi_transferLinear, mrhi_rangeExtended};
    mrhiSurfaceColor color = {0};
    SampleCheck(sample, mrhiSuggestSurfaceColor(caps, &asked, &color) == mrhi_success,
                "a suggested color");
    bool hdr = memcmp(&color, &asked, sizeof(color)) == 0;
    printf("presenting %s\n", hdr ? "half floats in linear Rec. 709 of extended range"
                                  : "8-bit sRGB, the surface offering no HDR");
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    config.surface = window->surface;
    config.color = color;
    config.usage = mrhi_textureRenderTarget | (caps->usages & mrhi_textureCopySource);
    config.width = WIDTH;
    config.height = HEIGHT;
    SampleCheck(sample, mrhiConfigureSurface(sample->device, &config) == mrhi_success,
                "the surface configured");
    return color;
}

// One frame clearing the image, and reading its first pixel back when
// asked: the readback, or a null request.
static mrhiRequestId Present(Sample* sample, const SampleWindow* window, bool read)
{
    mrhiDevice* device = sample->device;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId image = {0};
    SampleCheck(sample,
                mrhiBeginFrame(device, &frame) == mrhi_success &&
                    mrhiAcquireSurfaceImage(device, window->surface, &image) == mrhi_success,
                "an image acquired");
    mrhiPassDef clearDef = mrhiDefaultPassDef();
    clearDef.colorTargets[0] = (mrhiColorTarget){
        .resource = image,
        .load = mrhi_loadClear,
        .store = mrhi_storeKeep,
        .clear = kColor,
    };
    clearDef.colorTargetCount = 1;
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
    mrhiPassId clear = {0};
    mrhiPassId reading = {0};
    SampleCheck(sample,
                mrhiAddPass(device, &clearDef, &clear) == mrhi_success &&
                    (!read || mrhiAddPass(device, &readDef, &reading) == mrhi_success) &&
                    mrhiCompileFrame(device) == mrhi_success &&
                    mrhiBeginPass(device, clear) == mrhi_success &&
                    mrhiEndPass(device, clear) == mrhi_success,
                "the image cleared");
    mrhiRequestId pixel = {0};
    if (read)
    {
        const mrhiTextureCopy source = {.resource = image};
        const mrhiExtent3d one = {1, 1, 1};
        SampleCheck(sample,
                    mrhiBeginPass(device, reading) == mrhi_success &&
                        mrhiReadTexture(device, reading, &source, &one, &pixel) == mrhi_success &&
                        mrhiEndPass(device, reading) == mrhi_success,
                    "the first pixel read");
    }
    SampleFinish(sample);
    return pixel;
}

// The first pixel as the chosen format keeps it: 2.0 in half floats, or
// clamped to 255 in 8 bits, in the format's channel order.
static void CheckPixel(Sample* sample, mrhiSurfaceColor color, mrhiRequestId request)
{
    if (color.format == mrhi_formatRgba16Float)
    {
        uint16_t halves[4] = {0};
        SampleCheck(sample,
                    SampleTake(sample, request, halves, sizeof(halves)) &&
                        HalfToFloat(halves[0]) == 2.0f && HalfToFloat(halves[1]) == 0.5f &&
                        HalfToFloat(halves[2]) == 0.25f && HalfToFloat(halves[3]) == 1.0f,
                    "red past 1.0, kept in half floats");
        return;
    }
    bool bgra = color.format == mrhi_formatBgra8Unorm;
    const uint8_t expected[4] = {bgra ? 64 : 255, 128, bgra ? 255 : 64, 255};
    uint8_t pixel[4] = {0};
    SampleCheck(sample,
                SampleTake(sample, request, pixel, sizeof(pixel)) && SampleNear(pixel, expected, 1),
                "red clamped to 1.0 in 8 bits");
}

int main(void)
{
    Sample sample;
    int opened = SampleOpen(&sample, nullptr);
    if (opened != 0)
    {
        return opened;
    }
    SampleWindow window;
    opened = SampleWindowOpen(&sample, &window, 0, WIDTH, HEIGHT);
    if (opened != 0)
    {
        SampleClose(&sample);
        return opened;
    }
    mrhiSurfaceCaps caps;
    SampleCheck(&sample,
                mrhiGetSurfaceCaps(sample.instance, window.surface, sample.adapter, &caps) ==
                        mrhi_success &&
                    caps.presentable,
                "the adapter presents there");
    mrhiSurfaceColor color = Configure(&sample, &window, &caps);
    bool read = (caps.usages & mrhi_textureCopySource) != 0;
    mrhiRequestId pixel = Present(&sample, &window, read);
    if (read)
    {
        CheckPixel(&sample, color, pixel);
    }
    for (int i = 0; i < 3; ++i)
    {
        Present(&sample, &window, false);
    }
    SampleCheck(&sample, mrhiUnconfigureSurface(sample.device, window.surface) == mrhi_success,
                "the surface unconfigured");
    SampleWindowClose(&sample, &window);
    return SampleClose(&sample);
}
