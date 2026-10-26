// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// New buffers on a test driver device: the first frame that imports one
// clears it whole in a pass of the library's own, ahead of the
// program's passes, and no later frame does. The clear comes from no
// budget of the program's, a dropped frame clears nothing, and a buffer
// that takes the slot of one destroyed while its frame was open is
// cleared in its own first frame. The library's pass takes no program
// call.

#include "test_device_setup.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/resources.h"

static mrhiTestFrameLog s_log;

static mrhiDevice* Open(mrhiDeviceDef def)
{
    s_adapter.frameLog = &s_log;
    return OpenWith(def, true);
}

static mrhiBufferId MakeBuffer(mrhiDevice* device)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = 256;
    def.usage = mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    mrhiBufferId buffer = {0};
    CHECK(mrhiCreateBuffer(device, &def, &buffer) == mrhi_success, "a buffer");
    return buffer;
}

static mrhiResourceId Begin(mrhiDevice* device, mrhiBufferId buffer)
{
    mrhiFrameDef def = mrhiDefaultFrameDef();
    mrhiResourceId resource = {0};
    CHECK(mrhiBeginFrame(device, &def) == mrhi_success &&
              mrhiImportBuffer(device, buffer, &resource) == mrhi_success,
          "a frame with the buffer");
    return resource;
}

static mrhiPassId Read(mrhiDevice* device, mrhiResourceId resource)
{
    mrhiAccess access = {.resource = resource, .kind = mrhi_accessCopySource};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.neverCull = true;
    def.accesses = &access;
    def.accessCount = 1;
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(device, &def, &pass) == mrhi_success, "a pass reading it");
    return pass;
}

// Submits the open frame, the log counting it alone.
static void Submit(mrhiDevice* device)
{
    s_log = (mrhiTestFrameLog){0};
    mrhiRequestId token = {0};
    CHECK(mrhiSubmitFrame(device, &token) == mrhi_success, "submitted");
}

// The first frame that imports a buffer runs one more pass, with one
// clear; the next frame runs the program's pass alone. The library's
// pass, the frame's first, takes no call of the program's, and the
// buffer's plan shows it.
static void TestFirstFrame(void)
{
    mrhiDevice* device = Open(mrhiDefaultDeviceDef());
    mrhiBufferId buffer = MakeBuffer(device);
    mrhiResourceId resource = Begin(device, buffer);
    mrhiPassId pass = Read(device, resource);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    mrhiPassId library = {1, pass.generation};
    bool kept = false;
    mrhiPassPlan plan;
    CHECK(pass.index1 != library.index1 && mrhiBeginPass(device, library) == mrhi_errorStale &&
              mrhiIsPassKept(device, library, &kept) == mrhi_errorStale &&
              mrhiGetPassPlan(device, library, &plan) == mrhi_errorStale,
          "the library's pass refused to the program");
    mrhiResourcePlan used;
    CHECK(mrhiGetResourcePlan(device, resource, &used) == mrhi_success &&
              used.firstPass.index1 == library.index1 && used.lastPass.index1 == pass.index1,
          "the buffer first used by the library's pass");
    Submit(device);
    CHECK(s_log.passes == 2 && s_log.commands == 1 && s_log.accesses == 2,
          "the first frame clears the buffer before its pass");
    resource = Begin(device, buffer);
    (void)Read(device, resource);
    Submit(device);
    CHECK(s_log.passes == 1 && s_log.commands == 0 && s_log.accesses == 1,
          "the next frame does not");
    Close(device);
}

// A frame that imports a new buffer and is dropped clears nothing, so
// the next frame does; a frame that imports one and uses it nowhere
// still clears it, as the buffer outlives the frame.
static void TestDroppedAndUnused(void)
{
    mrhiDevice* device = Open(mrhiDefaultDeviceDef());
    mrhiBufferId buffer = MakeBuffer(device);
    (void)Begin(device, buffer);
    CHECK(mrhiDropFrame(device) == mrhi_success, "dropped");
    (void)Begin(device, buffer);
    Submit(device);
    CHECK(s_log.passes == 1 && s_log.commands == 1, "cleared though nothing used it");
    (void)Begin(device, buffer);
    Submit(device);
    CHECK(s_log.passes == 0, "and not again");
    Close(device);
}

// A buffer destroyed while its frame is open gives its slot to a new
// one; the frame clears the buffer it imported, and the new buffer is
// still cleared in its own first frame.
static void TestReusedSlot(void)
{
    mrhiDevice* device = Open(mrhiDefaultDeviceDef());
    mrhiBufferId old = MakeBuffer(device);
    mrhiResourceId resource = Begin(device, old);
    (void)Read(device, resource);
    CHECK(mrhiDestroyBuffer(device, old) == mrhi_success, "destroyed in its frame");
    mrhiBufferId next = MakeBuffer(device);
    CHECK(next.index1 == old.index1, "its slot taken again");
    Submit(device);
    CHECK(s_log.passes == 2 && s_log.commands == 1, "the frame clears the buffer it imported");
    resource = Begin(device, next);
    (void)Read(device, resource);
    Submit(device);
    CHECK(s_log.passes == 2 && s_log.commands == 1, "the new buffer is cleared in its first frame");
    Close(device);
}

// Records up to `most` clears into the frame's one pass: how many fit.
// A pass that runs out of room cannot be submitted, so a frame filled to
// the end is dropped.
static uint32_t Fill(mrhiDevice* device, mrhiResourceId resource, uint32_t most)
{
    mrhiAccess access = {.resource = resource, .kind = mrhi_accessCopyDestination};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = &access;
    def.accessCount = 1;
    mrhiPassId pass = {0};
    mrhiPassId more = {0};
    CHECK(mrhiAddPass(device, &def, &pass) == mrhi_success, "the program's one pass");
    CHECK(mrhiAddPass(device, &def, &more) == mrhi_errorCapacity, "and no other");
    CHECK(mrhiCompileFrame(device) == mrhi_success && mrhiBeginPass(device, pass) == mrhi_success,
          "begun");
    uint32_t fitted = 0;
    while (fitted < most && mrhiClearBuffer(device, pass, resource, 0, 4) == mrhi_success)
    {
        ++fitted;
    }
    CHECK(mrhiEndPass(device, pass) == mrhi_success, "ended");
    return fitted;
}

// A device whose frames take one pass, the two accesses AddPass holds
// for it and 4096 bytes of commands gives the program as much of each in
// a frame that clears a new buffer as in one that does not.
static void TestBudgets(void)
{
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.deviceLimits.framePasses = 1;
    deviceDef.deviceLimits.frameAccesses = 2;
    deviceDef.deviceLimits.frameCommandBytes = 4096;
    mrhiDevice* device = Open(deviceDef);
    mrhiBufferId buffer = MakeBuffer(device);
    uint32_t first = Fill(device, Begin(device, buffer), UINT32_MAX);
    CHECK(first > 0 && mrhiDropFrame(device) == mrhi_success, "room for clears beside the zeroing");
    CHECK(Fill(device, Begin(device, buffer), first) == first, "all of them again");
    Submit(device);
    CHECK(s_log.passes == 2 && s_log.commands == first + 1, "submitted beside the library's clear");
    uint32_t later = Fill(device, Begin(device, buffer), UINT32_MAX);
    CHECK(later == first && mrhiDropFrame(device) == mrhi_success,
          "as much room in a frame that clears nothing of its own");
    Close(device);
}

int main(void)
{
    ResetAdapter();
    TestFirstFrame();
    TestDroppedAndUnused();
    TestReusedSlot();
    TestBudgets();
    return s_failures == 0 ? 0 : 1;
}
