// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Readbacks on a test driver device: copies into the device's readback
// ring, answered after their frame finishes and taken out once, rows
// tightly packed; records and ring bytes freed in order as the oldest
// are taken; a dropped frame's readbacks rolled back; and every refusal
// WebGPU and the ring's bounds call for.

#include "device_core.h"
#include "test_device_setup.h"

#include "maul-rhi/encoder.h"

#include <string.h>

enum
{
    BUFFER,
    ARRAY,
    DEPTH,
    STENCIL,
    MULTI,
    TARGET,
    OUTSIDE,
    RESOURCES,
};

static mrhiDevice* s_device;
static mrhiBufferId s_buffer;
static mrhiTextureId s_textures[RESOURCES];
static mrhiResourceId s_r[RESOURCES];
static mrhiPassId s_pass;
static mrhiPassId s_render;

static mrhiTextureId MakeTexture(mrhiTextureKind kind, mrhiFormat format, uint32_t layers,
                                 uint32_t samples)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.kind = kind;
    def.format = format;
    def.width = 16;
    def.height = 8;
    def.depthOrLayers = layers;
    def.sampleCount = samples;
    def.usage = mrhi_textureCopySource | (samples > 1 ? mrhi_textureRenderTarget : 0);
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(s_device, &def, &texture) == mrhi_success, "a texture");
    return texture;
}

// Opens a ready device with a readback ring of bytes and records, answer
// room, frames held until waited on, and the resources read: a 4 KiB
// buffer, an rgba8 array of 16 by 8 and 2 layers, depth32float and
// combined depth-stencil textures, a multisampled one and a target.
static void Open(uint32_t ringBytes, uint32_t readbacks, uint32_t notifications)
{
    s_adapter.holdFrames = true;
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.deviceLimits.readbackBytes = ringBytes;
    def.deviceLimits.readbacks = readbacks;
    // The answers asked for, and the record kept for the loss notice.
    def.deviceLimits.notifications = notifications + 1;
    s_device = OpenWith(def, true);
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 4096;
    bufferDef.usage = mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    CHECK(mrhiCreateBuffer(s_device, &bufferDef, &s_buffer) == mrhi_success, "a buffer");
    s_textures[ARRAY] = MakeTexture(mrhi_texture2dArray, mrhi_formatRgba8Unorm, 2, 1);
    s_textures[DEPTH] = MakeTexture(mrhi_texture2d, mrhi_formatDepth32Float, 1, 1);
    s_textures[STENCIL] = MakeTexture(mrhi_texture2d, mrhi_formatDepthStencil, 1, 1);
    s_textures[MULTI] = MakeTexture(mrhi_texture2d, mrhi_formatRgba8Unorm, 1, 4);
    s_textures[TARGET] = MakeTexture(mrhi_texture2d, mrhi_formatRgba8Unorm, 1, 4);
    s_textures[OUTSIDE] = MakeTexture(mrhi_texture2d, mrhi_formatRgba8Unorm, 1, 1);
}

static void CloseDevice(void)
{
    Close(s_device);
    s_device = nullptr;
}

// Opens a frame with a pass declaring every resource but the target and
// the one outside a copy source, and a render pass declaring them too,
// begun.
static void Frame(void)
{
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &frameDef) == mrhi_success, "begun");
    CHECK(mrhiImportBuffer(s_device, s_buffer, &s_r[BUFFER]) == mrhi_success, "imported");
    for (uint32_t i = ARRAY; i < RESOURCES; ++i)
    {
        CHECK(mrhiImportTexture(s_device, s_textures[i], &s_r[i]) == mrhi_success, "imported");
    }
    mrhiAccess accesses[TARGET];
    for (uint32_t i = 0; i < TARGET; ++i)
    {
        accesses[i] = (mrhiAccess){
            .resource = s_r[i],
            .kind = mrhi_accessCopySource,
            .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
        };
    }
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.neverCull = true;
    def.accesses = accesses;
    def.accessCount = TARGET;
    CHECK(mrhiAddPass(s_device, &def, &s_pass) == mrhi_success, "the pass");
    def.passClass = mrhi_passGraphics;
    def.colorTargets[0] = (mrhiColorTarget){.resource = s_r[TARGET], .load = mrhi_loadClear};
    def.colorTargetCount = 1;
    CHECK(mrhiAddPass(s_device, &def, &s_render) == mrhi_success, "a render pass");
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(mrhiBeginPass(s_device, s_pass) == mrhi_success &&
              mrhiBeginPass(s_device, s_render) == mrhi_success,
          "begun");
}

