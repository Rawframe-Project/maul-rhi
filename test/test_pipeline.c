// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Compute pipelines on a test driver device: defs checked against the
// shader's reflection, constants against their types, creation answered
// in the device's queue with room reserved, and pipelines that outlive
// their shader.

#include "test_container.h"
#include "test_device_setup.h"

#include "maul-rhi/pipeline.h"

#include <float.h>
#include <math.h>

static mrhiDevice* Open(uint32_t pipelines, uint32_t notifications, bool ready)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.deviceLimits.pipelines = pipelines;
    // The answers asked for, and the record kept for the loss notice.
    def.deviceLimits.notifications = notifications + 1;
    return OpenWith(def, ready);
}

// The default container, with a boolean (id 8), a 32-bit integer (id 9)
// and an unsigned one without a default (id 10) beside its float (id 7).
static void Constants(void)
{
    Reset();
    uint8_t* flag = Record(CONSTANTS, 1, 16);
    Put32(flag, 8);
    flag[4] = mrhi_constantBool;
    uint8_t* integer = Record(CONSTANTS, 2, 16);
    Put32(integer, 9);
    integer[4] = mrhi_constantInt32;
    uint8_t* required = Record(CONSTANTS, 3, 16);
    Put32(required, 10);
    required[4] = mrhi_constantUint32;
    required[12] = 1;
}

// A shader of the sections on the device.
static mrhiShaderId Shader(mrhiDevice* device)
{
    Assemble();
    mrhiShaderDef def = mrhiDefaultShaderDef();
    def.bytes = s_container;
    def.byteCount = s_size;
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_success, "the shader");
    return shader;
}

static mrhiComputePipelineDef Def(mrhiShaderId shader, const char* entry)
{
    mrhiComputePipelineDef def = mrhiDefaultComputePipelineDef();
    def.shader = shader;
    def.entry = entry;
    def.entryLength = entry == nullptr ? 0 : strlen(entry);
    return def;
}

// The next record in the device's queue, which must be there.
static mrhiDeviceNotification Next(mrhiDevice* device)
{
    mrhiDeviceNotification record = {0};
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_success, "a record");
    return record;
}

static void TestCreate(void)
{
    mrhiDevice* device = Open(4, 16, true);
    Reset();
    mrhiShaderId shader = Shader(device);
    mrhiComputePipelineDef def = Def(shader, "cs");
    def.label = "blur";
    def.labelLength = 4;
    mrhiComputePipelineId pipeline = {0};
    mrhiRequestId request = {0};
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_success &&
              pipeline.index1 != 0 && request.index1 != 0,
          "a pipeline, pending");
    mrhiDeviceNotification record = Next(device);
    CHECK(record.kind == mrhi_devicePipelineReady && record.request.index1 == request.index1 &&
              record.request.generation == request.generation && record.outcome == mrhi_success,
          "answered ready");
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_empty, "once");
    mrhiComputePipelineId second = {0};
    mrhiRequestId next = {0};
    CHECK(mrhiCreateComputePipeline(device, &def, &second, &next) == mrhi_success &&
              next.index1 == request.index1 + 1,
          "a second request after the first");
    CHECK(mrhiDestroyShader(device, shader) == mrhi_success, "the shader goes first");
    CHECK(Next(device).outcome == mrhi_success, "the second is ready without its shader");
    CHECK(mrhiDestroyComputePipeline(device, pipeline) == mrhi_success, "destroyed");
    CHECK(mrhiDestroyComputePipeline(device, pipeline) == mrhi_errorStale, "its id has ended");
    CHECK(mrhiDestroyComputePipeline(nullptr, second) == mrhi_errorInvalid, "no device");
    CHECK(mrhiGetDeviceMisuse(device) == 0, "no misuse");
    // The second is still live: the device destroys it, and the
    // reflection with it.
    Close(device);
}

