// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The seam with Maul Window (mrhi-0028): a window made with Maul
// Window, a surface made from its native handles, frames presented;
// the window resized, the surface configured again at the new size and
// presented to; the surface ended before the window. The frames never
// wait for the GPU: each frame takes the answers that came and submits
// a new frame when the device has room, as a program on the browser's
// frames must. Maul Window is fetched for this check alone
// (test/seam/CMakeLists.txt). Exits 77 without a window system or an
// adapter unless MAUL_RHI_REQUIRE_SURFACE is set.

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "harness.h"

#include "maul-rhi/surface.h"
#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

// How long the check may take, and the frames presented at each size
// before it goes on. A window program's frames come as fast as the
// platform gives them (Win32 does not wait for the display), so the
// check is bounded by time, not by frames.
#define DEADLINE_SECONDS 10
#define PRESENTS         3

// One surface source of each kind, the one a bundle fills.
typedef union SeamSource
{
    mrhiChain chain;
    mrhiSurfaceSourceWin32 win32;
    mrhiSurfaceSourceWayland wayland;
    mrhiSurfaceSourceXcb xcb;
    mrhiSurfaceSourceAndroid android;
    mrhiSurfaceSourceMetalLayer metal;
    mrhiSurfaceSourceCanvas canvas;
} SeamSource;

// A Maul Window bundle as the Maul RHI surface source of its platform,
// the copy a program makes (both guides show it): the chain to put on a
// surface def, or NULL for a platform without one.
static const mrhiChain* SourceFrom(const mwinNativeHandles* handles, SeamSource* source)
{
    switch (handles->platform)
    {
    case mwin_platformWin32:
        source->win32 = (mrhiSurfaceSourceWin32){
            .chain = {.type = mrhi_structSurfaceSourceWin32},
            .hinstance = handles->handles.win32.hinstance,
            .hwnd = handles->handles.win32.hwnd,
        };
        return &source->chain;
    case mwin_platformWayland:
        source->wayland = (mrhiSurfaceSourceWayland){
            .chain = {.type = mrhi_structSurfaceSourceWayland},
            .display = handles->handles.wayland.display,
            .surface = handles->handles.wayland.surface,
        };
        return &source->chain;
    case mwin_platformX11:
        source->xcb = (mrhiSurfaceSourceXcb){
            .chain = {.type = mrhi_structSurfaceSourceXcb},
            .connection = handles->handles.x11.connection,
            .window = handles->handles.x11.window,
        };
        return &source->chain;
    case mwin_platformAndroid:
        source->android = (mrhiSurfaceSourceAndroid){
            .chain = {.type = mrhi_structSurfaceSourceAndroid},
            .window = handles->handles.android.window,
        };
        return &source->chain;
    case mwin_platformMacOS:
    case mwin_platformIOS:
        source->metal = (mrhiSurfaceSourceMetalLayer){
            .chain = {.type = mrhi_structSurfaceSourceMetalLayer},
            .layer = handles->handles.apple.layer,
        };
        return &source->chain;
    case mwin_platformWeb:
        source->canvas = (mrhiSurfaceSourceCanvas){
            .chain = {.type = mrhi_structSurfaceSourceCanvas},
            .selector = handles->handles.web.selector,
            .selectorLength = handles->handles.web.selectorLength,
        };
        return &source->chain;
    default:
        return nullptr;
    }
}

typedef enum Phase
{
    phaseShown,
    phasePresent,
    phaseResized,
    phaseDrain,
    phaseDone,
} Phase;

typedef struct Program
{
    Sample* sample;
    Phase phase;
    mwinWindowId window;
    bool shown;
    // The surface, the bundle's generation it was made from, its color,
    // the pixel size it is configured at, and the size first configured.
    mrhiSurfaceId surface;
    bool hasSurface;
    uint32_t generation;
    mrhiSurfaceColor color;
    mwinPixelSize configured;
    mwinPixelSize initial;
    // Frames submitted and finished, and the frames submitted before the
    // first configuration at another size than the first.
    int submitted;
    int finished;
    int resizedFrom;
    time_t deadline;
    // Why the check could not run: a window system or surface missing.
    const char* missing;
    int status;
} Program;

static mwinPixelSize PixelSize(mwinContext* context, mwinWindowId window)
{
    mwinWindowState state = {0};
    return mwinGetWindowState(context, window, &state) == mwin_success ? state.pixelSize
                                                                       : (mwinPixelSize){0};
}