// Ends both passes and submits: the token.
static mrhiRequestId Submit(void)
{
    CHECK(mrhiEndPass(s_device, s_pass) == mrhi_success &&
              mrhiEndPass(s_device, s_render) == mrhi_success,
          "ended");
    mrhiRequestId token = {0};
    CHECK(mrhiSubmitFrame(s_device, &token) == mrhi_success, "submitted");
    return token;
}

// Finishes a frame and takes its answer and those of its readbacks, how
// many: whether each came in order with the outcome.
static bool Finish(mrhiRequestId token, const mrhiRequestId* readbacks, uint32_t count,
                   mrhiResult outcome)
{
    CHECK(mrhiWaitFrame(s_device, token, 1) == mrhi_success, "finished");
    mrhiDeviceNotification record;
    bool answered = mrhiNextDeviceNotification(s_device, &record) == mrhi_success &&
                    record.kind == mrhi_deviceFrameDone && record.request.index1 == token.index1;
    for (uint32_t i = 0; i < count; ++i)
    {
        answered = answered && mrhiNextDeviceNotification(s_device, &record) == mrhi_success &&
                   record.kind == mrhi_deviceReadbackReady &&
                   record.request.index1 == readbacks[i].index1 && record.outcome == outcome;
    }
    return answered && mrhiNextDeviceNotification(s_device, &record) == mrhi_empty;
}

static const mrhiCommand* Nth(uint32_t n)
{
    const mrhiFramePass* pass = &s_device->framePasses[s_pass.index1 - 1];
    const mrhiCommandChunk* chunk = &s_device->frameChunks[pass->firstChunk - 1];
    return pass->firstChunk != 0 && n < chunk->count ? &chunk->commands[n] : nullptr;
}

// Fills the ring's bytes from an offset with a pattern from a seed.
static void Fill(uint64_t offset, uint64_t bytes, uint8_t seed)
{
    for (uint64_t i = 0; i < bytes; ++i)
    {
        s_device->readbackRing[offset + i] = (uint8_t)(seed + i * 3);
    }
}

static mrhiResult Read(uint64_t offset, uint64_t size, mrhiRequestId* requestOut)
{
    return mrhiReadBuffer(s_device, s_pass, s_r[BUFFER], offset, size, requestOut);
}

static void TestBuffer(void)
{
    Open(4096, 8, 16);
    Frame();
    mrhiRequestId request = {0};
    CHECK(Read(8, 16, &request) == mrhi_success && request.index1 != 0, "read");
    const mrhiCommand* command = Nth(0);
    const mrhiCommandBufferSide* from = (const mrhiCommandBufferSide*)Nth(1);
    const mrhiCommandBufferSide* to = (const mrhiCommandBufferSide*)Nth(2);
    CHECK(command != nullptr && command->type == mrhiCommandReadBuffer && command->b == 16 &&
              from->object == s_r[BUFFER].index1 && from->offset == 8 && to->object == 0 &&
              to->offset == 0,
          "recorded as a copy into the ring");
    size_t size = 0;
    CHECK(mrhiTakeReadback(s_device, request, nullptr, 0, &size) == mrhi_errorState,
          "not answered while recording");
    mrhiRequestId token = Submit();
    CHECK(token.index1 == request.index1 + 1, "requests share one counter");
    CHECK(mrhiTakeReadback(s_device, request, nullptr, 0, &size) == mrhi_errorState,
          "nor while its frame runs");
    CHECK(Finish(token, &request, 1, mrhi_success), "answered after its frame");
    CHECK(mrhiTakeReadback(s_device, request, nullptr, 0, &size) == mrhi_success && size == 16,
          "its size");
    uint8_t bytes[32] = {0};
    CHECK(mrhiTakeReadback(s_device, request, bytes, 15, &size) == mrhi_errorCapacity && size == 16,
          "too little room, with the size");
    Fill(0, 16, 9);
    CHECK(mrhiTakeReadback(s_device, request, bytes, sizeof(bytes), &size) == mrhi_success &&
              size == 16 && bytes[0] == 9 && bytes[15] == (uint8_t)(9 + 45) && bytes[16] == 0,
          "taken");
    CHECK(mrhiTakeReadback(s_device, request, bytes, sizeof(bytes), &size) == mrhi_errorStale,
          "once");
    Frame();
    mrhiRequestId second = {0};
    CHECK(Read(0, 8, &second) == mrhi_success, "a second frame's");
    token = Submit();
    CHECK(Finish(token, &second, 1, mrhi_success), "answered from its own record");
    mrhiRequestId other = {second.index1, 2};
    CHECK(mrhiTakeReadback(s_device, other, nullptr, 0, &size) == mrhi_errorStale,
          "another generation");
    CHECK(mrhiTakeReadback(s_device, token, bytes, sizeof(bytes), &size) == mrhi_errorStale,
          "a frame's token is no readback");
    CHECK(mrhiTakeReadback(s_device, request, bytes, 4, nullptr) == mrhi_errorInvalid &&
              mrhiTakeReadback(s_device, request, nullptr, 4, &size) == mrhi_errorInvalid,
          "no size out, or no array with room");
    CHECK(mrhiTakeReadback(nullptr, request, bytes, 4, &size) == mrhi_errorInvalid, "no device");
    CHECK(mrhiGetDeviceMisuse(s_device) == 2, "each counted");
    CloseDevice();
}

