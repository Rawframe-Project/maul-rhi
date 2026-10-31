// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Device loss on a test driver device: the driver's flag loses it at a
// poll, a submission, an acquire or a creation, and the program by
// mrhiDevice_SimulateLoss; the device answers
// everything it owed, in order, with mrhi_errorDeviceLost; its report;
// and what still works afterwards.

#include "device_core.h"
#include "test_container.h"
#include "test_device_setup.h"

#include "maul-rhi/encoder.h"

static bool s_lose;
static mrhiDevice* s_device;
static mrhiBufferId s_buffer;

static void Open(void)
{
    s_lose = false;
    s_adapter.loseDevice = &s_lose;
    s_adapter.lossReason = mrhi_lossHung;
    s_adapter.holdFrames = true;
    s_device = OpenWith(mrhiDefaultDeviceDef(), true);
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = 256;
    def.usage = mrhi_bufferCopySource;
    CHECK(mrhiCreateBuffer(s_device, &def, &s_buffer) == mrhi_success, "a buffer");
}

static void CloseDevice(void)
{
    Close(s_device);
    s_adapter.loseDevice = nullptr;
    s_adapter.holdFrames = false;
}

// Begins a frame of one pass reading the buffer back.
static mrhiPassId BeginReading(mrhiRequestId* readbackOut)
{
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frameDef) == mrhi_success, "begun");
    mrhiResourceId buffer = {0};
    CHECK(mrhiImportBuffer(s_device, s_buffer, &buffer) == mrhi_success, "imported");
    mrhiAccess access = {.resource = buffer, .kind = mrhi_accessCopySource};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.accesses = &access;
    def.accessCount = 1;
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success &&
              mrhiDevice_CompileFrame(s_device) == mrhi_success &&
              mrhiPass_Begin(s_device, pass) == mrhi_success &&
              mrhiPass_ReadBuffer(s_device, pass, buffer, 0, 16, readbackOut) == mrhi_success &&
              mrhiPass_End(s_device, pass) == mrhi_success,
          "a readback recorded");
    return pass;
}

// A compute pipeline left compiling, and its id.
static mrhiRequestId StartPipeline(mrhiComputePipelineId* pipelineOut)
{
    Reset();
    s_sections[BINDINGS].size = 0;
    Assemble();
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_container;
    shaderDef.byteCount = s_size;
    mrhiComputePipelineDef def = mrhiDefaultComputePipelineDef();
    CHECK(mrhiCreateShader(s_device, &shaderDef, &def.shader) == mrhi_success, "a shader");
    def.entry = "cs";
    def.entryLength = 2;
    mrhiRequestId request = {0};
    CHECK(mrhiCreateComputePipeline(s_device, &def, pipelineOut, &request) == mrhi_success,
          "a pipeline compiling");
    CHECK(mrhiDestroyShader(s_device, def.shader) == mrhi_success, "its shader destroyed");
    return request;
}

static bool Next(mrhiDeviceNotificationKind kind, uint32_t request)
{
    mrhiDeviceNotification record;
    return mrhiDevice_TakeNotification(s_device, &record) == mrhi_success && record.kind == kind &&
           record.request.index1 == request &&
           record.request.generation == (request != 0 ? 1u : 0u) &&
           record.outcome == mrhi_errorDeviceLost;
}

