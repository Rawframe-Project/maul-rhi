// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Submitting frames on a test driver device: the view the driver walks
// holds the compiled frame's kept passes, labels, barriers, resources,
// commands and uploads; and an imported object destroyed while the frame
// is open keeps its snapshot, its slot's next object untouched.

#include "device_core.h"
#include "test_device_setup.h"

#include "maul-rhi/encoder.h"

// A marker whose label takes two records.
#define MARKER "a marker over 32 bytes, two records long"

static mrhiTestFrameLog s_log;
static mrhiDevice* s_device;
static mrhiTextureId s_target;
static mrhiBufferId s_buffer;

static void Open(void)
{
    s_log = (mrhiTestFrameLog){0};
    s_adapter.frameLog = &s_log;
    s_adapter.features.timestampQuery = true;
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.features.timestampQuery = true;
    s_device = OpenWith(deviceDef, true);
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = 16;
    textureDef.height = 8;
    textureDef.usage = mrhi_textureRenderTarget;
    CHECK(mrhiCreateTexture(s_device, &textureDef, &s_target) == mrhi_success, "a target");
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 256;
    bufferDef.usage = mrhi_bufferCopyDestination;
    CHECK(mrhiCreateBuffer(s_device, &bufferDef, &s_buffer) == mrhi_success, "a buffer");
}

static void CloseDevice(void)
{
    Close(s_device);
    s_adapter.frameLog = nullptr;
}

static mrhiRequestId Submit(void)
{
    mrhiRequestId token = {0};
    CHECK(mrhiSubmitFrame(s_device, &token) == mrhi_success, "submitted");
    return token;
}

