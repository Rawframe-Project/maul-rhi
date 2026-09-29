// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The presentation samples' windows (window.h).

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "window.h"

#include <stdio.h>
#include <stdlib.h>

#if defined(__EMSCRIPTEN__)
#include <emscripten/em_js.h>
#elif defined(SAMPLE_XCB)
#include <xcb/xcb.h>
#endif

static bool IsSurfaceRequired(void)
{
    const char* value = getenv("MAUL_RHI_REQUIRE_SURFACE");
    return value != nullptr && value[0] != '\0';
}

[[maybe_unused]] static int Missing(const char* what)
{
    printf("%s: %s\n", IsSurfaceRequired() ? "FAIL" : "skip", what);
    return IsSurfaceRequired() ? 1 : SAMPLE_SKIPPED;
}

static int MakeSurface(Sample* sample, SampleWindow* window, const mrhiChain* source)
{
    mrhiSurfaceDef def = mrhiDefaultSurfaceDef();
    def.next = source;
    if (mrhiCreateSurface(sample->instance, &def, &window->surface) != mrhi_success)
    {
        printf("FAIL: no surface\n");
        return 1;
    }
    return 0;
}

#if defined(__EMSCRIPTEN__)

// clang-format off
EM_JS(void, ResizeCanvas, (int index, uint32_t width, uint32_t height), {
    const canvas = document.querySelector(index === 0 ? '#mrhi-canvas' : '#mrhi-canvas-2');
    canvas.width = width;
    canvas.height = height;
});
// clang-format on

int SampleWindowOpen(Sample* sample, SampleWindow* window, int index, uint32_t width,
                     uint32_t height)
{
    *window = (SampleWindow){.index = index};
    const mrhiSurfaceSourceCanvas source = {
        .chain = {.type = mrhi_structSurfaceSourceCanvas},
        .selector = index == 0 ? "#mrhi-canvas" : "#mrhi-canvas-2",
        .selectorLength = index == 0 ? 12 : 14,
    };
    ResizeCanvas(index, width, height);
    return MakeSurface(sample, window, &source.chain);
}

void SampleWindowResize(SampleWindow* window, uint32_t width, uint32_t height)
{
    ResizeCanvas(window->index, width, height);
}

void SampleWindowClose(Sample* sample, SampleWindow* window)
{
    SampleCheck(sample, mrhiDestroySurface(sample->instance, window->surface) == mrhi_success,
                "the surface destroyed");
}

#elif defined(SAMPLE_XCB)

// Waits until the X server has handled every request sent.
static void Sync(xcb_connection_t* connection)
{
    free(xcb_get_input_focus_reply(connection, xcb_get_input_focus(connection), nullptr));
}

int SampleWindowOpen(Sample* sample, SampleWindow* window, int index, uint32_t width,
                     uint32_t height)
{
    *window = (SampleWindow){.index = index};
    xcb_connection_t* connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(connection) != 0)
    {
        xcb_disconnect(connection);
        return Missing("no X server");
    }
    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    window->connection = connection;
    window->window = xcb_generate_id(connection);
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, window->window, screen->root, 0, 0,
                      (uint16_t)width, (uint16_t)height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual, 0, nullptr);
    xcb_map_window(connection, window->window);
    Sync(connection);
    const mrhiSurfaceSourceXcb source = {
        .chain = {.type = mrhi_structSurfaceSourceXcb},
        .connection = connection,
        .window = window->window,
    };
    return MakeSurface(sample, window, &source.chain);
}

void SampleWindowResize(SampleWindow* window, uint32_t width, uint32_t height)
{
    const uint32_t size[2] = {width, height};
    xcb_configure_window(window->connection, window->window,
                         XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, size);
    Sync(window->connection);
}

void SampleWindowClose(Sample* sample, SampleWindow* window)
{
    SampleCheck(sample, mrhiDestroySurface(sample->instance, window->surface) == mrhi_success,
                "the surface destroyed");
    xcb_destroy_window(window->connection, window->window);
    xcb_disconnect(window->connection);
}

#else

int SampleWindowOpen(Sample* sample, SampleWindow* window, int index, uint32_t width,
                     uint32_t height)
{
    (void)sample;
    (void)width;
    (void)height;
    *window = (SampleWindow){.index = index};
    return Missing("no window system the samples know");
}

void SampleWindowResize(SampleWindow* window, uint32_t width, uint32_t height)
{
    (void)window;
    (void)width;
    (void)height;
}

void SampleWindowClose(Sample* sample, SampleWindow* window)
{
    (void)sample;
    (void)window;
}

#endif