// Makes the surface from the window's bundle, or makes it again when
// the bundle's generation moved on: whether there is one.
static bool EnsureSurface(Program* program, mwinContext* context)
{
    Sample* sample = program->sample;
    mwinNativeHandles handles;
    if (mwinGetNativeHandles(context, program->window, &handles) != mwin_success)
    {
        return false;
    }
    if (program->hasSurface && handles.surfaceGeneration == program->generation)
    {
        return true;
    }
    if (program->hasSurface)
    {
        SampleCheck(sample, mrhiDestroySurface(sample->instance, program->surface) == mrhi_success,
                    "the last generation's surface destroyed");
        program->hasSurface = false;
    }
    SeamSource source;
    mrhiSurfaceDef def = mrhiDefaultSurfaceDef();
    def.next = SourceFrom(&handles, &source);
    if (def.next == nullptr ||
        mrhiCreateSurface(sample->instance, &def, &program->surface) != mrhi_success)
    {
        program->missing = "no surface from the window's handles";
        return false;
    }
    program->hasSurface = true;
    program->generation = handles.surfaceGeneration;
    program->configured = (mwinPixelSize){0};
    mrhiSurfaceCaps caps;
    return SampleCheck(
        sample,
        mrhiGetSurfaceCaps(sample->instance, program->surface, sample->adapter, &caps) ==
                mrhi_success &&
            mrhiSuggestSurfaceColor(&caps, &caps.colors[0], &program->color) == mrhi_success,
        "the surface's preferred color");
}

static bool Configure(Program* program, mwinPixelSize size)
{
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    config.surface = program->surface;
    config.color = program->color;
    config.width = size.width;
    config.height = size.height;
    program->configured = size;
    program->initial = program->initial.width == 0 ? size : program->initial;
    if (program->resizedFrom < 0 &&
        (size.width != program->initial.width || size.height != program->initial.height))
    {
        program->resizedFrom = program->submitted;
    }
    return SampleCheck(program->sample,
                       mrhiConfigureSurface(program->sample->device, &config) == mrhi_success,
                       "configured at the window's pixel size");
}

// Takes the answers that came: each frame that finished, a success.
static void TakeAnswers(Program* program)
{
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(program->sample->device, &record) == mrhi_success)
    {
        program->finished +=
            SampleCheck(program->sample, record.outcome == mrhi_success, "a frame finished");
    }
}

// Submits a frame that clears the window's image, after configuring at
// the window's pixel size when it differs or the surface said so; none
// while the device's frames are all running or the window has no image.
static void Present(Program* program, mwinContext* context)
{
    Sample* sample = program->sample;
    mwinPixelSize size = PixelSize(context, program->window);
    if (size.width == 0 || size.height == 0 ||
        ((size.width != program->configured.width || size.height != program->configured.height) &&
         !Configure(program, size)))
    {
        return;
    }
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResult begun = mrhiBeginFrame(sample->device, &frame);
    if (begun == mrhi_errorCapacity)
    {
        return;
    }
    SampleCheck(sample, begun == mrhi_success, "a frame");
    mrhiResourceId image = {0};
    mrhiResult acquired = mrhiAcquireSurfaceImage(sample->device, program->surface, &image);
    if (acquired != mrhi_success)
    {
        SampleCheck(sample, mrhiDropFrame(sample->device) == mrhi_success, "the frame dropped");
        // Out of date or suboptimal: configure again at the next frame.
        program->configured = (mwinPixelSize){0};
        return;
    }
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){
        .resource = image,
        .load = mrhi_loadClear,
        .store = mrhi_storeKeep,
        .clear = {0.2f, 0.4f, 0.8f, 1.0f},
    };
    def.colorTargetCount = 1;
    mrhiPassId pass = {0};
    mrhiRequestId token = {0};
    program->submitted += SampleCheck(sample,
                                      mrhiAddPass(sample->device, &def, &pass) == mrhi_success &&
                                          mrhiCompileFrame(sample->device) == mrhi_success &&
                                          mrhiBeginPass(sample->device, pass) == mrhi_success &&
                                          mrhiEndPass(sample->device, pass) == mrhi_success &&
                                          mrhiSubmitFrame(sample->device, &token) == mrhi_success,
                                      "the image cleared and submitted");
}