// A frame of four passes: a labeled render pass that clears the target
// and is never begun; a labeled pass uploading into the buffer; a pass
// writing a transient buffer nothing reads, culled; and one writing a
// transient texture that a kept pass then reads.
static void TestView(void)
{
    // Frames finish only when waited on, so the first still runs when the
    // second begins.
    s_adapter.holdFrames = true;
    Open();
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &frameDef) == mrhi_success, "begun");
    mrhiResourceId target = {0};
    mrhiResourceId buffer = {0};
    mrhiResourceId unread = {0};
    mrhiResourceId image = {0};
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 64;
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = 4;
    textureDef.height = 4;
    CHECK(mrhiImportTexture(s_device, s_target, &target) == mrhi_success &&
              mrhiImportBuffer(s_device, s_buffer, &buffer) == mrhi_success &&
              mrhiDeclareBuffer(s_device, &bufferDef, &unread) == mrhi_success &&
              mrhiDeclareTexture(s_device, &textureDef, &image) == mrhi_success,
          "four resources");
    mrhiQuerySetDef setDef = mrhiDefaultQuerySetDef();
    mrhiQuerySetId occlusion;
    mrhiQuerySetId timestamps;
    CHECK(mrhiCreateQuerySet(s_device, &setDef, &occlusion) == mrhi_success, "occlusion");
    setDef.type = mrhi_queryTimestamp;
    setDef.count = 2;
    CHECK(mrhiCreateQuerySet(s_device, &setDef, &timestamps) == mrhi_success, "timestamps");
    mrhiPassDef def = mrhiDefaultPassDef();
    def.occlusionQuerySet = occlusion;
    def.label = "draw";
    def.labelLength = 4;
    def.colorTargets[0] = (mrhiColorTarget){.resource = target, .load = mrhi_loadClear};
    def.colorTargetCount = 1;
    mrhiPassId draw;
    CHECK(mrhiAddPass(s_device, &def, &draw) == mrhi_success, "the render pass");
    mrhiAccess access = {.resource = buffer, .kind = mrhi_accessCopyDestination};
    def = mrhiDefaultPassDef();
    def.label = "upload";
    def.labelLength = 6;
    def.accesses = &access;
    def.accessCount = 1;
    def.timestampQuerySet = timestamps;
    def.timestampBegin = 0;
    def.timestampEnd = 1;
    mrhiPassId upload;
    CHECK(mrhiAddPass(s_device, &def, &upload) == mrhi_success, "the upload");
    access = (mrhiAccess){.resource = unread, .kind = mrhi_accessCopyDestination};
    def.label = nullptr;
    def.labelLength = 0;
    def.timestampQuerySet = (mrhiQuerySetId){0};
    def.timestampBegin = MRHI_NO_QUERY;
    def.timestampEnd = MRHI_NO_QUERY;
    mrhiPassId culled;
    CHECK(mrhiAddPass(s_device, &def, &culled) == mrhi_success, "a pass nothing needs");
    def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){.resource = image, .load = mrhi_loadClear};
    def.colorTargetCount = 1;
    mrhiPassId painting;
    CHECK(mrhiAddPass(s_device, &def, &painting) == mrhi_success, "a transient painted");
    access = (mrhiAccess){
        .resource = image,
        .kind = mrhi_accessSampled,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
    def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.accesses = &access;
    def.accessCount = 1;
    mrhiPassId sampling;
    CHECK(mrhiAddPass(s_device, &def, &sampling) == mrhi_success, "and sampled");
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    bool kept = false;
    CHECK(mrhiIsPassKept(s_device, culled, &kept) == mrhi_success && !kept, "one culled");
    size_t barriers = 0;
    uint64_t memory = 0;
    CHECK(mrhiGetFrameBarriers(s_device, nullptr, 0, &barriers) == mrhi_success &&
              mrhiGetFrameMemory(s_device, &memory) == mrhi_success && memory > 0,
          "the plan");
    uint32_t bytes[4] = {1, 2, 3, 4};
    mrhiClearColor color = {0};
    CHECK(mrhiBeginPass(s_device, upload) == mrhi_success &&
              mrhiWriteBuffer(s_device, upload, buffer, 0, bytes, sizeof(bytes)) == mrhi_success &&
              mrhiInsertDebugMarker(s_device, upload, MARKER, sizeof(MARKER) - 1) == mrhi_success &&
              mrhiEndPass(s_device, upload) == mrhi_success,
          "the upload recorded");
    CHECK(mrhiBeginPass(s_device, painting) == mrhi_success &&
              mrhiSetBlendConstant(s_device, painting, &color) == mrhi_success,
          "the painting recorded, left open to end");
    CHECK(mrhiEndPass(s_device, painting) == mrhi_success, "ended");
    CHECK(s_device->framePasses[draw.index1 - 1].width == 16 &&
              s_device->framePasses[draw.index1 - 1].height == 8,
          "a pass never begun measured");
    mrhiRequestId first = Submit();
    CHECK(s_log.frames == 1 && s_log.passes == 4 && s_log.labeled == 2 && s_log.labelBytes == 10,
          "the kept passes and their labels");
    CHECK(s_log.barriers == barriers && s_log.memoryBytes == memory, "the plan's barriers");
    CHECK(s_log.resources == 4 && s_log.needed == 3 && s_log.transients == 2,
          "four resources, the culled pass's not needed");
    // An upload, then a marker with two label records, then a blend
    // constant with its color.
    CHECK(s_log.commands == 3 && s_log.records == 3 + 2 + 2 + 1 && s_log.chunks == 2,
          "every command walked");
    CHECK(s_log.stagingBytes == 512 && s_log.uploadSum == 1 + 2 + 3 + 4,
          "one upload at a 512-byte boundary, its bytes");
    CHECK(s_log.occlusionPasses == 1 && s_log.timestampPasses == 1, "the passes' query sets");
    // The first frame still runs, so the next stages into the other region.
    CHECK(mrhiBeginFrame(s_device, &frameDef) == mrhi_success, "another frame");
    CHECK(s_device->stagingRegion == 1, "the second region");
    CHECK(mrhiImportBuffer(s_device, s_buffer, &buffer) == mrhi_success, "imported");
    access = (mrhiAccess){.resource = buffer, .kind = mrhi_accessCopyDestination};
    def = mrhiDefaultPassDef();
    def.accesses = &access;
    def.accessCount = 1;
    uint32_t more[2] = {5, 6};
    CHECK(mrhiAddPass(s_device, &def, &upload) == mrhi_success &&
              mrhiCompileFrame(s_device) == mrhi_success &&
              mrhiBeginPass(s_device, upload) == mrhi_success &&
              mrhiWriteBuffer(s_device, upload, buffer, 0, more, sizeof(more)) == mrhi_success &&
              mrhiEndPass(s_device, upload) == mrhi_success,
          "an upload");
    mrhiRequestId second = Submit();
    CHECK(mrhiWaitFrame(s_device, first, 1) == mrhi_success &&
              mrhiWaitFrame(s_device, second, 1) == mrhi_success,
          "both finished");
    s_adapter.holdFrames = false;
    CHECK(s_log.frames == 2 && s_log.passes == 1 && s_log.commands == 1 &&
              s_log.uploadSum == 5 + 6 && s_log.occlusionPasses == 0,
          "read from the frame's own region");
    CloseDevice();
}

