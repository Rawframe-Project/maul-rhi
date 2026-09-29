// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Frames on test driver devices: one open at a time, tokens answered once
// in the device's queue, the frames in flight limit, waits with and
// without a deadline, and a failing driver.

#include "test_device_setup.h"

static mrhiResult Begin(mrhiDevice* device)
{
    mrhiFrameDef def = mrhiDefaultFrameDef();
    return mrhiBeginFrame(device, &def);
}

// Begins and submits a frame, returning its token.
static mrhiRequestId Run(mrhiDevice* device)
{
    mrhiRequestId token = {0};
    CHECK(Begin(device) == mrhi_success, "begun");
    CHECK(mrhiSubmitFrame(device, &token) == mrhi_success, "submitted");
    return token;
}

static void TestLifecycle(void)
{
    mrhiDevice* device = OpenWith(mrhiDefaultDeviceDef(), true);
    mrhiRequestId token;
    CHECK(mrhiSubmitFrame(device, &token) == mrhi_errorState, "nothing to submit");
    CHECK(mrhiDropFrame(device) == mrhi_errorState, "nothing to drop");
    CHECK(Begin(device) == mrhi_success, "begun");
    CHECK(Begin(device) == mrhi_errorState, "one frame at a time");
    CHECK(mrhiDropFrame(device) == mrhi_success, "dropped");
    token = Run(device);
    CHECK(token.index1 == 1 && token.generation == 1, "the first token");
    mrhiDeviceNotification record;
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_success, "answered");
    CHECK(record.kind == mrhi_deviceFrameDone && record.requestId.index1 == token.index1 &&
              record.outcome == mrhi_success,
          "the frame is done");
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_empty, "once");
    CHECK(mrhiWaitFrame(device, token, 0) == mrhi_success, "a finished frame");
    mrhiRequestId next = Run(device);
    CHECK(next.index1 == 2, "the next token");
    CHECK(mrhiWaitFrame(device, next, 0) == mrhi_success, "finished by now");
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_success &&
              record.requestId.index1 == 2,
          "and still answered once");
    CHECK(mrhiGetDeviceMisuse(device) == 0, "no misuse");
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
    CHECK(mrhiWaitFrame(device, first, 0) == mrhi_timeout, "not finished");
    CHECK(Begin(device) == mrhi_errorCapacity, "still two");
    mrhiDeviceNotification record;
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_empty, "nothing finished");
    CHECK(mrhiWaitFrame(device, second, 1) == mrhi_success, "the second waited for");
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_success &&
              record.requestId.index1 == second.index1,
          "the second answered");
    CHECK(mrhiWaitFrame(device, first, 0) == mrhi_timeout, "the first still running");
    CHECK(mrhiWaitFrame(device, first, 1000000) == mrhi_success, "waited for");
    CHECK(Begin(device) == mrhi_success, "room for one");
    mrhiRequestId third = {0};
    CHECK(mrhiSubmitFrame(device, &third) == mrhi_success && third.index1 == 3, "the third");
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_success &&
              record.requestId.index1 == first.index1,
          "the first answered");
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_empty, "the third held");
    CHECK(mrhiWaitFrame(device, third, 1) == mrhi_success, "the third");
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_success, "its answer");
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_empty, "all answered");
    Close(device);
}

// Every submitted frame has room for its answer in the device's queue.
static void TestQueueRoom(void)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.deviceLimits.notifications = 2;
    mrhiDevice* device = OpenWith(def, true);
    Run(device);
    Run(device);
    CHECK(Begin(device) == mrhi_success, "begun");
    mrhiRequestId token;
    CHECK(mrhiSubmitFrame(device, &token) == mrhi_errorCapacity, "two answers not taken");
    mrhiDeviceNotification record;
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_success, "one taken");
    CHECK(mrhiSubmitFrame(device, &token) == mrhi_success, "the frame was kept open");
    Close(device);
}

// Answers wrap round the queue in order, on both ends.
static void TestQueueWraps(void)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.deviceLimits.notifications = 2;
    mrhiDevice* device = OpenWith(def, true);
    mrhiRequestId a = Run(device);
    mrhiRequestId b = Run(device);
    mrhiDeviceNotification record;
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_success &&
              record.requestId.index1 == a.index1,
          "the first");
    mrhiRequestId c = Run(device);
    CHECK(Begin(device) == mrhi_success, "the third's answer taken in, past the queue's end");
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_success &&
              record.requestId.index1 == b.index1,
          "the second");
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_success &&
              record.requestId.index1 == c.index1,
          "the third, from the queue's start");
    CHECK(mrhiDropFrame(device) == mrhi_success, "dropped");
    Close(device);
}

