// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The samples' harness (harness.h).

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "harness.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define REQUIRED "MAUL_RHI_REQUIRE_WEBGPU"
#else
#define REQUIRED "MAUL_RHI_REQUIRE_VULKAN"
#endif

// How long a sample waits for an answer: 10 s natively, 10,000 sleeps
// of a millisecond or more on the web.
#define WAIT_NS     UINT64_C(10000000000)
#define WAIT_SLEEPS 10000

static mrhiResult NextInstance(mrhiInstance* instance, mrhiInstanceNotification* recordOut)
{
    mrhiResult status = mrhiNextInstanceNotification(instance, recordOut);
#ifdef __EMSCRIPTEN__
    for (int slept = 0; status == mrhi_empty && slept < WAIT_SLEEPS; ++slept)
    {
        emscripten_sleep(1);
        status = mrhiNextInstanceNotification(instance, recordOut);
    }
#endif
    return status;
}

static mrhiResult NextDevice(mrhiDevice* device, mrhiDeviceNotification* recordOut)
{
    mrhiResult status = mrhiNextDeviceNotification(device, recordOut);
#ifdef __EMSCRIPTEN__
    for (int slept = 0; status == mrhi_empty && slept < WAIT_SLEEPS; ++slept)
    {
        emscripten_sleep(1);
        status = mrhiNextDeviceNotification(device, recordOut);
    }
#endif
    return status;
}

static bool IsRequired(void)
{
    const char* value = getenv(REQUIRED);
    return value != nullptr && value[0] != '\0';
}

// The first adapter the native driver lists, software ones included.
static bool FindAdapter(Sample* sample)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    if (mrhiCreateInstance(&def, &sample->instance) != mrhi_success)
    {
        return false;
    }
    mrhiAdapterRequestDef request = mrhiDefaultAdapterRequestDef();
    request.allowSoftware = true;
    mrhiRequestId id = {0};
    mrhiInstanceNotification record;
    size_t count = 0;
    return mrhiRequestAdapters(sample->instance, &request, &id) == mrhi_success &&
           NextInstance(sample->instance, &record) == mrhi_success &&
           record.outcome == mrhi_success &&
           mrhiGetAdapters(sample->instance, &sample->adapter, 1, &count) == mrhi_success &&
           count > 0;
}

// Makes a device on the sample's adapter and waits until it opens.
static bool OpenDevice(Sample* sample, const mrhiFeatures* features)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = sample->adapter;
    if (features != nullptr)
    {
        def.features = *features;
    }
    mrhiRequestId request = {0};
    mrhiInstanceNotification record;
    sample->device = nullptr;
    if (mrhiCreateDevice(sample->instance, &def, &sample->device, &request) != mrhi_success)
    {
        return false;
    }
    if (NextInstance(sample->instance, &record) != mrhi_success || record.outcome != mrhi_success)
    {
        mrhiDestroyDevice(sample->device);
        sample->device = nullptr;
        return false;
    }
    return true;
}

int SampleOpen(Sample* sample, const mrhiFeatures* features)
{
    *sample = (Sample){0};
    if (!FindAdapter(sample))
    {
        if (sample->instance != nullptr)
        {
            mrhiDestroyInstance(sample->instance);
        }
        printf("%s: no adapter\n", IsRequired() ? "FAIL" : "skip");
        return IsRequired() ? 1 : SAMPLE_SKIPPED;
    }
    if (!OpenDevice(sample, features))
    {
        printf("FAIL: no device\n");
        mrhiDestroyInstance(sample->instance);
        return 1;
    }
    return 0;
}

bool SampleReopen(Sample* sample)
{
    mrhiDestroyDevice(sample->device);
    return SampleCheck(sample, OpenDevice(sample, nullptr), "a new device");
}