static void TestRefusals(void)
{
    Open(4096, 8, 16);
    Frame();
    mrhiRequestId request = {0};
    CHECK(Read(2, 4, &request) == mrhi_errorInvalid && Read(0, 6, &request) == mrhi_errorInvalid,
          "not multiples of 4");
    CHECK(Read(4096, 4, &request) == mrhi_errorInvalid &&
              Read(4100, 0, &request) == mrhi_errorInvalid,
          "past the buffer");
    CHECK(Read(0, 4, nullptr) == mrhi_errorInvalid, "no request out");
    CHECK(mrhiReadBuffer(s_device, s_pass, s_r[ARRAY], 0, 4, &request) == mrhi_errorInvalid,
          "a texture");
    CHECK(mrhiReadBuffer(s_device, s_render, s_r[BUFFER], 0, 4, &request) == mrhi_errorInvalid,
          "a pass with targets");
    CHECK(mrhiGetDeviceMisuse(s_device) == 7, "each counted");
    mrhiResourceId none = {0};
    CHECK(mrhiReadBuffer(s_device, s_pass, none, 0, 4, &request) == mrhi_errorStale, "none");
    CHECK(mrhiReadBuffer(nullptr, s_pass, s_r[BUFFER], 0, 4, &request) == mrhi_errorInvalid,
          "no device");
    CHECK(s_device->readbackHead == 0, "none recorded");
    CHECK(mrhiEndPass(s_device, s_pass) == mrhi_success && Read(0, 4, &request) == mrhi_errorState,
          "not recording");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    CloseDevice();
    // A pass declaring the buffer a copy destination.
    Open(4096, 8, 16);
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &frameDef) == mrhi_success, "begun");
    CHECK(mrhiImportBuffer(s_device, s_buffer, &s_r[BUFFER]) == mrhi_success, "imported");
    mrhiAccess access = {.resource = s_r[BUFFER], .kind = mrhi_accessCopyDestination};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.neverCull = true;
    def.accesses = &access;
    def.accessCount = 1;
    CHECK(mrhiAddPass(s_device, &def, &s_pass) == mrhi_success &&
              mrhiCompileFrame(s_device) == mrhi_success &&
              mrhiBeginPass(s_device, s_pass) == mrhi_success,
          "a pass");
    CHECK(Read(0, 4, &request) == mrhi_errorInvalid, "a buffer declared a copy destination");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    CloseDevice();
}

