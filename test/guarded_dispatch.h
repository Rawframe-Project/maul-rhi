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
#include "shaders/lone_table_container.h"
#include "shaders/split_tables_container.h"
#include "shaders/storage_container.h"

#include <string.h>

// A pipeline of a container's compute entry "cs", and its shader.
static mrhiComputePipelineId GuardedPipeline(mrhiDevice* device, const uint8_t* bytes,
                                             size_t byteCount, mrhiShaderId* shaderOut)
{
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = bytes;
    shaderDef.byteCount = byteCount;
    CHECK(mrhiCreateShader(device, &shaderDef, shaderOut) == mrhi_success, "a shader");
    mrhiComputePipelineDef pipelineDef = mrhiDefaultComputePipelineDef();
    pipelineDef.shader = *shaderOut;
    pipelineDef.entry = "cs";
    pipelineDef.entryLength = 2;
    mrhiComputePipelineId pipeline = {0};
    mrhiRequestId request = {0};
    mrhiDeviceNotification record;
    CHECK(mrhiCreateComputePipeline(device, &pipelineDef, &pipeline, &request) == mrhi_success &&
              mrhiDevice_TakeNotification(device, &record) == mrhi_success &&
              record.kind == mrhi_devicePipelineReady && record.outcome == mrhi_success,
          "its pipeline");
    return pipeline;
}

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
    mrhiShaderId shader = {0};
    mrhiComputePipelineId pipeline =
        GuardedPipeline(device, s_storageContainer, sizeof(s_storageContainer), &shader);
    mrhiDeviceNotification record;
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