// Lost at a poll with a frame running and a pipeline compiling: the
// notice, then the frame, its readback and the pipeline.
static void TestLostAtPoll(void)
{
    Open();
    mrhiDeviceLossReport report;
    CHECK(mrhiDevice_GetLossReport(s_device, &report) == mrhi_errorState, "not lost yet");
    CHECK(mrhiDevice_GetLossReport(s_device, nullptr) == mrhi_errorInvalid &&
              mrhiDevice_GetLossReport(nullptr, &report) == mrhi_errorInvalid,
          "no out or device");
    uint32_t room = mrhiAnswerRoom(s_device);
    CHECK(room == 255, "one record kept for the notice");
    mrhiRequestId readback = {0};
    BeginReading(&readback);
    mrhiRequestId finished = {0};
    CHECK(mrhiDevice_SubmitFrame(s_device, &finished) == mrhi_success &&
              mrhiRequest_WaitFrame(s_device, finished, 1) == mrhi_success,
          "a frame finished");
    mrhiDeviceNotification record;
    CHECK(mrhiDevice_TakeNotification(s_device, &record) == mrhi_success &&
              mrhiDevice_TakeNotification(s_device, &record) == mrhi_success,
          "it and its readback answered");
    size_t size = 0;
    CHECK(mrhiRequest_TakeReadback(s_device, readback, nullptr, 0, &size) == mrhi_success &&
              size == 16,
          "its size");
    uint8_t bytes[16];
    CHECK(mrhiRequest_TakeReadback(s_device, readback, bytes, sizeof(bytes), &size) == mrhi_success,
          "taken");
    BeginReading(&readback);
    mrhiRequestId token = {0};
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_success, "a frame running");
    // A freed slot before the compiling pipeline's reads as pending.
    mrhiComputePipelineId freed;
    mrhiComputePipelineId compiling;
    mrhiRequestId first = StartPipeline(&freed);
    mrhiRequestId pipeline = StartPipeline(&compiling);
    CHECK(freed.index1 < compiling.index1, "the compiling one in the later slot");
    CHECK(mrhiDestroyComputePipeline(s_device, freed) == mrhi_success &&
              mrhiDevice_TakeNotification(s_device, &record) == mrhi_success &&
              record.request.index1 == first.index1 && record.outcome == mrhi_errorStale,
          "the first destroyed while compiling, answered stale");
    s_lose = true;
    CHECK(mrhiDevice_GetState(s_device) == mrhi_deviceReady, "lost at the next poll");
    CHECK(Next(mrhi_deviceLostNotice, 0), "the notice first");
    CHECK(mrhiDevice_GetState(s_device) == mrhi_deviceLost, "lost");
    CHECK(Next(mrhi_deviceFrameDone, token.index1) &&
              Next(mrhi_deviceReadbackReady, readback.index1),
          "the frame, then its readback");
    CHECK(Next(mrhi_devicePipelineReady, pipeline.index1), "the pipeline");
    CHECK(mrhiDevice_TakeNotification(s_device, &record) == mrhi_empty, "nothing else");
    CHECK(mrhiAnswerRoom(s_device) == 256, "the whole queue free, the notice read");
    CHECK(mrhiDestroyComputePipeline(s_device, compiling) == mrhi_success &&
              mrhiDevice_TakeNotification(s_device, &record) == mrhi_empty,
          "the lost pipeline destroyed without another answer");
    CHECK(mrhiDevice_GetLossReport(s_device, &report) == mrhi_success &&
              report.reason == mrhi_lossHung && report.lastSubmitted.index1 == token.index1 &&
              report.lastSubmitted.generation == 1 &&
              report.lastFinished.index1 == finished.index1 &&
              report.lastFinished.generation == 1 && report.faultingFrame.index1 == token.index1 &&
              report.faultingPass == 1 && report.messageLength == 31 &&
              memcmp(report.message, "the test driver lost its device", 31) == 0,
          "the report");
    CHECK(mrhiRequest_TakeReadback(s_device, readback, nullptr, 0, &size) == mrhi_errorDeviceLost,
          "the readback taken lost");
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = 4;
    def.usage = mrhi_bufferVertex;
    mrhiBufferId buffer;
    CHECK(mrhiCreateBuffer(s_device, &def, &buffer) == mrhi_errorDeviceLost, "nothing made");
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frameDef) == mrhi_errorDeviceLost, "no frame begun");
    CHECK(mrhiDestroyBuffer(s_device, s_buffer) == mrhi_success, "still destroyed");
    CHECK(mrhiRequest_WaitFrame(s_device, token, 1) == mrhi_success, "its token answered");
    CHECK(mrhiDevice_GetMisuse(s_device) == 1, "the missing out counted");
    CloseDevice();
}

