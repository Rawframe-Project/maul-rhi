// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Frames on test driver devices: one open at a time, tokens answered once
// in the device's queue, the frames in flight limit, waits with and
// without a deadline, waits on the device's queue, and a failing driver.

#include "test_device_setup.h"

static mrhiResult Begin(mrhiDevice* device)
{
    mrhiFrameDef def = mrhiDefaultFrameDef();
    return mrhiDevice_BeginFrame(device, &def);
}

// Begins and submits a frame, returning its token.
static mrhiRequestId Run(mrhiDevice* device)
{
    mrhiRequestId token = {0};
    CHECK(Begin(device) == mrhi_success, "begun");
    CHECK(mrhiDevice_SubmitFrame(device, &token) == mrhi_success, "submitted");
    return token;
}

static void TestLifecycle(void)
{
    mrhiDevice* device = OpenWith(mrhiDefaultDeviceDef(), true);
    mrhiRequestId token;
    CHECK(mrhiDevice_SubmitFrame(device, &token) == mrhi_errorState, "nothing to submit");
    CHECK(mrhiDevice_DropFrame(device) == mrhi_errorState, "nothing to drop");
    CHECK(Begin(device) == mrhi_success, "begun");
    CHECK(Begin(device) == mrhi_errorState, "one frame at a time");
    CHECK(mrhiDevice_DropFrame(device) == mrhi_success, "dropped");
    token = Run(device);
    CHECK(token.index1 == 1 && token.generation == 1, "the first token");
    mrhiDeviceNotification record;
    CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_success, "answered");
    CHECK(record.kind == mrhi_deviceFrameDone && record.request.index1 == token.index1 &&
              record.outcome == mrhi_success,
          "the frame is done");
    CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_empty, "once");
    CHECK(mrhiRequest_WaitFrame(device, token, 0) == mrhi_success, "a finished frame");
    mrhiRequestId next = Run(device);
    CHECK(next.index1 == 2, "the next token");
    CHECK(mrhiRequest_WaitFrame(device, next, 0) == mrhi_success, "finished by now");
    CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_success &&
              record.request.index1 == 2,
          "and still answered once");
    CHECK(mrhiDevice_GetMisuse(device) == 0, "no misuse");
    Close(device);
}

// Held frames keep the device's frames in flight full until waited on.
static void TestFramesInFlight(void)
{
    s_adapter.holdFrames = true;
    mrhiDevice* device = OpenWith(mrhiDefaultDeviceDef(), true);
    mrhiRequestId first = Run(device);
    mrhiRequestId second = Run(device);
    CHECK(Begin(device) == mrhi_errorCapacity, "two frames in flight");
    CHECK(mrhiRequest_WaitFrame(device, first, 0) == mrhi_timeout, "not finished");
    CHECK(Begin(device) == mrhi_errorCapacity, "still two");
    mrhiDeviceNotification record;
    CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_empty, "nothing finished");
    CHECK(mrhiRequest_WaitFrame(device, second, 1) == mrhi_success, "the second waited for");
    CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_success &&
              record.request.index1 == second.index1,
          "the second answered");
    CHECK(mrhiRequest_WaitFrame(device, first, 0) == mrhi_timeout, "the first still running");
    CHECK(mrhiRequest_WaitFrame(device, first, 1000000) == mrhi_success, "waited for");
    CHECK(Begin(device) == mrhi_success, "room for one");
    mrhiRequestId third = {0};
    CHECK(mrhiDevice_SubmitFrame(device, &third) == mrhi_success && third.index1 == 3, "the third");
    CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_success &&
              record.request.index1 == first.index1,
          "the first answered");
    CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_empty, "the third held");
    CHECK(mrhiRequest_WaitFrame(device, third, 1) == mrhi_success, "the third");
    CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_success, "its answer");
    CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_empty, "all answered");
    Close(device);
}