static void TestRequestsShared(void)
{
    mrhiDevice* device = Open(4, 16, true);
    Reset();
    mrhiShaderId shader = Shader(device);
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    mrhiRequestId token = {0};
    CHECK(mrhiBeginFrame(device, &frameDef) == mrhi_success &&
              mrhiSubmitFrame(device, &token) == mrhi_success,
          "a frame");
    mrhiComputePipelineDef def = Def(shader, "cs");
    mrhiComputePipelineId pipeline;
    mrhiRequestId request = {0};
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_success &&
              request.index1 == token.index1 + 1,
          "the pipeline's request follows the frame's token");
    CHECK(mrhiBeginFrame(device, &frameDef) == mrhi_success &&
              mrhiSubmitFrame(device, &token) == mrhi_success && token.index1 == request.index1 + 1,
          "and the next frame's token follows it");
    uint32_t pipelines = 0;
    uint32_t frames = 0;
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(device, &record) == mrhi_success)
    {
        pipelines += record.kind == mrhi_devicePipelineReady ? 1 : 0;
        frames += record.kind == mrhi_deviceFrameDone ? 1 : 0;
    }
    CHECK(pipelines == 1 && frames == 2, "each answered once, by kind");
    Close(device);
}

static void TestInvalid(void)
{
    mrhiDevice* device = Open(4, 16, true);
    Reset();
    mrhiShaderId shader = Shader(device);
    mrhiComputePipelineId pipeline;
    mrhiRequestId request;
    mrhiComputePipelineDef def = Def(shader, "cs");
    CHECK(mrhiCreateComputePipeline(nullptr, &def, &pipeline, &request) == mrhi_errorInvalid,
          "no device");
    CHECK(mrhiCreateComputePipeline(device, nullptr, &pipeline, &request) == mrhi_errorInvalid,
          "no def");
    CHECK(mrhiCreateComputePipeline(device, &def, nullptr, &request) == mrhi_errorInvalid,
          "no pipeline out");
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, nullptr) == mrhi_errorInvalid,
          "no request out");
    def.cookie = 0;
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_errorInvalid,
          "no cookie");
    def = Def(shader, "cs");
    def.label = "\xFF";
    def.labelLength = 1;
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_errorInvalid,
          "a bad label");
    const char* entries[] = {"vs", "fs", "c", "css", "", nullptr};
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); ++i)
    {
        def = Def(shader, entries[i]);
        CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_errorInvalid,
              "not a compute entry");
    }
    def = Def(shader, "cs");
    def.entryLength = 1;
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_errorInvalid,
          "a name's start");
    def = Def(shader, nullptr);
    def.entryLength = 2;
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_errorInvalid,
          "no name with a length");
    CHECK(mrhiGetDeviceMisuse(device) == 13, "each counted");
    mrhiChain critical = {.next = nullptr, .type = 0x7000u};
    def = Def(shader, "cs");
    def.next = &critical;
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_errorUnsupported,
          "an extension");
    def = Def((mrhiShaderId){shader.index1, shader.generation + 2}, "cs");
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_errorStale,
          "a stale shader");
    def = Def((mrhiShaderId){0, 0}, "cs");
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_errorStale,
          "no shader");
    CHECK(mrhiDestroyComputePipeline(device, (mrhiComputePipelineId){1, 1}) == mrhi_errorStale,
          "a pipeline never made");
    CHECK(mrhiGetDeviceMisuse(device) == 13, "other refusals are not misuse");
    CHECK(mrhiNextDeviceNotification(device, &(mrhiDeviceNotification){0}) == mrhi_empty,
          "nothing started");
    Close(device);
}

// Creates a compute pipeline with one constant value, destroying it
// again: the creation's result.
static mrhiResult WithValue(mrhiDevice* device, mrhiShaderId shader, uint32_t id, double value)
{
    mrhiConstantValue values[2] = {{10, 0.0}, {id, value}};
    mrhiComputePipelineDef def = Def(shader, "cs");
    def.constants = values;
    def.constantCount = id == 10 ? 1 : 2;
    values[0].value = id == 10 ? value : 0.0;
    mrhiComputePipelineId pipeline;
    mrhiRequestId request;
    mrhiResult status = mrhiCreateComputePipeline(device, &def, &pipeline, &request);
    if (status == mrhi_success)
    {
        CHECK(mrhiDestroyComputePipeline(device, pipeline) == mrhi_success, "destroyed");
        CHECK(Next(device).outcome == mrhi_errorStale, "answered stale");
    }
    return status;
}

