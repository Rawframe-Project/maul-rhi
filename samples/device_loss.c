// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Device loss and recovery. The program loses its device on purpose
// with mrhiSimulateDeviceLoss while a frame with a readback is running,
// as a GPU hang or reset would lose it: the notice comes first, then the
// frame and its readback answered lost; the report says why; calls that
// need the GPU answer lost, while destroying still works. The program
// then makes a new device on the same adapter, makes its objects again
// and carries on.

#include "harness.h"

#include <stdio.h>
#include <string.h>

static mrhiBufferId MakeBuffer(Sample* sample)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = 256;
    def.usage = mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    mrhiBufferId buffer = {0};
    SampleCheck(sample, mrhiCreateBuffer(sample->device, &def, &buffer) == mrhi_success,
                "a buffer");
    return buffer;
}

// Records a frame writing bytes into the buffer and reading them back:
// the readback.
static mrhiRequestId Record(Sample* sample, mrhiBufferId bufferId, const uint8_t* bytes,
                            size_t size)
{
    mrhiDevice* device = sample->device;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId buffer = {0};
    SampleCheck(sample,
                mrhiBeginFrame(device, &frame) == mrhi_success &&
                    mrhiImportBuffer(device, bufferId, &buffer) == mrhi_success,
                "a frame");
    const mrhiAccess write = {.resource = buffer, .kind = mrhi_accessCopyDestination};
    const mrhiAccess read = {.resource = buffer, .kind = mrhi_accessCopySource};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.accesses = &write;
    def.accessCount = 1;
    mrhiPassId upload = {0};
    SampleCheck(sample, mrhiAddPass(device, &def, &upload) == mrhi_success, "an upload pass");
    def.accesses = &read;
    def.neverCull = true;
    mrhiPassId reading = {0};
    mrhiRequestId request = {0};
    SampleCheck(sample,
                mrhiAddPass(device, &def, &reading) == mrhi_success &&
                    mrhiCompileFrame(device) == mrhi_success &&
                    mrhiBeginPass(device, upload) == mrhi_success &&
                    mrhiWriteBuffer(device, upload, buffer, 0, bytes, size) == mrhi_success &&
                    mrhiEndPass(device, upload) == mrhi_success &&
                    mrhiBeginPass(device, reading) == mrhi_success &&
                    mrhiReadBuffer(device, reading, buffer, 0, size, &request) == mrhi_success &&
                    mrhiEndPass(device, reading) == mrhi_success,
                "written and read");
    return request;
}

// Loses the device under a running frame and reads what it answers.
static void Lose(Sample* sample, mrhiBufferId buffer)
{
    mrhiDevice* device = sample->device;
    const uint8_t bytes[16] = {1, 2, 3, 4};
    mrhiRequestId readback = Record(sample, buffer, bytes, sizeof(bytes));
    mrhiRequestId token = {0};
    SampleCheck(sample, mrhiSubmitFrame(device, &token) == mrhi_success, "a frame running");
    SampleCheck(sample,
                mrhiSimulateDeviceLoss(device) == mrhi_success &&
                    mrhiGetDeviceState(device) == mrhi_deviceLost,
                "the device lost");
    mrhiDeviceNotification record;
    SampleCheck(sample,
                mrhiNextDeviceNotification(device, &record) == mrhi_success &&
                    record.kind == mrhi_deviceLostNotice && record.outcome == mrhi_errorDeviceLost,
                "the notice first");
    SampleCheck(
        sample,
        mrhiNextDeviceNotification(device, &record) == mrhi_success &&
            record.kind == mrhi_deviceFrameDone && record.requestId.index1 == token.index1 &&
            record.outcome == mrhi_errorDeviceLost &&
            mrhiNextDeviceNotification(device, &record) == mrhi_success &&
            record.kind == mrhi_deviceReadbackReady && record.requestId.index1 == readback.index1 &&
            record.outcome == mrhi_errorDeviceLost,
        "the frame and its readback answered lost");
    mrhiDeviceLossReport report;
    SampleCheck(sample,
                mrhiGetDeviceLossReport(device, &report) == mrhi_success &&
                    report.reason == mrhi_lossSimulated &&
                    report.lastSubmitted.index1 == token.index1,
                "a report of a simulated loss");
    printf("lost: %.*s\n", (int)report.messageLength, report.message);
    size_t size = 0;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiBufferId another = {0};
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = 16;
    def.usage = mrhi_bufferCopyDestination;
    SampleCheck(sample,
                mrhiTakeReadback(device, readback, nullptr, 0, &size) == mrhi_errorDeviceLost &&
                    mrhiBeginFrame(device, &frame) == mrhi_errorDeviceLost &&
                    mrhiCreateBuffer(device, &def, &another) == mrhi_errorDeviceLost,
                "the GPU's work refused");
    SampleCheck(sample, mrhiDestroyBuffer(device, buffer) == mrhi_success,
                "destroying still works");
}

int main(void)
{
    Sample sample;
    int opened = SampleOpen(&sample, nullptr);
    if (opened != 0)
    {
        return opened;
    }
    Lose(&sample, MakeBuffer(&sample));
    if (SampleReopen(&sample))
    {
        // Everything the lost device held is made again.
        mrhiBufferId buffer = MakeBuffer(&sample);
        uint8_t bytes[16];
        for (size_t i = 0; i < sizeof(bytes); ++i)
        {
            bytes[i] = (uint8_t)(i * 17);
        }
        mrhiRequestId readback = Record(&sample, buffer, bytes, sizeof(bytes));
        uint8_t back[sizeof(bytes)] = {0};
        SampleCheck(&sample,
                    SampleFinish(&sample) && SampleTake(&sample, readback, back, sizeof(back)) &&
                        memcmp(back, bytes, sizeof(bytes)) == 0,
                    "the new device's work");
        SampleCheck(&sample, mrhiDestroyBuffer(sample.device, buffer) == mrhi_success,
                    "the new buffer destroyed");
    }
    return SampleClose(&sample);
}