static void Collect(Program* program, mwinContext* context)
{
    mwinEvent event;
    while (mwinNextEvent(context, &event) == mwin_success)
    {
        program->shown = program->shown || event.type == mwin_eventShown;
    }
}

static mwinResult Init(mwinContext* context, void* user)
{
    Program* program = user;
    mwinWindowDef def = mwinDefaultWindowDef();
    def.size = (mwinSize){160.0f, 120.0f};
    return mwinCreateWindow(context, &def, &program->window, nullptr);
}

// Moves the check on by one of the window program's frames.
static void Step(Program* program, mwinContext* context)
{
    Sample* sample = program->sample;
    switch (program->phase)
    {
    case phaseShown:
        program->phase =
            program->shown && EnsureSurface(program, context) ? phasePresent : phaseShown;
        break;
    case phasePresent:
        if (EnsureSurface(program, context))
        {
            Present(program, context);
        }
        if (program->finished >= PRESENTS)
        {
            SampleCheck(sample,
                        mwinRequestSize(context, program->window, (mwinSize){200.0f, 150.0f},
                                        nullptr) == mwin_success,
                        "a resize asked for");
            program->phase = phaseResized;
        }
        break;
    case phaseResized:
        if (EnsureSurface(program, context))
        {
            Present(program, context);
        }
        // Frames finish in order: those past the mark were at the new size.
        if (program->resizedFrom >= 0 && program->finished - program->resizedFrom >= PRESENTS)
        {
            program->phase = phaseDrain;
        }
        break;
    case phaseDrain:
        program->phase = program->finished == program->submitted ? phaseDone : phaseDrain;
        break;
    default:
        break;
    }
}

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Collect(program, context);
    TakeAnswers(program);
    if (time(nullptr) > program->deadline)
    {
        SampleCheck(program->sample, false, "every phase in time");
        return mwin_frameStop;
    }
    Step(program, context);
    if (program->missing != nullptr)
    {
        return mwin_frameStop;
    }
    if (program->phase != phaseDone)
    {
        return mwin_frameContinue;
    }
    SampleCheck(program->sample,
                mrhiDestroySurface(program->sample->instance, program->surface) == mrhi_success,
                "the surface destroyed before the window");
    program->hasSurface = false;
    SampleCheck(program->sample, mwinDestroyWindow(context, program->window) == mwin_success,
                "the window destroyed");
    return mwin_frameStop;
}

// Ends the check once the window program stops: its status, and on the
// web, where mwinRun returned at once, the page's exit.
static void Quit(mwinContext* context, mwinResult status, void* user)
{
    (void)context;
    Program* program = user;
    Sample* sample = program->sample;
    const char* required = getenv("MAUL_RHI_REQUIRE_SURFACE");
    bool require = required != nullptr && required[0] != '\0';
    if (program->hasSurface)
    {
        (void)mrhiDestroySurface(sample->instance, program->surface);
    }
    if (program->missing != nullptr)
    {
        printf("%s: %s\n", require ? "FAIL" : "skip", program->missing);
        SampleClose(sample);
        program->status = require ? 1 : SAMPLE_SKIPPED;
    }
    else
    {
        printf("presented %d frames, the first at %ux%u, the last %d at %ux%u\n", program->finished,
               program->initial.width, program->initial.height,
               program->finished - program->resizedFrom, program->configured.width,
               program->configured.height);
        SampleCheck(sample, status == mwin_success, "the window program ran");
        SampleCheck(sample, program->phase == phaseDone, "every phase ran");
        program->status = SampleClose(sample);
    }
#ifdef __EMSCRIPTEN__
    exit(program->status);
#endif
}

int main(void)
{
    static Sample sample;
    int opened = SampleOpen(&sample, nullptr);
    if (opened != 0)
    {
        return opened;
    }
    static Program program;
    program = (Program){.sample = &sample,
                        .resizedFrom = -1,
                        .deadline = time(nullptr) + DEADLINE_SECONDS,
                        .status = -1};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.quit = Quit;
    def.user = &program;
    mwinResult ran = mwinRun(&def);
    if (program.status >= 0)
    {
        return program.status;
    }
    // The window program never started: no window system here.
    const char* required = getenv("MAUL_RHI_REQUIRE_SURFACE");
    bool require = required != nullptr && required[0] != '\0';
    printf("%s: the window program did not start (%d)\n", require ? "FAIL" : "skip", (int)ran);
    SampleClose(&sample);
    return require ? 1 : SAMPLE_SKIPPED;
}