// An imported buffer destroyed while its frame is open: the frame keeps
// its handle and state, and the object that takes its slot keeps its own
// state when the frame is submitted.
static void TestDestroyedImport(void)
{
    Open();
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &frameDef) == mrhi_success, "begun");
    mrhiResourceId buffer = {0};
    CHECK(mrhiImportBuffer(s_device, s_buffer, &buffer) == mrhi_success, "imported");
    uint64_t handle = s_device->bufferSlots[s_buffer.index1 - 1].handle;
    mrhiAccess access = {.resource = buffer, .kind = mrhi_accessCopyDestination};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.accesses = &access;
    def.accessCount = 1;
    mrhiPassId pass;
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success, "written");
    CHECK(mrhiDestroyBuffer(s_device, s_buffer) == mrhi_success, "destroyed");
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 1024;
    bufferDef.usage = mrhi_bufferVertex;
    mrhiBufferId next;
    CHECK(mrhiCreateBuffer(s_device, &bufferDef, &next) == mrhi_success &&
              next.index1 == s_buffer.index1,
          "its slot taken again");
    const mrhiFrameResource* resource = &s_device->frameResources[buffer.index1 - 1];
    CHECK(resource->handle == handle && resource->size == 256 &&
              s_device->bufferSlots[next.index1 - 1].handle != handle,
          "the frame keeps what it imported");
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    Submit();
    CHECK(s_log.frames == 1 && s_log.passes == 1, "the pass ran");
    CHECK(s_device->bufferSlots[next.index1 - 1].state == mrhi_stateUndefined,
          "the new buffer's state untouched");
    CloseDevice();
}

// The same for a texture drawn into.
static void TestDestroyedTexture(void)
{
    Open();
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &frameDef) == mrhi_success, "begun");
    mrhiResourceId target = {0};
    CHECK(mrhiImportTexture(s_device, s_target, &target) == mrhi_success, "imported");
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){.resource = target, .load = mrhi_loadClear};
    def.colorTargetCount = 1;
    mrhiPassId pass;
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success, "drawn into");
    CHECK(mrhiDestroyTexture(s_device, s_target) == mrhi_success, "destroyed");
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = 2;
    textureDef.height = 2;
    textureDef.usage = mrhi_textureSampled;
    mrhiTextureId next;
    CHECK(mrhiCreateTexture(s_device, &textureDef, &next) == mrhi_success &&
              next.index1 == s_target.index1,
          "its slot taken again");
    CHECK(s_device->frameResources[target.index1 - 1].texture.width == 16,
          "the frame keeps the def it imported");
    Submit();
    CHECK(s_log.frames == 1 && s_log.passes == 1, "the pass ran");
    CHECK(s_device->textureSlots[next.index1 - 1].state == mrhi_stateUndefined,
          "the new texture's state untouched");
    CloseDevice();
}

int main(void)
{
    ResetAdapter();
    TestView();
    TestDestroyedImport();
    TestDestroyedTexture();
    return s_failures == 0 ? 0 : 1;
}