// A pass's tables outliving its pipeline: the lone_table container's
// dispatch binds table 1, then the split_tables container's indirect
// dispatch binds tables 0 and 2, where its layout has an empty table 1.
// The guard's kernel disturbs the pass's tables, and the driver binds
// again only those the split_tables pipeline reads, never the lone
// table's set where it has none, which the validation layers would
// refuse. Both write: 1 and the input plus one.
static void CheckGuardedTables(mrhiDevice* device)
{
    mrhiShaderId shaders[2] = {{0}};
    mrhiComputePipelineId lone =
        GuardedPipeline(device, s_lone_tableContainer, sizeof(s_lone_tableContainer), &shaders[0]);
    mrhiComputePipelineId split = GuardedPipeline(device, s_split_tablesContainer,
                                                  sizeof(s_split_tablesContainer), &shaders[1]);
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success, "a frame");
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 16;
    mrhiResourceId input = {0};
    mrhiResourceId lonely = {0};
    mrhiResourceId output = {0};
    mrhiResourceId arguments = {0};
    CHECK(mrhiDeclareBuffer(device, &bufferDef, &input) == mrhi_success &&
              mrhiDeclareBuffer(device, &bufferDef, &lonely) == mrhi_success &&
              mrhiDeclareBuffer(device, &bufferDef, &output) == mrhi_success &&
              mrhiDeclareBuffer(device, &bufferDef, &arguments) == mrhi_success,
          "the buffers");
    mrhiPassDef uploadDef = mrhiDefaultPassDef();
    const mrhiAccess uploads[2] = {GuardedWhole(input, mrhi_accessCopyDestination),
                                   GuardedWhole(arguments, mrhi_accessCopyDestination)};
    uploadDef.accesses = uploads;
    uploadDef.accessCount = 2;
    uploadDef.neverCull = true;
    mrhiPassDef dispatchDef = mrhiDefaultPassDef();
    const mrhiAccess uses[4] = {GuardedWhole(input, mrhi_accessStorageRead),
                                GuardedWhole(lonely, mrhi_accessStorageWrite),
                                GuardedWhole(output, mrhi_accessStorageWrite),
                                GuardedWhole(arguments, mrhi_accessIndirect)};
    dispatchDef.accesses = uses;
    dispatchDef.accessCount = 4;
    dispatchDef.neverCull = true;
    mrhiPassDef readDef = mrhiDefaultPassDef();
    const mrhiAccess reads[2] = {GuardedWhole(lonely, mrhi_accessCopySource),
                                 GuardedWhole(output, mrhi_accessCopySource)};
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
    const uint32_t value[4] = {41, 0, 0, 0};
    const uint32_t counts[4] = {1, 1, 1, 0};
    const mrhiBinding lonelyBinding = {.slot = 0, .resource = lonely, .size = MRHI_WHOLE_SIZE};
    const mrhiBinding inputBinding = {.slot = 0, .resource = input, .size = MRHI_WHOLE_SIZE};
    const mrhiBinding outputBinding = {.slot = 0, .resource = output, .size = MRHI_WHOLE_SIZE};
    mrhiRequestId results[2] = {{0}};
    CHECK(mrhiPass_Begin(device, upload) == mrhi_success &&
              mrhiPass_WriteBuffer(device, upload, input, 0, value, sizeof(value)) ==
                  mrhi_success &&
              mrhiPass_WriteBuffer(device, upload, arguments, 0, counts, sizeof(counts)) ==
                  mrhi_success &&
              mrhiPass_End(device, upload) == mrhi_success,
          "the input and counts uploaded");
    CHECK(mrhiPass_Begin(device, dispatch) == mrhi_success &&
              mrhiPass_SetComputePipeline(device, dispatch, lone) == mrhi_success &&
              mrhiPass_SetBindings(device, dispatch, 1, &lonelyBinding, 1) == mrhi_success &&
              mrhiPass_Dispatch(device, dispatch, 1, 1, 1) == mrhi_success,
          "table 1 bound and dispatched");
    CHECK(mrhiPass_SetComputePipeline(device, dispatch, split) == mrhi_success &&
              mrhiPass_SetBindings(device, dispatch, 0, &inputBinding, 1) == mrhi_success &&
              mrhiPass_SetBindings(device, dispatch, 2, &outputBinding, 1) == mrhi_success &&
              mrhiPass_DispatchIndirect(device, dispatch, arguments, 0) == mrhi_success &&
              mrhiPass_End(device, dispatch) == mrhi_success,
          "tables 0 and 2 bound and dispatched indirectly");
    CHECK(mrhiPass_Begin(device, read) == mrhi_success &&
              mrhiPass_ReadBuffer(device, read, lonely, 0, 4, &results[0]) == mrhi_success &&
              mrhiPass_ReadBuffer(device, read, output, 0, 4, &results[1]) == mrhi_success &&
              mrhiPass_End(device, read) == mrhi_success,
          "both outputs read");
    mrhiRequestId token = {0};
    CHECK(mrhiDevice_SubmitFrame(device, &token) == mrhi_success &&
              mrhiRequest_WaitFrame(device, token, UINT64_C(10000000000)) == mrhi_success,
          "submitted and finished");
    mrhiDeviceNotification record;
    while (mrhiDevice_TakeNotification(device, &record) == mrhi_success)
    {
    }
    uint32_t got[2] = {0};
    size_t taken = 0;
    for (int i = 0; i < 2; ++i)
    {
        CHECK(mrhiRequest_TakeReadback(device, results[i], &got[i], sizeof(got[i]), &taken) ==
                      mrhi_success &&
                  taken == sizeof(got[i]),
              "a word read back");
    }
    CHECK(got[0] == 1, "the lone table's dispatch written");
    CHECK(got[1] == 42, "the indirect dispatch through tables 0 and 2 written");
    CHECK(mrhiDestroyComputePipeline(device, lone) == mrhi_success &&
              mrhiDestroyComputePipeline(device, split) == mrhi_success &&
              mrhiDestroyShader(device, shaders[0]) == mrhi_success &&
              mrhiDestroyShader(device, shaders[1]) == mrhi_success,
          "the pipelines and shaders ended");
}

#endif
