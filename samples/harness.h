// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The samples' harness: each sample is a program that checks its own
// result, so the samples double as tests. The harness opens an
// instance on the build's native driver (Vulkan, or WebGPU on the web)
// and a device on its first adapter, waits for the device's answers,
// and turns the checks into an exit status: 0 when every check passed,
// 77 when there is no adapter and none is required
// (MAUL_RHI_REQUIRE_VULKAN, MAUL_RHI_REQUIRE_WEBGPU), 1 otherwise. On the
// web a browser answers only between the page's tasks, so the harness
// sleeps there while it waits (JSPI).

#ifndef MAUL_RHI_SAMPLES_HARNESS_H
#define MAUL_RHI_SAMPLES_HARNESS_H

#include "maul-rhi/device.h"
#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/shader.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// The exit status of a sample skipped for want of an adapter.
#define SAMPLE_SKIPPED 77

typedef struct Sample
{
    mrhiInstance* instance;
    mrhiAdapterId adapter;
    mrhiDevice* device;
    // The checks that failed.
    int failures;
} Sample;

// Opens the sample's instance and a device with the features given
// (none when NULL): 0, SAMPLE_SKIPPED, or 1 after printing why.
int SampleOpen(Sample* sample, const mrhiFeatures* features);

// Destroys the device and makes a new one without features on the same
// adapter, as a program recovering from a loss does: whether it opened.
bool SampleReopen(Sample* sample);

// Destroys the device and the instance and returns the exit status.
int SampleClose(Sample* sample);

// Records a check, printing what failed; returns the condition.
bool SampleCheck(Sample* sample, bool condition, const char* what);

// Makes a shader from a container's bytes.
mrhiShaderId SampleShader(Sample* sample, const uint8_t* bytes, size_t size);

// Makes a pipeline and waits until it is ready.
mrhiGraphicsPipelineId SampleGraphics(Sample* sample, const mrhiGraphicsPipelineDef* def);
mrhiComputePipelineId SampleCompute(Sample* sample, const mrhiComputePipelineDef* def);

// Waits for a submitted frame, and for the frames before it, and takes
// every answer: whether each was a success.
bool SampleWait(Sample* sample, mrhiRequestId token);

// Submits the open frame and waits for it.
bool SampleFinish(Sample* sample);

// Takes a readback's bytes, exactly size of them.
bool SampleTake(Sample* sample, mrhiRequestId request, void* bytes, size_t size);

// Whether a pixel of 8-bit channels is within tolerance of a color.
bool SampleNear(const uint8_t* pixel, const uint8_t expected[4], int tolerance);

#endif // MAUL_RHI_SAMPLES_HARNESS_H