// Lost at a submission: the frame never runs, only the notice comes.
static void TestLostAtSubmit(void)
{
    Open();
    mrhiRequestId readback = {0};
    BeginReading(&readback);
    s_lose = true;
    mrhiRequestId token = {0};
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_errorDeviceLost, "refused, lost");
    CHECK(mrhiDevice_GetState(s_device) == mrhi_deviceLost && Next(mrhi_deviceLostNotice, 0),
          "the notice");
    mrhiDeviceNotification record;
    CHECK(mrhiDevice_TakeNotification(s_device, &record) == mrhi_empty, "nothing else owed");
    mrhiDeviceLossReport report;
    CHECK(mrhiDevice_GetLossReport(s_device, &report) == mrhi_success &&
              report.lastSubmitted.index1 == 0 && report.faultingFrame.index1 == 0 &&
              report.faultingPass == 0,
          "no frame submitted");
    size_t size = 0;
    CHECK(mrhiRequest_TakeReadback(s_device, readback, nullptr, 0, &size) == mrhi_errorStale,
          "the readback dropped with its frame");
    CloseDevice();
}

// Lost while a frame is open, seen by a creation: the frame recorded
// since is refused and dropped at submission.
static void TestLostWhileOpen(void)
{
    Open();
    mrhiRequestId readback = {0};
    BeginReading(&readback);
    s_lose = true;
    mrhiSamplerDef def = mrhiDefaultSamplerDef();
    mrhiSamplerId sampler;
    CHECK(mrhiCreateSampler(s_device, &def, &sampler) == mrhi_errorDeviceLost,
          "lost at a creation");
    CHECK(mrhiDevice_GetState(s_device) == mrhi_deviceLost, "lost");
    mrhiRequestId token = {0};
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_errorDeviceLost,
          "the open frame refused");
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_errorState, "and dropped already");
    CHECK(Next(mrhi_deviceLostNotice, 0), "the notice");
    CloseDevice();
}

// A configured surface of 64 by 64 texels.
static mrhiSurfaceId MakeSurface(void)
{
    mrhiSurfaceSourceTest source = {
        .chain = {.next = nullptr, .type = mrhi_structSurfaceSourceTest},
        .caps = {.colors = {{.format = mrhi_formatBgra8Unorm}}, .colorCount = 1},
        .presentingAdapters = 1,
    };
    mrhiSurfaceDef surfaceDef = mrhiDefaultSurfaceDef();
    surfaceDef.next = &source.chain;
    mrhiSurfaceId surface = {0};
    CHECK(mrhiCreateSurface(s_instance, &surfaceDef, &surface) == mrhi_success, "a surface");
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    config.surface = surface;
    config.color = (mrhiSurfaceColor){.format = mrhi_formatBgra8Unorm};
    config.width = 64;
    config.height = 64;
    CHECK(mrhiDevice_ConfigureSurface(s_device, &config) == mrhi_success, "configured");
    return surface;
}

// Lost at an acquire, with the image-less answer the device gives; then
// no driver is asked for another surface's image.
static void TestLostAtAcquire(void)
{
    Open();
    mrhiSurfaceId surface = MakeSurface();
    mrhiSurfaceId other = MakeSurface();
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frameDef) == mrhi_success, "begun");
    s_lose = true;
    mrhiResourceId image = {0};
    CHECK(mrhiAcquireSurfaceImage(s_device, surface, &image) == mrhi_errorDeviceLost &&
              image.index1 == 0,
          "no image, lost");
    CHECK(mrhiDevice_GetState(s_device) == mrhi_deviceLost && Next(mrhi_deviceLostNotice, 0),
          "the notice");
    CHECK(mrhiAcquireSurfaceImage(s_device, surface, &image) == mrhi_errorDeviceLost &&
              mrhiAcquireSurfaceImage(s_device, other, &image) == mrhi_errorDeviceLost,
          "lost again, the driver not asked");
    CHECK(mrhiDestroySurface(s_instance, surface) == mrhi_success &&
              mrhiDestroySurface(s_instance, other) == mrhi_success,
          "the surfaces destroyed");
    CloseDevice();
}