// Every submitted frame has room for its answer in the device's queue.
static void TestQueueRoom(void)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    // Two answers, and the record kept for the loss notice.
    def.budgets.notifications = 3;
    mrhiDevice* device = OpenWith(def, true);
    Run(device);
    Run(device);
    CHECK(Begin(device) == mrhi_success, "begun");
    mrhiRequestId token;
    CHECK(mrhiDevice_SubmitFrame(device, &token) == mrhi_errorCapacity, "two answers not taken");
    mrhiDeviceNotification record;
    CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_success, "one taken");
    CHECK(mrhiDevice_SubmitFrame(device, &token) == mrhi_success, "the frame was kept open");
    Close(device);
}

// Answers wrap round the queue in order, on both ends.
static void TestQueueWraps(void)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    // Two answers, and the record kept for the loss notice.
    def.budgets.notifications = 3;
    mrhiDevice* device = OpenWith(def, true);
    mrhiRequestId a = Run(device);
    mrhiRequestId b = Run(device);
    mrhiDeviceNotification record;
    CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_success &&
              record.request.index1 == a.index1,
          "the first");
    mrhiRequestId c = Run(device);
    CHECK(Begin(device) == mrhi_success, "the third's answer taken in, past the queue's end");
    CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_success &&
              record.request.index1 == b.index1,
          "the second");
    CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_success &&
              record.request.index1 == c.index1,
          "the third, from the queue's start");
    CHECK(mrhiDevice_DropFrame(device) == mrhi_success, "dropped");
    Close(device);
}

// Running frames count toward the answers the queue must hold.
static void TestRunningNeedRoom(void)
{
    s_adapter.holdFrames = true;
    s_adapter.limits.framesInFlight = 3;
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.limits.framesInFlight = 3;
    // Two answers, and the record kept for the loss notice.
    def.budgets.notifications = 3;
    mrhiDevice* device = OpenWith(def, true);
    Run(device);
    Run(device);
    CHECK(Begin(device) == mrhi_success, "a third may begin");
    mrhiRequestId token;
    CHECK(mrhiDevice_SubmitFrame(device, &token) == mrhi_errorCapacity, "but not be answered");
    CHECK(mrhiDevice_DropFrame(device) == mrhi_success, "dropped");
    Close(device);
}

// A wait on the device's queue waits for the oldest running frame: a
// held frame times out a check, finishes when waited on with a deadline,
// and its answer is then in the queue. Two frames are answered in their
// order of finishing, the oldest waited on first.
static void TestWaitNotification(void)
{
    s_adapter.holdFrames = true;
    mrhiDevice* device = OpenWith(mrhiDefaultDeviceDef(), true);
    CHECK(mrhiDevice_WaitNotification(device, 1000) == mrhi_empty, "nothing owed, at once");
    mrhiRequestId first = Run(device);
    mrhiRequestId second = Run(device);
    CHECK(mrhiDevice_WaitNotification(device, 0) == mrhi_timeout, "a check of held frames");
    CHECK(mrhiDevice_WaitNotification(device, 1000) == mrhi_success, "the oldest finished");
    mrhiDeviceNotification record;
    CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_success &&
              record.kind == mrhi_deviceFrameDone && record.request.index1 == first.index1,
          "its answer");
    CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_empty, "the second still held");
    CHECK(mrhiDevice_WaitNotification(device, 1000) == mrhi_success &&
              mrhiDevice_TakeNotification(device, &record) == mrhi_success &&
              record.request.index1 == second.index1,
          "then the second");
    CHECK(mrhiDevice_WaitNotification(device, 1000) == mrhi_empty, "nothing owed after");
    Close(device);
    s_adapter.holdFrames = false;
}

// A frame that fails on the GPU is answered with the error.
static void TestFrameOutcome(void)
{
    s_adapter.frameOutcome = mrhi_errorPlatform;
    mrhiDevice* device = OpenWith(mrhiDefaultDeviceDef(), true);
    Run(device);
    mrhiDeviceNotification record;
    CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_success &&
              record.outcome == mrhi_errorPlatform,
          "the error");
    Close(device);
}