static void TestTexture(void)
{
    Open(8192, 8, 16);
    Frame();
    mrhiTextureCopy source = {.resource = s_r[ARRAY], .z = 1};
    mrhiExtent3d size = {8, 8, 1};
    mrhiRequestId request = {0};
    CHECK(Read(0, 4, &request) == mrhi_success, "a buffer first");
    CHECK(mrhiReadTexture(s_device, s_pass, &source, &size, &request) == mrhi_success, "a layer");
    const mrhiCommand* command = Nth(3);
    const mrhiCommandTextureSide* from = (const mrhiCommandTextureSide*)Nth(4);
    const mrhiCommandBufferSide* to = (const mrhiCommandBufferSide*)Nth(5);
    CHECK(command != nullptr && command->type == mrhiCommandReadTexture && command->b == 8 &&
              command->c == 8 && from->object == s_r[ARRAY].index1 && from->z == 1 &&
              to->object == 0 && to->offset == 512 && to->bytesPerRow == 256 &&
              to->rowsPerImage == 8,
          "recorded into the ring at 512, rows at a pitch of 256");
    mrhiTextureCopy depth = {.resource = s_r[DEPTH]};
    mrhiRequestId depthRequest = {0};
    CHECK(mrhiReadTexture(s_device, s_pass, &depth, &(mrhiExtent3d){16, 8, 1}, &depthRequest) ==
              mrhi_success,
          "depth32float's depth");
    mrhiTextureCopy stencil = {.resource = s_r[STENCIL], .aspect = mrhi_aspectDepthOnly};
    CHECK(mrhiReadTexture(s_device, s_pass, &stencil, &(mrhiExtent3d){16, 8, 1}, &request) ==
              mrhi_errorInvalid,
          "not the combined format's depth");
    mrhiTextureCopy multi = {.resource = s_r[MULTI]};
    CHECK(mrhiReadTexture(s_device, s_pass, &multi, &(mrhiExtent3d){16, 8, 1}, &request) ==
              mrhi_errorInvalid,
          "not a multisampled texture");
    mrhiTextureCopy outside = {.resource = s_r[OUTSIDE]};
    CHECK(mrhiReadTexture(s_device, s_pass, &outside, &(mrhiExtent3d){16, 8, 1}, &request) ==
              mrhi_errorInvalid,
          "not a texture the pass does not declare");
    CHECK(mrhiReadTexture(s_device, s_pass, nullptr, &size, &request) == mrhi_errorInvalid &&
              mrhiReadTexture(s_device, s_pass, &source, nullptr, &request) == mrhi_errorInvalid &&
              mrhiReadTexture(s_device, s_pass, &source, &size, nullptr) == mrhi_errorInvalid,
          "NULL arguments");
    CHECK(mrhiReadTexture(s_device, s_render, &source, &size, &request) == mrhi_errorInvalid,
          "a pass with targets");
    CHECK(mrhiGetDeviceMisuse(s_device) == 7, "each counted");
    CHECK(mrhiReadTexture(nullptr, s_pass, &source, &size, &request) == mrhi_errorInvalid,
          "no device");
    mrhiTextureCopy stencilOnly = {.resource = s_r[STENCIL], .aspect = mrhi_aspectStencilOnly};
    mrhiRequestId stencilRequest = {0};
    CHECK(mrhiReadTexture(s_device, s_pass, &stencilOnly, &(mrhiExtent3d){16, 8, 1},
                          &stencilRequest) == mrhi_success,
          "the combined format's stencil");
    mrhiRequestId layer = {s_device->readbacks[1].request, 1};
    mrhiRequestId token = Submit();
    mrhiRequestId answers[] = {
        {s_device->readbacks[0].request, 1},
        layer,
        depthRequest,
        stencilRequest,
    };
    CHECK(Finish(token, answers, 4, mrhi_success), "all four answered in order");
    size_t taken = 0;
    CHECK(mrhiTakeReadback(s_device, stencilRequest, nullptr, 0, &taken) == mrhi_success &&
              taken == 128,
          "the stencil a byte a texel");
    for (uint32_t y = 0; y < 8; ++y)
    {
        Fill(512 + y * 256, 32, (uint8_t)(y * 16));
    }
    uint8_t bytes[512];
    CHECK(mrhiTakeReadback(s_device, layer, bytes, sizeof(bytes), &taken) == mrhi_success &&
              taken == 256,
          "taken tightly: 8 rows of 32 bytes");
    bool packed = true;
    for (uint32_t y = 0; y < 8; ++y)
    {
        packed = packed && bytes[y * 32] == (uint8_t)(y * 16) &&
                 bytes[y * 32 + 31] == (uint8_t)(y * 16 + 31 * 3);
    }
    CHECK(packed, "each row from its pitch");
    CloseDevice();
}