static void TestConstants(void)
{
    mrhiDevice* device = Open(4, 16, true);
    Constants();
    mrhiShaderId shader = Shader(device);
    CHECK(WithValue(device, shader, 8, 1.0) == mrhi_success, "true");
    CHECK(WithValue(device, shader, 8, 0.0) == mrhi_success, "false");
    CHECK(WithValue(device, shader, 8, 0.5) == mrhi_errorInvalid, "a boolean of one half");
    CHECK(WithValue(device, shader, 8, 2.0) == mrhi_errorInvalid, "a boolean of 2");
    CHECK(WithValue(device, shader, 9, -2147483648.0) == mrhi_success, "the least int32");
    CHECK(WithValue(device, shader, 9, 2147483647.0) == mrhi_success, "the greatest int32");
    CHECK(WithValue(device, shader, 9, 2147483648.0) == mrhi_errorInvalid, "past int32");
    CHECK(WithValue(device, shader, 9, -2147483649.0) == mrhi_errorInvalid, "below int32");
    CHECK(WithValue(device, shader, 9, 1.5) == mrhi_errorInvalid, "an int32 with a fraction");
    CHECK(WithValue(device, shader, 10, 4294967295.0) == mrhi_success, "the greatest uint32");
    CHECK(WithValue(device, shader, 10, 4294967296.0) == mrhi_errorInvalid, "past uint32");
    CHECK(WithValue(device, shader, 10, -1.0) == mrhi_errorInvalid, "a negative uint32");
    CHECK(WithValue(device, shader, 10, 0.25) == mrhi_errorInvalid, "a uint32 with a fraction");
    CHECK(WithValue(device, shader, 7, (double)FLT_MAX) == mrhi_success, "the greatest float");
    CHECK(WithValue(device, shader, 7, -(double)FLT_MAX) == mrhi_success, "the least float");
    CHECK(WithValue(device, shader, 7, 3.5e38) == mrhi_errorInvalid, "past float");
    CHECK(WithValue(device, shader, 7, -3.5e38) == mrhi_errorInvalid, "below float");
    CHECK(WithValue(device, shader, 7, (double)INFINITY) == mrhi_errorInvalid, "infinity");
    CHECK(WithValue(device, shader, 7, (double)NAN) == mrhi_errorInvalid, "NaN");
    CHECK(WithValue(device, shader, 11, 0.0) == mrhi_errorInvalid, "an unknown id");
    CHECK(WithValue(device, shader, 10, 1.0) == mrhi_success, "the required one alone");
    mrhiComputePipelineDef def = Def(shader, "cs");
    mrhiComputePipelineId pipeline;
    mrhiRequestId request;
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_errorInvalid,
          "the required one left unset");
    mrhiConstantValue values[5] = {{10, 1.0}, {10, 2.0}, {7, 0.0}, {8, 0.0}, {9, 0.0}};
    def.constants = values;
    def.constantCount = 2;
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_errorInvalid,
          "one set twice");
    values[1].id = 7;
    def.constantCount = 5;
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_errorInvalid,
          "more values than constants");
    def.constants = nullptr;
    def.constantCount = 1;
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_errorInvalid,
          "no values with a count");
    def.constants = &values[2];
    def.constantCount = 3;
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_errorInvalid,
          "every value but the required one");
    Close(device);
}

static void TestPendingAndOutcome(void)
{
    s_adapter.pipelineOutcome = mrhi_errorPlatform;
    mrhiDevice* device = Open(4, 16, true);
    Reset();
    mrhiShaderId shader = Shader(device);
    mrhiComputePipelineDef def = Def(shader, "cs");
    mrhiComputePipelineId pipeline;
    mrhiRequestId request = {0};
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_success, "made");
    CHECK(Next(device).outcome == mrhi_errorPlatform, "failed in the driver");
    CHECK(mrhiDestroyComputePipeline(device, pipeline) == mrhi_success, "a failed one destroyed");
    CHECK(mrhiNextDeviceNotification(device, &(mrhiDeviceNotification){0}) == mrhi_empty,
          "without a second answer");
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_success, "again");
    CHECK(mrhiDestroyComputePipeline(device, pipeline) == mrhi_success, "destroyed pending");
    mrhiDeviceNotification record = Next(device);
    CHECK(record.kind == mrhi_devicePipelineReady && record.request.index1 == request.index1 &&
              record.outcome == mrhi_errorStale,
          "answered stale at once");
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_empty, "and never by the driver");
    Close(device);
    s_adapter.pipelineOutcome = mrhi_success;
    device = Open(4, 1, true);
    Reset();
    def = Def(Shader(device), "cs");
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_success &&
              mrhiDestroyComputePipeline(device, pipeline) == mrhi_success,
          "destroyed pending on a queue of one");
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_errorCapacity,
          "its stale answer holds the room");
    CHECK(Next(device).outcome == mrhi_errorStale, "taken");
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_success,
          "and the room is free, no longer held for the destroyed one");
    Close(device);
}