static void TestRefusals(void)
{
    mrhiDevice* device = OpenWith(mrhiDefaultDeviceDef(), false);
    mrhiFrameDef def = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(device, &def) == mrhi_errorState, "a device still opening");
    mrhiInstanceNotification opened;
    CHECK(mrhiInstance_TakeNotification(s_instance, &opened) == mrhi_success, "ready");
    CHECK(mrhiDevice_BeginFrame(nullptr, &def) == mrhi_errorInvalid, "no device");
    CHECK(mrhiDevice_BeginFrame(device, nullptr) == mrhi_errorInvalid, "no def");
    def.cookie = 0;
    CHECK(mrhiDevice_BeginFrame(device, &def) == mrhi_errorInvalid, "no cookie");
    def = mrhiDefaultFrameDef();
    mrhiChain critical = {.next = nullptr, .type = 0x7000u};
    def.next = &critical;
    CHECK(mrhiDevice_BeginFrame(device, &def) == mrhi_errorUnsupported,
          "an unknown critical extension");
    CHECK(mrhiDevice_DropFrame(nullptr) == mrhi_errorInvalid, "no device to drop on");
    mrhiRequestId token;
    CHECK(mrhiDevice_SubmitFrame(nullptr, &token) == mrhi_errorInvalid, "no device to submit on");
    CHECK(mrhiDevice_SubmitFrame(device, nullptr) == mrhi_errorInvalid, "no token out");
    CHECK(mrhiRequest_WaitFrame(nullptr, token, 0) == mrhi_errorInvalid, "no device to wait on");
    CHECK(mrhiRequest_WaitFrame(device, (mrhiRequestId){0, 0}, 0) == mrhi_errorInvalid,
          "a null token");
    CHECK(mrhiRequest_WaitFrame(device, (mrhiRequestId){0, 1}, 0) == mrhi_errorInvalid, "token 0");
    CHECK(mrhiRequest_WaitFrame(device, (mrhiRequestId){1, 1}, 0) == mrhi_errorInvalid,
          "not given yet");
    token = Run(device);
    CHECK(mrhiRequest_WaitFrame(device, (mrhiRequestId){token.index1, 2}, 0) == mrhi_errorInvalid,
          "another generation");
    mrhiDeviceNotification record;
    CHECK(mrhiDevice_TakeNotification(nullptr, &record) == mrhi_errorInvalid, "no device");
    CHECK(mrhiDevice_TakeNotification(device, nullptr) == mrhi_errorInvalid, "no record out");
    CHECK(mrhiDevice_GetMisuse(device) == 8, "each counted");
    Close(device);
}

// A failing driver closes the frame without a token.
static void TestFailure(void)
{
    s_adapter.objectsBeforeFailure = 1;
    mrhiDevice* device = OpenWith(mrhiDefaultDeviceDef(), true);
    Run(device);
    CHECK(Begin(device) == mrhi_success, "begun");
    mrhiRequestId token = {0};
    CHECK(mrhiDevice_SubmitFrame(device, &token) == mrhi_errorPlatform, "the driver fails");
    CHECK(token.index1 == 0, "no token");
    CHECK(mrhiDevice_DropFrame(device) == mrhi_errorState, "the frame closed");
    CHECK(Begin(device) == mrhi_success, "another frame");
    CHECK(mrhiDevice_DropFrame(device) == mrhi_success, "dropped");
    Close(device);
}

int main(void)
{
    ResetAdapter();
    TestLifecycle();
    TestFramesInFlight();
    TestQueueRoom();
    TestQueueWraps();
    TestRunningNeedRoom();
    TestFrameOutcome();
    TestWaitNotification();
    TestRefusals();
    TestFailure();
    return s_failures == 0 ? 0 : 1;
}