// Lost by the program with a frame running and a pipeline compiling:
// at once, the same answers in the same order as a driver's loss, and a
// report that says it was simulated; the driver is told nothing.
static void TestSimulated(void)
{
    CHECK(mrhiDevice_SimulateLoss(nullptr) == mrhi_errorInvalid, "no device");
    mrhiDevice* opening = OpenWith(mrhiDefaultDeviceDef(), false);
    CHECK(mrhiDevice_SimulateLoss(opening) == mrhi_errorState, "not while opening");
    Close(opening);
    Open();
    mrhiRequestId readback = {0};
    BeginReading(&readback);
    mrhiRequestId token = {0};
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_success, "a frame running");
    mrhiComputePipelineId compiling;
    mrhiRequestId pipeline = StartPipeline(&compiling);
    CHECK(mrhiDevice_SimulateLoss(s_device) == mrhi_success &&
              mrhiDevice_GetState(s_device) == mrhi_deviceLost,
          "lost at once");
    CHECK(mrhiDevice_WaitNotification(s_device, 0) == mrhi_success, "a wait finds the notice");
    CHECK(Next(mrhi_deviceLostNotice, 0), "the notice first");
    CHECK(Next(mrhi_deviceFrameDone, token.index1) &&
              Next(mrhi_deviceReadbackReady, readback.index1),
          "the frame, then its readback");
    CHECK(Next(mrhi_devicePipelineReady, pipeline.index1), "the pipeline");
    mrhiDeviceNotification record;
    CHECK(mrhiDevice_TakeNotification(s_device, &record) == mrhi_empty, "nothing else");
    CHECK(mrhiDevice_WaitNotification(s_device, UINT64_MAX) == mrhi_empty,
          "and a wait owed nothing, at once");
    mrhiDeviceLossReport report;
    static const char message[] = "Lost through mrhiDevice_SimulateLoss";
    CHECK(mrhiDevice_GetLossReport(s_device, &report) == mrhi_success &&
              report.reason == mrhi_lossSimulated && report.lastSubmitted.index1 == token.index1 &&
              report.lastFinished.index1 == 0 && report.faultingFrame.index1 == 0 &&
              report.faultingPass == 0 && report.messageLength == sizeof(message) - 1 &&
              memcmp(report.message, message, sizeof(message) - 1) == 0,
          "the report says it was simulated");
    CHECK(mrhiDevice_SimulateLoss(s_device) == mrhi_errorDeviceLost, "not lost twice");
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frameDef) == mrhi_errorDeviceLost, "no frame begun");
    CHECK(mrhiDestroyComputePipeline(s_device, compiling) == mrhi_success &&
              mrhiDestroyBuffer(s_device, s_buffer) == mrhi_success,
          "still destroyed");
    CloseDevice();
}

// A driver whose report names no reason the contract lists: -1,
// INT32_MIN or one past the last. The program reads mrhi_lossUnknown,
// never the driver's value.
static void TestUnknownReason(void)
{
    static const mrhiDeviceLossReason unknown[] = {-1, INT32_MIN, mrhi_lossSimulated + 1};
    for (size_t i = 0; i < sizeof(unknown) / sizeof(unknown[0]); ++i)
    {
        s_lose = false;
        s_adapter.loseDevice = &s_lose;
        s_adapter.lossReason = unknown[i];
        s_device = OpenWith(mrhiDefaultDeviceDef(), true);
        s_lose = true;
        mrhiDeviceLossReport report;
        CHECK(Next(mrhi_deviceLostNotice, 0) &&
                  mrhiDevice_GetLossReport(s_device, &report) == mrhi_success &&
                  report.reason == mrhi_lossUnknown && report.messageLength == 31,
              "an unknown reason passed on as mrhi_lossUnknown");
        Close(s_device);
        s_adapter.loseDevice = nullptr;
    }
}

int main(void)
{
    ResetAdapter();
    TestLostAtPoll();
    TestLostAtSubmit();
    TestLostWhileOpen();
    TestLostAtAcquire();
    TestSimulated();
    TestUnknownReason();
    return s_failures == 0 ? 0 : 1;
}