// Running frames count toward the answers the queue must hold.
static void TestRunningNeedRoom(void)
{
    s_adapter.holdFrames = true;
    s_adapter.limits.framesInFlight = 3;
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.limits.framesInFlight = 3;
    def.deviceLimits.notifications = 2;
    mrhiDevice* device = OpenWith(def, true);
    Run(device);
    Run(device);
    CHECK(Begin(device) == mrhi_success, "a third may begin");
    mrhiRequestId token;
    CHECK(mrhiSubmitFrame(device, &token) == mrhi_errorCapacity, "but not be answered");
    CHECK(mrhiDropFrame(device) == mrhi_success, "dropped");
    Close(device);
}

// A frame that fails on the GPU is answered with the error.
static void TestFrameOutcome(void)
{
    s_adapter.frameOutcome = mrhi_errorPlatform;
    mrhiDevice* device = OpenWith(mrhiDefaultDeviceDef(), true);
    Run(device);
    mrhiDeviceNotification record;
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_success &&
              record.outcome == mrhi_errorPlatform,
          "the error");
    Close(device);
}

static void TestRefusals(void)
{
    mrhiDevice* device = OpenWith(mrhiDefaultDeviceDef(), false);
    mrhiFrameDef def = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(device, &def) == mrhi_errorState, "a device still opening");
    mrhiInstanceNotification opened;
    CHECK(mrhiNextInstanceNotification(s_instance, &opened) == mrhi_success, "ready");
    CHECK(mrhiBeginFrame(nullptr, &def) == mrhi_errorInvalid, "no device");
    CHECK(mrhiBeginFrame(device, nullptr) == mrhi_errorInvalid, "no def");
    def.cookie = 0;
    CHECK(mrhiBeginFrame(device, &def) == mrhi_errorInvalid, "no cookie");
    def = mrhiDefaultFrameDef();
    mrhiChain critical = {.next = nullptr, .type = 0x7000u};
    def.next = &critical;
    CHECK(mrhiBeginFrame(device, &def) == mrhi_errorUnsupported, "an unknown critical extension");
    CHECK(mrhiDropFrame(nullptr) == mrhi_errorInvalid, "no device to drop on");
    mrhiRequestId token;
    CHECK(mrhiSubmitFrame(nullptr, &token) == mrhi_errorInvalid, "no device to submit on");
    CHECK(mrhiSubmitFrame(device, nullptr) == mrhi_errorInvalid, "no token out");
    CHECK(mrhiWaitFrame(nullptr, token, 0) == mrhi_errorInvalid, "no device to wait on");
    CHECK(mrhiWaitFrame(device, (mrhiRequestId){0, 0}, 0) == mrhi_errorInvalid, "a null token");
    CHECK(mrhiWaitFrame(device, (mrhiRequestId){0, 1}, 0) == mrhi_errorInvalid, "token 0");
    CHECK(mrhiWaitFrame(device, (mrhiRequestId){1, 1}, 0) == mrhi_errorInvalid, "not given yet");
    token = Run(device);
    CHECK(mrhiWaitFrame(device, (mrhiRequestId){token.index1, 2}, 0) == mrhi_errorInvalid,
          "another generation");
    mrhiDeviceNotification record;
    CHECK(mrhiNextDeviceNotification(nullptr, &record) == mrhi_errorInvalid, "no device");
    CHECK(mrhiNextDeviceNotification(device, nullptr) == mrhi_errorInvalid, "no record out");
    CHECK(mrhiGetDeviceMisuse(device) == 8, "each counted");
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
    CHECK(mrhiSubmitFrame(device, &token) == mrhi_errorPlatform, "the driver fails");
    CHECK(token.index1 == 0, "no token");
    CHECK(mrhiDropFrame(device) == mrhi_errorState, "the frame closed");
    CHECK(Begin(device) == mrhi_success, "another frame");
    CHECK(mrhiDropFrame(device) == mrhi_success, "dropped");
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
    TestRefusals();
    TestFailure();
    return s_failures == 0 ? 0 : 1;
}