static void TestRing(void)
{
    Open(4096, 3, 16);
    Frame();
    mrhiRequestId a = {0};
    mrhiRequestId b = {0};
    mrhiRequestId c = {0};
    CHECK(Read(0, 2048, &a) == mrhi_success && Read(0, 1536, &b) == mrhi_success,
          "3.5 KiB of the ring");
    CHECK(Read(0, 1024, &c) == mrhi_errorCapacity, "not 1 KiB more");
    CHECK(Read(0, 512, &c) == mrhi_success, "512 bytes more");
    mrhiRequestId d = {0};
    CHECK(Read(0, 0, &d) == mrhi_errorCapacity, "not a fourth record");
    CHECK(mrhiGetDeviceMisuse(s_device) == 0, "not misuse");
    mrhiRequestId token = Submit();
    mrhiRequestId answers[] = {a, b, c};
    CHECK(Finish(token, answers, 3, mrhi_success), "answered");
    size_t size = 0;
    uint8_t bytes[2048];
    CHECK(mrhiTakeReadback(s_device, b, bytes, sizeof(bytes), &size) == mrhi_success,
          "the second taken first");
    CHECK(mrhiTakeReadback(s_device, b, bytes, sizeof(bytes), &size) == mrhi_errorStale,
          "once, though its room is held");
    Frame();
    CHECK(Read(0, 512, &d) == mrhi_errorCapacity, "its room held behind the first");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    CHECK(mrhiTakeReadback(s_device, a, bytes, sizeof(bytes), &size) == mrhi_success,
          "the first taken");
    Frame();
    CHECK(Read(0, 2048, &d) == mrhi_success, "both freed");
    CHECK(((const mrhiCommandBufferSide*)Nth(2))->offset == 0,
          "past the ring's end, back at its start");
    mrhiRequestId e = {0};
    CHECK(Read(0, 1024, &e) == mrhi_success, "the rest");
    token = Submit();
    mrhiRequestId more[] = {d, e};
    CHECK(Finish(token, more, 2, mrhi_success), "answered");
    CHECK(mrhiTakeReadback(s_device, c, bytes, sizeof(bytes), &size) == mrhi_success &&
              mrhiTakeReadback(s_device, d, bytes, sizeof(bytes), &size) == mrhi_success &&
              mrhiTakeReadback(s_device, e, bytes, sizeof(bytes), &size) == mrhi_success,
          "all taken");
    CHECK(s_device->readbackTail == s_device->readbackHead &&
              s_device->ringTail == s_device->ringHead,
          "the ring empty");
    CloseDevice();
}

static void TestDropAndFailure(void)
{
    Open(4096, 4, 16);
    Frame();
    mrhiRequestId dropped = {0};
    CHECK(Read(0, 4096, &dropped) == mrhi_success, "the whole ring");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    size_t size = 0;
    CHECK(mrhiTakeReadback(s_device, dropped, nullptr, 0, &size) == mrhi_errorStale,
          "its readback gone");
    CHECK(s_device->readbackPending == 0 && s_device->readbackHead == 0 && s_device->ringHead == 0,
          "rolled back");
    Frame();
    mrhiRequestId request = {0};
    CHECK(Read(0, 4096, &request) == mrhi_success, "the whole ring again");
    mrhiRequestId token = Submit();
    CloseDevice();
    // A frame the driver fails answers its readbacks with its failure.
    s_adapter.frameOutcome = mrhi_errorPlatform;
    Open(4096, 4, 16);
    s_adapter.frameOutcome = mrhi_errorPlatform;
    Frame();
    CHECK(Read(0, 16, &request) == mrhi_success, "read");
    token = Submit();
    CHECK(Finish(token, &request, 1, mrhi_errorPlatform), "answered with the failure");
    uint8_t bytes[16];
    CHECK(mrhiTakeReadback(s_device, request, bytes, sizeof(bytes), &size) == mrhi_errorPlatform,
          "taken as the failure");
    CHECK(mrhiTakeReadback(s_device, request, bytes, sizeof(bytes), &size) == mrhi_errorStale &&
              s_device->ringTail == s_device->ringHead,
          "and freed");
    CloseDevice();
}

