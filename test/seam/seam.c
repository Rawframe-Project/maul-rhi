// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The seam with Maul Window (mrhi-0028): a window made with Maul
// Window, a surface made from its native handles, frames presented;
// the window resized, the surface configured again at the new size and
// presented to; the surface ended before the window. Maul Window is
// fetched for this check alone (test/seam/CMakeLists.txt). Exits 77
// without a window system or an adapter unless MAUL_RHI_REQUIRE_SURFACE
// is set.

#include "harness.h"

#include "maul-rhi/surface.h"
#include "maul-window/event.h"
#include "maul-window/native.h"
#include "maul-window/window.h"

#include <stdio.h>
#include <stdlib.h>

#define FRAMES_TO_GIVE 600

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
    phaseDone,
} Phase;

typedef struct Program
{
    Sample* sample;
    Phase phase;
    mwinWindowId window;
    bool shown;
    // The surface, the bundle's generation it was made from, its color,
    // and the pixel size it is configured at.
    mrhiSurfaceId surface;
    bool hasSurface;
    uint32_t generation;
    mrhiSurfaceColor color;
    mwinPixelSize configured;
    // The size first configured, and the frames presented before and
    // after the resize.
    mwinPixelSize initial;
    int presented;
    int resizedPresents;
    int frames;
    // Why the check could not run: a window system or surface missing.
    const char* missing;
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
    return SampleCheck(program->sample,
                       mrhiConfigureSurface(program->sample->device, &config) == mrhi_success,
                       "configured at the window's pixel size");
}

// A frame cleared and presented: whether it was, after configuring at
// the window's pixel size when it differs or the surface says so.
static bool Present(Program* program, mwinContext* context)
{
    Sample* sample = program->sample;
    mwinPixelSize size = PixelSize(context, program->window);
    if (size.width == 0 || size.height == 0)
    {
        return false;
    }
    if ((size.width != program->configured.width || size.height != program->configured.height) &&
        !Configure(program, size))
    {
        return false;
    }
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId image = {0};
    SampleCheck(sample, mrhiBeginFrame(sample->device, &frame) == mrhi_success, "a frame");
    mrhiResult acquired = mrhiAcquireSurfaceImage(sample->device, program->surface, &image);
    if (acquired != mrhi_success)
    {
        SampleCheck(sample, mrhiDropFrame(sample->device) == mrhi_success, "the frame dropped");
        // Out of date or suboptimal: configure again at the next frame.
        program->configured = (mwinPixelSize){0};
        return false;
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
    bool submitted = SampleCheck(sample,
                                 mrhiAddPass(sample->device, &def, &pass) == mrhi_success &&
                                     mrhiCompileFrame(sample->device) == mrhi_success &&
                                     mrhiBeginPass(sample->device, pass) == mrhi_success &&
                                     mrhiEndPass(sample->device, pass) == mrhi_success &&
                                     mrhiSubmitFrame(sample->device, &token) == mrhi_success,
                                 "the image cleared and submitted");
    return submitted && SampleWait(sample, token);
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

static mwinFrameResult Frame(mwinContext* context, void* user)
{
    Program* program = user;
    Sample* sample = program->sample;
    Collect(program, context);
    if (++program->frames > FRAMES_TO_GIVE)
    {
        SampleCheck(sample, false, "every phase within its frames");
        return mwin_frameStop;
    }
    switch (program->phase)
    {
    case phaseShown:
        if (program->shown && EnsureSurface(program, context))
        {
            program->phase = phasePresent;
        }
        else if (program->missing != nullptr)
        {
            return mwin_frameStop;
        }
        break;
    case phasePresent:
        program->presented += EnsureSurface(program, context) && Present(program, context);
        if (program->presented == 3)
        {
            SampleCheck(sample,
                        mwinRequestSize(context, program->window, (mwinSize){200.0f, 150.0f},
                                        nullptr) == mwin_success,
                        "a resize asked for");
            program->phase = phaseResized;
        }
        break;
    case phaseResized:
        // Frames present on until three have at the new size.
        if (EnsureSurface(program, context) && Present(program, context) &&
            (program->configured.width != program->initial.width ||
             program->configured.height != program->initial.height) &&
            ++program->resizedPresents == 3)
        {
            program->phase = phaseDone;
        }
        break;
    default:
        SampleCheck(sample, mrhiDestroySurface(sample->instance, program->surface) == mrhi_success,
                    "the surface destroyed before the window");
        program->hasSurface = false;
        SampleCheck(sample, mwinDestroyWindow(context, program->window) == mwin_success,
                    "the window destroyed");
        return mwin_frameStop;
    }
    return mwin_frameContinue;
}

int main(void)
{
    Sample sample;
    int opened = SampleOpen(&sample, nullptr);
    if (opened != 0)
    {
        return opened;
    }
    Program program = {.sample = &sample};
    mwinAppDef def = mwinDefaultAppDef();
    def.init = Init;
    def.frame = Frame;
    def.user = &program;
    mwinResult ran = mwinRun(&def);
    const char* required = getenv("MAUL_RHI_REQUIRE_SURFACE");
    bool require = required != nullptr && required[0] != '\0';
    if (ran == mwin_errorUnsupported || program.missing != nullptr)
    {
        printf("%s: %s\n", require ? "FAIL" : "skip",
               program.missing != nullptr ? program.missing : "no window system");
        SampleClose(&sample);
        return require ? 1 : SAMPLE_SKIPPED;
    }
    printf("presented %d frames at %ux%u, then %d at %ux%u\n", program.presented,
           program.initial.width, program.initial.height, program.resizedPresents,
           program.configured.width, program.configured.height);
    SampleCheck(&sample, ran == mwin_success, "the window program ran");
    SampleCheck(&sample, program.phase == phaseDone, "every phase ran");
    if (program.hasSurface)
    {
        (void)mrhiDestroySurface(sample.instance, program.surface);
    }
    return SampleClose(&sample);
}
