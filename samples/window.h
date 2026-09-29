// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The windows the presentation samples show: an X window through XCB
// where the build has the XCB client library and an X server runs, or
// on the web a canvas of the runner's page (#mrhi-canvas,
// #mrhi-canvas-2). No window library is linked; a program takes its
// window's handles from its own. Elsewhere, or without a display, the
// samples are skipped unless MAUL_RHI_REQUIRE_SURFACE is set.

#ifndef MAUL_RHI_SAMPLES_WINDOW_H
#define MAUL_RHI_SAMPLES_WINDOW_H

#include "harness.h"

#include "maul-rhi/surface.h"

typedef struct SampleWindow
{
    mrhiSurfaceId surface;
    // The X connection and window, where there are; the CAMetalLayer on
    // the Metal driver.
    void* connection;
    uint32_t window;
    // Which window: 0 or 1.
    int index;
} SampleWindow;

// Opens window 0 or 1 at a size and makes its surface: 0,
// SAMPLE_SKIPPED when there is no window system and none is required,
// or 1 after printing why.
int SampleWindowOpen(Sample* sample, SampleWindow* window, int index, uint32_t width,
                     uint32_t height);

// Resizes the window as its user or page would, and waits until the
// window system has.
void SampleWindowResize(SampleWindow* window, uint32_t width, uint32_t height);

// Destroys the surface and the window.
void SampleWindowClose(Sample* sample, SampleWindow* window);

#endif // MAUL_RHI_SAMPLES_WINDOW_H
