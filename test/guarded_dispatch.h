// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Indirect dispatches past the workgroup limit doing nothing on a native
// driver, as WebGPU's dispatchWorkgroupsIndirect does: the drivers whose
// API leaves such a dispatch undefined guard its counts on the GPU first.

#ifndef MAUL_RHI_TEST_GUARDED_DISPATCH_H
#define MAUL_RHI_TEST_GUARDED_DISPATCH_H

#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/shader.h"
#include "shaders/storage_container.h"

#include <string.h>

static mrhiAccess GuardedWhole(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
}

// Two indirect dispatches of the storage container's entry, four
// invocations writing each input doubled plus one: one of 70000
// workgroups, past the limit, which the native API leaves undefined and
// WebGPU skips, writes nothing to its outputs; the next, of one, writes
// its own after the library's kernel guarding the first replaced and the
// driver set again the pass's pipeline, constants and tables.
static void CheckGuardedDispatch(mrhiDevice* device)
{
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_storageContainer;
    shaderDef.byteCount = sizeof(s_storageContainer);
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(device, &shaderDef, &shader) == mrhi_success, "the storage shader");
    mrhiComputePipelineDef pipelineDef = mrhiDefaultComputePipelineDef();
    pipelineDef.shader = shader;
    pipelineDef.entry = "cs";
    pipelineDef.entryLength = 2;
    mrhiComputePipelineId pipeline = {0};
    mrhiRequestId request = {0};
    mrhiDeviceNotification record;
    CHECK(mrhiCreateComputePipeline(device, &pipelineDef, &pipeline, &request) == mrhi_success &&
              mrhiDevice_TakeNotification(device, &record) == mrhi_success &&
              record.kind == mrhi_devicePipelineReady && record.outcome == mrhi_success,
          "its pipeline");
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success, "a frame");
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 16;
    mrhiBufferDef argumentsDef = mrhiDefaultBufferDef();
    argumentsDef.size = 24;
    mrhiTextureDef imageDef = mrhiDefaultTextureDef();
    imageDef.format = mrhi_formatRgba8Unorm;
    imageDef.width = 4;
    imageDef.height = 1;
    mrhiResourceId inputs = {0};
    mrhiResourceId skipped = {0};
    mrhiResourceId written = {0};
    mrhiResourceId arguments = {0};
    mrhiResourceId image = {0};
    CHECK(mrhiDeclareBuffer(device, &bufferDef, &inputs) == mrhi_success &&
              mrhiDeclareBuffer(device, &bufferDef, &skipped) == mrhi_success &&
              mrhiDeclareBuffer(device, &bufferDef, &written) == mrhi_success &&
              mrhiDeclareBuffer(device, &argumentsDef, &arguments) == mrhi_success &&
              mrhiDeclareTexture(device, &imageDef, &image) == mrhi_success,
          "the buffers and the image");
    mrhiPassDef uploadDef = mrhiDefaultPassDef();
    // A declared buffer holds nothing defined until written, so the one
    // the skipped dispatch would write starts cleared.
    const mrhiAccess uploads[3] = {GuardedWhole(inputs, mrhi_accessCopyDestination),
                                   GuardedWhole(arguments, mrhi_accessCopyDestination),
                                   GuardedWhole(skipped, mrhi_accessCopyDestination)};
    uploadDef.accesses = uploads;
    uploadDef.accessCount = 3;
    uploadDef.neverCull = true;
    mrhiPassDef dispatchDef = mrhiDefaultPassDef();
    const mrhiAccess uses[5] = {GuardedWhole(inputs, mrhi_accessStorageRead),
                                GuardedWhole(skipped, mrhi_accessStorageReadWrite),
                                GuardedWhole(written, mrhi_accessStorageWrite),
                                GuardedWhole(image, mrhi_accessStorageWrite),
                                GuardedWhole(arguments, mrhi_accessIndirect)};
    dispatchDef.accesses = uses;
    dispatchDef.accessCount = 5;
    dispatchDef.neverCull = true;
    mrhiPassDef readDef = mrhiDefaultPassDef();
    const mrhiAccess reads[2] = {GuardedWhole(skipped, mrhi_accessCopySource),
                                 GuardedWhole(written, mrhi_accessCopySource)};
    readDef.accesses = reads;
    readDef.accessCount = 2;
    readDef.neverCull = true;
    mrhiPassId upload = {0};
    mrhiPassId dispatch = {0};
    mrhiPassId read = {0};
    CHECK(mrhiAddPass(device, &uploadDef, &upload) == mrhi_success &&
              mrhiAddPass(device, &dispatchDef, &dispatch) == mrhi_success &&
              mrhiAddPass(device, &readDef, &read) == mrhi_success &&
              mrhiDevice_CompileFrame(device) == mrhi_success,
          "the passes compiled");
    const uint32_t values[4] = {0, 1, 2, 3};
    const uint32_t counts[6] = {70000, 1, 1, 1, 1, 1};
    mrhiBinding bindings[3] = {
        {.slot = 0, .resource = inputs, .size = MRHI_WHOLE_SIZE},
        {.slot = 1, .resource = skipped, .size = MRHI_WHOLE_SIZE},
        {.slot = 2, .resource = image, .range = {.mipCount = 1, .layerCount = 1}},
    };
    mrhiRequestId results[2] = {{0}};
    CHECK(mrhiPass_Begin(device, upload) == mrhi_success &&
              mrhiPass_WriteBuffer(device, upload, inputs, 0, values, sizeof(values)) ==
                  mrhi_success &&
              mrhiPass_WriteBuffer(device, upload, arguments, 0, counts, sizeof(counts)) ==
                  mrhi_success &&
              mrhiPass_ClearBuffer(device, upload, skipped, 0, 16) == mrhi_success &&
              mrhiPass_End(device, upload) == mrhi_success,
          "the inputs and counts uploaded, the skipped outputs cleared");
    CHECK(mrhiPass_Begin(device, dispatch) == mrhi_success &&
              mrhiPass_SetComputePipeline(device, dispatch, pipeline) == mrhi_success &&
              mrhiPass_SetBindings(device, dispatch, 0, bindings, 3) == mrhi_success,
          "the pipeline and its bindings set");
    CHECK(mrhiPass_DispatchIndirect(device, dispatch, arguments, 0) == mrhi_success,
          "dispatched past the limit");
    bindings[1].resource = written;
    CHECK(mrhiPass_SetBindings(device, dispatch, 0, bindings, 3) == mrhi_success &&
              mrhiPass_DispatchIndirect(device, dispatch, arguments, 12) == mrhi_success &&
              mrhiPass_End(device, dispatch) == mrhi_success,
          "dispatched within it");
    CHECK(mrhiPass_Begin(device, read) == mrhi_success &&
              mrhiPass_ReadBuffer(device, read, skipped, 0, 16, &results[0]) == mrhi_success &&
              mrhiPass_ReadBuffer(device, read, written, 0, 16, &results[1]) == mrhi_success &&
              mrhiPass_End(device, read) == mrhi_success,
          "both outputs read");
    mrhiRequestId token = {0};
    CHECK(mrhiDevice_SubmitFrame(device, &token) == mrhi_success &&
              mrhiRequest_WaitFrame(device, token, UINT64_C(10000000000)) == mrhi_success,
          "submitted and finished");
    while (mrhiDevice_TakeNotification(device, &record) == mrhi_success)
    {
    }
    uint32_t got[2][4] = {{0}};
    size_t taken[2] = {0};
    for (int i = 0; i < 2; ++i)
    {
        CHECK(mrhiRequest_TakeReadback(device, results[i], got[i], sizeof(got[i]), &taken[i]) ==
                      mrhi_success &&
                  taken[i] == sizeof(got[i]),
              "a buffer read back");
    }
    const uint32_t none[4] = {0, 0, 0, 0};
    const uint32_t wanted[4] = {1, 3, 5, 7};
    CHECK(memcmp(got[0], none, sizeof(none)) == 0, "nothing written past the limit");
    CHECK(memcmp(got[1], wanted, sizeof(wanted)) == 0, "the dispatch after it written");
    CHECK(mrhiDestroyComputePipeline(device, pipeline) == mrhi_success &&
              mrhiDestroyShader(device, shader) == mrhi_success,
          "the pipeline and shader ended");
}

#endif