static void TestRoomAndLimits(void)
{
    mrhiDevice* device = Open(4, 2, true);
    Reset();
    mrhiShaderId shader = Shader(device);
    mrhiComputePipelineDef def = Def(shader, "cs");
    mrhiComputePipelineId pipelines[4];
    mrhiRequestId request;
    CHECK(mrhiCreateComputePipeline(device, &def, &pipelines[0], &request) == mrhi_success &&
              mrhiCreateComputePipeline(device, &def, &pipelines[1], &request) == mrhi_success,
          "two answers fit the queue");
    CHECK(mrhiCreateComputePipeline(device, &def, &pipelines[2], &request) == mrhi_errorCapacity,
          "a third has no room");
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    mrhiRequestId token;
    CHECK(mrhiBeginFrame(device, &frameDef) == mrhi_success &&
              mrhiSubmitFrame(device, &token) == mrhi_errorCapacity,
          "nor a frame");
    CHECK(mrhiDropFrame(device) == mrhi_success, "dropped");
    // Beginning the frame took both answers in, so the queue is full.
    CHECK(mrhiDestroyComputePipeline(device, pipelines[0]) == mrhi_success,
          "a ready one destroyed, answering nothing");
    CHECK(mrhiCreateComputePipeline(device, &def, &pipelines[2], &request) == mrhi_errorCapacity,
          "the queued answers hold their room");
    Next(device);
    Next(device);
    CHECK(mrhiCreateComputePipeline(device, &def, &pipelines[2], &request) == mrhi_success &&
              mrhiCreateComputePipeline(device, &def, &pipelines[3], &request) == mrhi_success,
          "room for two again");
    Close(device);
    device = Open(1, 16, true);
    Reset();
    shader = Shader(device);
    def = Def(shader, "cs");
    CHECK(mrhiCreateComputePipeline(device, &def, &pipelines[0], &request) == mrhi_success, "one");
    CHECK(mrhiCreateComputePipeline(device, &def, &pipelines[1], &request) == mrhi_errorCapacity,
          "the pipeline limit");
    CHECK(mrhiDestroyComputePipeline(device, pipelines[0]) == mrhi_success, "destroyed");
    CHECK(mrhiCreateComputePipeline(device, &def, &pipelines[1], &request) == mrhi_success,
          "its slot again");
    Close(device);
}

static void TestStateAndFailure(void)
{
    mrhiDevice* device = Open(4, 16, false);
    mrhiComputePipelineDef def = Def((mrhiShaderId){1, 1}, "cs");
    mrhiComputePipelineId pipeline;
    mrhiRequestId request;
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_errorState,
          "not ready");
    Close(device);
    s_adapter.objectsBeforeFailure = 1;
    device = Open(1, 1, true);
    Reset();
    def = Def(Shader(device), "cs");
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_errorPlatform,
          "the driver fails at once");
    CHECK(mrhiNextDeviceNotification(device, &(mrhiDeviceNotification){0}) == mrhi_empty,
          "nothing to answer");
    s_adapter.objectsBeforeFailure = 0;
    Close(device);
    // The test driver holds 64 creations: a 65th fails there, and its slot
    // comes back once the driver has room.
    device = Open(65, 65, true);
    Reset();
    def = Def(Shader(device), "cs");
    for (int i = 0; i < 64; ++i)
    {
        CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_success,
              "one of 64");
    }
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_errorPlatform,
          "the driver is full");
    for (int i = 0; i < 64; ++i)
    {
        CHECK(Next(device).outcome == mrhi_success, "answered");
    }
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_success,
          "the failed one's slot again");
    Close(device);
}

int main(void)
{
    ResetAdapter();
    TestCreate();
    TestRequestsShared();
    TestInvalid();
    TestConstants();
    TestPendingAndOutcome();
    TestRoomAndLimits();
    TestStateAndFailure();
    return s_failures == 0 ? 0 : 1;
}
