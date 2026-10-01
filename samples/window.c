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
#include <threads.h>
#include <xcb/xcb.h>
#elif defined(SAMPLE_WIN32)
#include <windows.h>
#elif defined(SAMPLE_METAL)
#include "metal_layer.h"
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

[[maybe_unused]] static int MakeSurface(Sample* sample, SampleWindow* window,
                                        const mrhiChain* source)
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

// Connects to the X server DISPLAY names, trying again for a second: a
// server busy with many clients at once may refuse a connection for a
// moment, as Xvfb under a parallel test run does.
static xcb_connection_t* ConnectX(void)
{
    xcb_connection_t* connection = xcb_connect(nullptr, nullptr);
    for (int tries = 1;
         tries < 20 && xcb_connection_has_error(connection) != 0 && getenv("DISPLAY") != nullptr;
         ++tries)
    {
        xcb_disconnect(connection);
        (void)thrd_sleep(&(struct timespec){.tv_nsec = 50000000}, nullptr);
        connection = xcb_connect(nullptr, nullptr);
    }
    return connection;
}

int SampleWindowOpen(Sample* sample, SampleWindow* window, int index, uint32_t width,
                     uint32_t height)
{
    *window = (SampleWindow){.index = index};
    xcb_connection_t* connection = ConnectX();
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

#elif defined(SAMPLE_WIN32)

// The samples' window class, registered with the first window, and the
// windows' style: popups, which Windows gives no minimum size, so that
// a small client area is the size asked for.
#define WINDOW_CLASS L"mrhiSample"
#define WINDOW_STYLE WS_POPUP

// Handles the messages the window system has sent, so that it treats
// the window as live.
static void Pump(void)
{
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        (void)TranslateMessage(&message);
        (void)DispatchMessageW(&message);
    }
}

// The window's outer size for a client area of width by height.
static SIZE OuterSize(uint32_t width, uint32_t height)
{
    RECT rect = {0, 0, (LONG)width, (LONG)height};
    (void)AdjustWindowRect(&rect, WINDOW_STYLE, FALSE);
    return (SIZE){rect.right - rect.left, rect.bottom - rect.top};
}

int SampleWindowOpen(Sample* sample, SampleWindow* window, int index, uint32_t width,
                     uint32_t height)
{
    *window = (SampleWindow){.index = index};
    HINSTANCE module = GetModuleHandleW(nullptr);
    const WNDCLASSW windowClass = {
        .lpfnWndProc = DefWindowProcW,
        .hInstance = module,
        .lpszClassName = WINDOW_CLASS,
    };
    // A second window finds the class registered.
    (void)RegisterClassW(&windowClass);
    SIZE size = OuterSize(width, height);
    HWND made = CreateWindowExW(0, WINDOW_CLASS, L"sample", WINDOW_STYLE, 0, 0, size.cx, size.cy,
                                nullptr, nullptr, module, nullptr);
    if (made == nullptr)
    {
        return Missing("no Win32 window");
    }
    (void)ShowWindow(made, SW_SHOWNOACTIVATE);
    Pump();
    window->connection = made;
    const mrhiSurfaceSourceWin32 source = {
        .chain = {.type = mrhi_structSurfaceSourceWin32},
        .hinstance = module,
        .hwnd = made,
    };
    return MakeSurface(sample, window, &source.chain);
}

void SampleWindowResize(SampleWindow* window, uint32_t width, uint32_t height)
{
    SIZE size = OuterSize(width, height);
    (void)SetWindowPos((HWND)window->connection, nullptr, 0, 0, size.cx, size.cy,
                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    Pump();
}

void SampleWindowClose(Sample* sample, SampleWindow* window)
{
    SampleCheck(sample, mrhiDestroySurface(sample->instance, window->surface) == mrhi_success,
                "the surface destroyed");
    (void)DestroyWindow((HWND)window->connection);
    Pump();
}

#elif defined(SAMPLE_METAL)

int SampleWindowOpen(Sample* sample, SampleWindow* window, int index, uint32_t width,
                     uint32_t height)
{
    *window = (SampleWindow){.index = index};
    window->connection = mrhiTestNewMetalLayer(width, height);
    if (window->connection == nullptr)
    {
        return Missing("no CAMetalLayer");
    }
    const mrhiSurfaceSourceMetalLayer source = {
        .chain = {.type = mrhi_structSurfaceSourceMetalLayer},
        .layer = window->connection,
    };
    return MakeSurface(sample, window, &source.chain);
}

void SampleWindowResize(SampleWindow* window, uint32_t width, uint32_t height)
{
    mrhiTestResizeMetalLayer(window->connection, width, height);
}

void SampleWindowClose(Sample* sample, SampleWindow* window)
{
    SampleCheck(sample, mrhiDestroySurface(sample->instance, window->surface) == mrhi_success,
                "the surface destroyed");
    mrhiTestReleaseMetalLayer(window->connection);
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