static void TestAnswerRoom(void)
{
    // Room for four answers: a readback needs one for itself and one for
    // its frame.
    Open(4096, 8, 4);
    Frame();
    mrhiRequestId requests[4];
    CHECK(Read(0, 4, &requests[0]) == mrhi_success && Read(0, 4, &requests[1]) == mrhi_success &&
              Read(0, 4, &requests[2]) == mrhi_success,
          "three readbacks");
    CHECK(Read(0, 4, &requests[3]) == mrhi_errorCapacity, "not four");
    mrhiRequestId token = Submit();
    CHECK(Finish(token, requests, 3, mrhi_success), "all answered");
    Frame();
    CHECK(Read(0, 4, &requests[0]) == mrhi_success && Read(0, 4, &requests[1]) == mrhi_success &&
              Read(0, 4, &requests[2]) == mrhi_success,
          "the room back once they are answered");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    CloseDevice();
    Open(0, 0, 16);
    Frame();
    mrhiRequestId request = {0};
    CHECK(Read(0, 4, &request) == mrhi_errorCapacity, "no readbacks on a device without them");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    CloseDevice();
    Open(0, 4, 16);
    Frame();
    CHECK(Read(0, 0, &request) == mrhi_success, "no bytes without a ring");
    CHECK(Read(0, 4, &request) == mrhi_errorCapacity, "but no more");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    CloseDevice();
}

// A readback that would pass the ring's end starts at its start.
static void TestWrap(void)
{
    Open(4096, 8, 16);
    Frame();
    mrhiRequestId first = {0};
    CHECK(Read(0, 3584, &first) == mrhi_success, "most of the ring");
    mrhiRequestId token = Submit();
    CHECK(Finish(token, &first, 1, mrhi_success), "answered");
    uint8_t bytes[4096];
    size_t size = 0;
    CHECK(mrhiTakeReadback(s_device, first, bytes, sizeof(bytes), &size) == mrhi_success, "taken");
    Frame();
    mrhiRequestId second = {0};
    CHECK(Read(0, 1024, &second) == mrhi_success, "a kilobyte past what is left");
    CHECK(((const mrhiCommandBufferSide*)Nth(2))->offset == 0 && s_device->ringHead == 4096 + 1024,
          "placed at the ring's start");
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    CloseDevice();
}

// Frames finishing out of order answer their own readbacks.
static void TestOutOfOrder(void)
{
    Open(4096, 8, 16);
    Frame();
    mrhiRequestId a = {0};
    CHECK(Read(0, 4, &a) == mrhi_success, "the first frame's");
    mrhiRequestId first = Submit();
    Frame();
    mrhiRequestId b[2] = {0};
    CHECK(Read(0, 4, &b[0]) == mrhi_success && Read(0, 4, &b[1]) == mrhi_success,
          "the second frame's two");
    mrhiRequestId second = Submit();
    CHECK(Finish(first, &a, 1, mrhi_success), "the first finished first");
    CHECK(Finish(second, b, 2, mrhi_success), "the second, then");
    CloseDevice();
    Open(4096, 8, 16);
    Frame();
    CHECK(Read(0, 4, &a) == mrhi_success, "the first frame's");
    first = Submit();
    Frame();
    CHECK(Read(0, 4, &b[0]) == mrhi_success, "the second frame's");
    second = Submit();
    CHECK(Finish(second, b, 1, mrhi_success), "the second finished first");
    CHECK(Finish(first, &a, 1, mrhi_success), "the first, then");
    CloseDevice();
}

// A submission the driver refuses drops its readbacks.
static void TestSubmitFailure(void)
{
    // The buffer and six textures, then the submission fails.
    s_adapter.objectsBeforeFailure = 7;
    Open(4096, 8, 16);
    s_adapter.objectsBeforeFailure = 7;
    Frame();
    mrhiRequestId request = {0};
    CHECK(Read(0, 4096, &request) == mrhi_success, "the whole ring");
    CHECK(mrhiEndPass(s_device, s_pass) == mrhi_success &&
              mrhiEndPass(s_device, s_render) == mrhi_success,
          "ended");
    mrhiRequestId token = {0};
    CHECK(mrhiSubmitFrame(s_device, &token) == mrhi_errorPlatform, "refused by the driver");
    size_t size = 0;
    CHECK(mrhiTakeReadback(s_device, request, nullptr, 0, &size) == mrhi_errorStale &&
              s_device->readbackPending == 0 && s_device->ringHead == 0,
          "its readback dropped");
    CloseDevice();
}

static void TestLimits(void)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    CHECK(def.deviceLimits.readbackBytes == 1u << 20 && def.deviceLimits.readbacks == 64,
          "1 MiB and 64 readbacks by default");
}

int main(void)
{
    ResetAdapter();
    TestBuffer();
    TestRefusals();
    TestTexture();
    TestRing();
    TestDropAndFailure();
    TestAnswerRoom();
    TestWrap();
    TestOutOfOrder();
    TestSubmitFailure();
    TestLimits();
    return s_failures == 0 ? 0 : 1;
}