int SampleClose(Sample* sample)
{
    if (sample->device != nullptr)
    {
        mrhiDestroyDevice(sample->device);
    }
    mrhiDestroyInstance(sample->instance);
    return sample->failures == 0 ? 0 : 1;
}

bool SampleCheck(Sample* sample, bool condition, const char* what)
{
    if (!condition)
    {
        printf("FAIL: %s\n", what);
        ++sample->failures;
    }
    return condition;
}

mrhiShaderId SampleShader(Sample* sample, const uint8_t* bytes, size_t size)
{
    mrhiShaderDef def = mrhiDefaultShaderDef();
    def.bytes = bytes;
    def.byteCount = size;
    mrhiShaderId shader = {0};
    SampleCheck(sample, mrhiCreateShader(sample->device, &def, &shader) == mrhi_success,
                "a shader");
    return shader;
}

// Waits for a pipeline's answer: whether it is ready.
static bool AwaitPipeline(Sample* sample, mrhiRequestId request)
{
    mrhiDeviceNotification record;
    while (NextDevice(sample->device, &record) == mrhi_success)
    {
        if (record.kind == mrhi_devicePipelineReady && record.request.index1 == request.index1 &&
            record.request.generation == request.generation)
        {
            return record.outcome == mrhi_success;
        }
    }
    return false;
}

mrhiGraphicsPipelineId SampleGraphics(Sample* sample, const mrhiGraphicsPipelineDef* def)
{
    mrhiGraphicsPipelineId pipeline = {0};
    mrhiRequestId request = {0};
    SampleCheck(sample,
                mrhiCreateGraphicsPipeline(sample->device, def, &pipeline, &request) ==
                        mrhi_success &&
                    AwaitPipeline(sample, request),
                "a graphics pipeline");
    return pipeline;
}

mrhiComputePipelineId SampleCompute(Sample* sample, const mrhiComputePipelineDef* def)
{
    mrhiComputePipelineId pipeline = {0};
    mrhiRequestId request = {0};
    SampleCheck(sample,
                mrhiCreateComputePipeline(sample->device, def, &pipeline, &request) ==
                        mrhi_success &&
                    AwaitPipeline(sample, request),
                "a compute pipeline");
    return pipeline;
}

// Waits for a frame to finish.
static mrhiResult WaitFrame(mrhiDevice* device, mrhiRequestId token)
{
    mrhiResult status = mrhiWaitFrame(device, token, WAIT_NS);
#ifdef __EMSCRIPTEN__
    for (int slept = 0; status == mrhi_timeout && slept < WAIT_SLEEPS; ++slept)
    {
        emscripten_sleep(1);
        status = mrhiWaitFrame(device, token, 0);
    }
#endif
    return status;
}

bool SampleWait(Sample* sample, mrhiRequestId token)
{
    bool ok =
        SampleCheck(sample, WaitFrame(sample->device, token) == mrhi_success, "the frame finished");
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(sample->device, &record) == mrhi_success)
    {
        ok = SampleCheck(sample, record.outcome == mrhi_success, "an answer a success") && ok;
    }
    return ok;
}

bool SampleFinish(Sample* sample)
{
    mrhiRequestId token = {0};
    return SampleCheck(sample, mrhiSubmitFrame(sample->device, &token) == mrhi_success,
                       "the frame submitted") &&
           SampleWait(sample, token);
}

bool SampleTake(Sample* sample, mrhiRequestId request, void* bytes, size_t size)
{
    size_t taken = 0;
    return SampleCheck(sample,
                       mrhiTakeReadback(sample->device, request, bytes, size, &taken) ==
                               mrhi_success &&
                           taken == size,
                       "a readback's bytes");
}

bool SampleNear(const uint8_t* pixel, const uint8_t expected[4], int tolerance)
{
    for (int i = 0; i < 4; ++i)
    {
        int difference = pixel[i] - expected[i];
        if (difference < -tolerance || difference > tolerance)
        {
            return false;
        }
    }
    return true;
}
