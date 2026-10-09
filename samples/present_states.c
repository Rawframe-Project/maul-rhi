// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The present state machine, step by step on one window: acquiring
// before the surface is configured is refused; configured, frames
// present; an image acquired and then given back with its dropped frame
// is taken again by the next; after the window is resized an acquire
// answers that the configuration is out of date, or at best suboptimal,
// at once or after a present, and the program configures again at the
// window's new size; once
// unconfigured, acquiring is refused again; configured once more, the
// surface is destroyed while a frame it presents to is still running,
// which is safe.

#include "harness.h"
#include "window.h"

#include <stdio.h>

#define WIDTH  64
#define HEIGHT 48
// The frames a resize may take to be told.
#define RESIZE_FRAMES 60

static mrhiResult Configure(Sample* sample, const SampleWindow* window, mrhiSurfaceColor color,
                            uint32_t width, uint32_t height)
{
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    config.surface = window->surface;
    config.color = color;
    config.width = width;
    config.height = height;
    return mrhiConfigureSurface(sample->device, &config);
}

// Opens a frame and acquires the window's image: the acquire's outcome.
// Without an image the frame is dropped.
static mrhiResult Acquire(Sample* sample, const SampleWindow* window, mrhiResourceId* imageOut)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    SampleCheck(sample, mrhiBeginFrame(sample->device, &frame) == mrhi_success, "a frame");
    mrhiResult acquired = mrhiAcquireSurfaceImage(sample->device, window->surface, imageOut);
    if (acquired != mrhi_success && acquired != mrhi_suboptimal)
    {
        SampleCheck(sample, mrhiDropFrame(sample->device) == mrhi_success, "the frame dropped");
    }
    return acquired;
}

// Clears the acquired image in the open frame and submits it: its token.
static mrhiRequestId ClearAndSubmit(Sample* sample, mrhiResourceId image)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){
        .resource = image,
        .load = mrhi_loadClear,
        .store = mrhi_storeKeep,
        .clear = {0.1f, 0.6f, 0.3f, 1.0f},
    };
    def.colorTargetCount = 1;
    mrhiPassId pass = {0};
    mrhiRequestId token = {0};
    SampleCheck(sample,
                mrhiAddPass(sample->device, &def, &pass) == mrhi_success &&
                    mrhiCompileFrame(sample->device) == mrhi_success &&
                    mrhiBeginPass(sample->device, pass) == mrhi_success &&
                    mrhiEndPass(sample->device, pass) == mrhi_success &&
                    mrhiSubmitFrame(sample->device, &token) == mrhi_success,
                "the image cleared and submitted");
    return token;
}

// A frame that presents: whether its image was acquired.
static bool Present(Sample* sample, const SampleWindow* window)
{
    mrhiResourceId image = {0};
    mrhiResult acquired = Acquire(sample, window, &image);
    if (acquired != mrhi_success && acquired != mrhi_suboptimal)
    {
        return false;
    }
    return SampleWait(sample, ClearAndSubmit(sample, image));
}

// Resizes the window: an acquire tells the configuration is out of date
// or suboptimal, at once on some platforms and after the next present on
// others, so frames present until one does, up to RESIZE_FRAMES: where a
// thread of the platform's own watches the window (Mesa's X11 swapchain)
// it may tell several frames late on a busy machine. Presenting works
// again once the surface is configured at the new size.
static void Resize(Sample* sample, SampleWindow* window, mrhiSurfaceColor color)
{
    SampleWindowResize(window, WIDTH + 16, HEIGHT + 12);
    mrhiResult acquired = mrhi_success;
    for (int frame = 0; frame < RESIZE_FRAMES && acquired == mrhi_success; ++frame)
    {
        mrhiResourceId image = {0};
        acquired = Acquire(sample, window, &image);
        if (acquired == mrhi_success)
        {
            SampleWait(sample, ClearAndSubmit(sample, image));
        }
        else if (acquired == mrhi_suboptimal)
        {
            // A suboptimal image may still be presented; this program
            // configures again first.
            SampleCheck(sample, mrhiDropFrame(sample->device) == mrhi_success,
                        "the image given back");
        }
        printf("frame %d after the resize: the acquire answered %d\n", frame, (int)acquired);
    }
    SampleCheck(sample, acquired == mrhi_errorOutOfDate || acquired == mrhi_suboptimal,
                "out of date, or suboptimal, after the resize");
    SampleCheck(sample,
                Configure(sample, window, color, WIDTH + 16, HEIGHT + 12) == mrhi_success &&
                    Present(sample, window),
                "presenting at the new size");
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
    mrhiSurfaceColor color = {0};
    SampleCheck(&sample,
                mrhiGetSurfaceCaps(sample.instance, window.surface, sample.adapter, &caps) ==
                        mrhi_success &&
                    mrhiSuggestSurfaceColor(&caps, &caps.colors[0], &color) == mrhi_success,
                "the preferred color");
    mrhiResourceId image = {0};
    SampleCheck(&sample, Acquire(&sample, &window, &image) == mrhi_errorState,
                "no image before the surface is configured");
    SampleCheck(&sample, Configure(&sample, &window, color, WIDTH, HEIGHT) == mrhi_success,
                "configured");
    for (int i = 0; i < 3; ++i)
    {
        SampleCheck(&sample, Present(&sample, &window), "a frame presented");
    }
    SampleCheck(&sample,
                Acquire(&sample, &window, &image) == mrhi_success &&
                    mrhiDropFrame(sample.device) == mrhi_success,
                "an image given back with its frame");
    SampleCheck(&sample, Present(&sample, &window), "the image taken again");
    Resize(&sample, &window, color);
    SampleCheck(&sample, mrhiUnconfigureSurface(sample.device, window.surface) == mrhi_success,
                "unconfigured");
    SampleCheck(&sample, Acquire(&sample, &window, &image) == mrhi_errorState,
                "no image once unconfigured");
    SampleCheck(&sample,
                Configure(&sample, &window, color, WIDTH + 16, HEIGHT + 12) == mrhi_success &&
                    Acquire(&sample, &window, &image) == mrhi_success,
                "configured again");
    mrhiRequestId running = ClearAndSubmit(&sample, image);
    // The surface's configuration ends first, and the running frame
    // finishes on its own.
    SampleWindowClose(&sample, &window);
    SampleCheck(&sample, SampleWait(&sample, running), "the running frame finished");
    return SampleClose(&sample);
}
