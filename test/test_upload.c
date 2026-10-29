// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Uploads on a test driver device: the program's bytes copied at the
// call into the frame's staging, placed at 512-byte boundaries with a
// texture's rows at a 256-byte pitch, checked as WebGPU checks writes,
// and a full staging refusing the frame. Each running frame keeps its
// own staging region, whatever order frames finish in.

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
    BLOCKS,
    WIDE,
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
static uint8_t s_bytes[8192];

static mrhiTextureId MakeTexture(mrhiTextureKind kind, mrhiFormat format, uint32_t width,
                                 uint32_t height, uint32_t layers, uint32_t samples)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.kind = kind;
    def.format = format;
    def.width = width;
    def.height = height;
    def.depthOrLayers = layers;
    def.sampleCount = samples;
    def.usage = mrhi_textureCopyDestination | (samples > 1 ? mrhi_textureRenderTarget : 0);
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(s_device, &def, &texture) == mrhi_success, "a texture");
    return texture;
}

// Opens a ready device with BC formats, the given staging and frames
// held until waited on, and the resources uploads use: a 1 KiB buffer,
// an rgba8 array of 16 by 8 and 3 layers, depth32float and combined
// depth-stencil textures, a multisampled one, a BC1 texture of 8 by 8,
// and a render target.
static void Open(uint32_t uploadBytes)
{
    for (uint32_t i = 0; i < sizeof(s_bytes); ++i)
    {
        s_bytes[i] = (uint8_t)(i * 7 + 1);
    }
    s_adapter.features.textureCompressionBc = true;
    s_adapter.holdFrames = true;
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.features.textureCompressionBc = true;
    def.budgets.frameUploadBytes = uploadBytes;
    s_device = OpenWith(def, true);
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 1024;
    bufferDef.usage = mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    CHECK(mrhiCreateBuffer(s_device, &bufferDef, &s_buffer) == mrhi_success, "a buffer");
    s_textures[ARRAY] = MakeTexture(mrhi_texture2dArray, mrhi_formatRgba8Unorm, 16, 8, 3, 1);
    s_textures[DEPTH] = MakeTexture(mrhi_texture2d, mrhi_formatDepth32Float, 8, 8, 1, 1);
    s_textures[STENCIL] = MakeTexture(mrhi_texture2d, mrhi_formatDepthStencil, 8, 8, 1, 1);
    s_textures[MULTI] = MakeTexture(mrhi_texture2d, mrhi_formatRgba8Unorm, 8, 8, 1, 4);
    s_textures[BLOCKS] = MakeTexture(mrhi_texture2d, mrhi_formatBc1RgbaUnorm, 8, 8, 1, 1);
    s_textures[WIDE] = MakeTexture(mrhi_texture2d, mrhi_formatRgba8Unorm, 64, 2, 1, 1);
    s_textures[TARGET] = MakeTexture(mrhi_texture2d, mrhi_formatRgba8Unorm, 8, 8, 1, 4);
    s_textures[OUTSIDE] = MakeTexture(mrhi_texture2d, mrhi_formatRgba8Unorm, 8, 8, 1, 1);
}

static void CloseDevice(void)
{
    Close(s_device);
    s_device = nullptr;
}

// Opens a frame with a pass declaring every resource but the target and
// the one outside a copy destination, and a render pass declaring them
// too, begun.
static void Frame(void)
{
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frameDef) == mrhi_success, "begun");
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
            .kind = mrhi_accessCopyDestination,
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
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(mrhiPass_Begin(s_device, s_pass) == mrhi_success &&
              mrhiPass_Begin(s_device, s_render) == mrhi_success,
          "begun");
}

// Ends both passes and submits: the token.
static mrhiRequestId Submit(void)
{
    CHECK(mrhiPass_End(s_device, s_pass) == mrhi_success &&
              mrhiPass_End(s_device, s_render) == mrhi_success,
          "ended");
    mrhiRequestId token = {0};
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_success, "submitted");
    return token;
}

static void Drop(void)
{
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success, "dropped");
}

static const mrhiCommand* Nth(uint32_t n)
{
    const mrhiFramePass* pass = &s_device->framePasses[s_pass.index1 - 1];
    const mrhiCommandChunk* chunk = &s_device->frameChunks[pass->firstChunk - 1];
    return pass->firstChunk != 0 && n < chunk->count ? &chunk->commands[n] : nullptr;
}

// The open frame's staging at an offset.
static const uint8_t* Staged(uint64_t offset)
{
    return s_device->frameStaging +
           (size_t)s_device->stagingRegion * s_device->budgets.frameUploadBytes + offset;
}

static void TestBuffers(void)
{
    Open(4096);
    Frame();
    mrhiResourceId buffer = s_r[BUFFER];
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, buffer, 8, s_bytes, 12) == mrhi_success,
          "written");
    const mrhiCommand* command = Nth(0);
    const mrhiCommandBufferSide* from = (const mrhiCommandBufferSide*)Nth(1);
    const mrhiCommandBufferSide* to = (const mrhiCommandBufferSide*)Nth(2);
    CHECK(command != nullptr && command->type == mrhiCommandWriteBuffer && command->b == 12 &&
              from->object == 0 && from->offset == 0 && to->object == buffer.index1 &&
              to->offset == 8,
          "recorded as a copy from staging");
    CHECK(memcmp(Staged(0), s_bytes, 12) == 0, "the bytes staged at the call");
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, buffer, 1020, s_bytes + 4, 4) == mrhi_success,
          "the last word");
    CHECK(((const mrhiCommandBufferSide*)Nth(4))->offset == 512 &&
              memcmp(Staged(512), s_bytes + 4, 4) == 0,
          "at the next 512 bytes");
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, buffer, 0, nullptr, 0) == mrhi_success, "nothing");
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, buffer, 2, s_bytes, 4) == mrhi_errorInvalid &&
              mrhiPass_WriteBuffer(s_device, s_pass, buffer, 0, s_bytes, 6) == mrhi_errorInvalid,
          "not multiples of 4");
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, buffer, 1024, s_bytes, 4) == mrhi_errorInvalid &&
              mrhiPass_WriteBuffer(s_device, s_pass, buffer, 1028, s_bytes, 0) == mrhi_errorInvalid,
          "past the buffer");
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, s_r[ARRAY], 0, s_bytes, 4) == mrhi_errorInvalid,
          "a texture");
    CHECK(mrhiPass_WriteBuffer(s_device, s_render, buffer, 0, s_bytes, 4) == mrhi_errorInvalid,
          "a pass with targets");
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, buffer, 0, nullptr, 4) == mrhi_errorInvalid,
          "no bytes");
    CHECK(mrhiDevice_GetMisuse(s_device) == 7, "each counted");
    mrhiResourceId none = {0};
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, none, 0, s_bytes, 4) == mrhi_errorStale, "none");
    CHECK(mrhiPass_WriteBuffer(nullptr, s_pass, buffer, 0, s_bytes, 4) == mrhi_errorInvalid,
          "no device");
    Submit();
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, buffer, 0, s_bytes, 4) == mrhi_errorState,
          "no frame");
    CloseDevice();
    // A pass declaring the buffer a copy source.
    Open(4096);
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frameDef) == mrhi_success, "begun");
    mrhiResourceId source = {0};
    CHECK(mrhiImportBuffer(s_device, s_buffer, &source) == mrhi_success, "imported");
    mrhiAccess access = {.resource = source, .kind = mrhi_accessCopySource};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.neverCull = true;
    def.accesses = &access;
    def.accessCount = 1;
    CHECK(mrhiAddPass(s_device, &def, &s_pass) == mrhi_success &&
              mrhiDevice_CompileFrame(s_device) == mrhi_success &&
              mrhiPass_Begin(s_device, s_pass) == mrhi_success,
          "a pass");
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, source, 0, s_bytes, 4) == mrhi_errorInvalid,
          "a buffer declared a copy source");
    Drop();
    CloseDevice();
}

static void TestFull(void)
{
    Open(1024);
    Frame();
    mrhiResourceId buffer = s_r[BUFFER];
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, buffer, 0, s_bytes, 512) == mrhi_success &&
              mrhiPass_WriteBuffer(s_device, s_pass, buffer, 0, s_bytes, 512) == mrhi_success,
          "the staging filled");
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, buffer, 0, s_bytes, 4) == mrhi_errorCapacity,
          "full");
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, buffer, 0, nullptr, 0) == mrhi_errorCapacity,
          "nothing more in the pass");
    CHECK(mrhiPass_End(s_device, s_pass) == mrhi_success, "ended");
    mrhiRequestId token;
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_errorCapacity, "the frame refused");
    Drop();
    Frame();
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, s_r[BUFFER], 0, s_bytes, 1024) == mrhi_success,
          "all of it, in the next frame");
    Drop();
    Frame();
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, s_r[BUFFER], 0, s_bytes, 1028) ==
              mrhi_errorInvalid,
          "more than the buffer");
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, s_r[BUFFER], 0, s_bytes, 4) == mrhi_success,
          "a refusal for input marks nothing");
    Drop();
    CloseDevice();
    Open(0);
    Frame();
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, s_r[BUFFER], 0, nullptr, 0) == mrhi_success,
          "nothing, with no staging");
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, s_r[BUFFER], 0, s_bytes, 4) == mrhi_errorCapacity,
          "anything else refused");
    Drop();
    CloseDevice();
    Open(512);
    Frame();
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, s_r[BUFFER], 0, s_bytes, 1024) ==
              mrhi_errorCapacity,
          "more than the staging");
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, s_r[BUFFER], 0, s_bytes, 4) == mrhi_errorCapacity,
          "and nothing after, though it would fit");
    Drop();
    CloseDevice();
}

// Staging whose size is not a multiple of the placement boundary: the
// last upload's room ends with the staging, and the driver is handed no
// byte past it.
static void TestUnevenStaging(void)
{
    mrhiTestFrameLog log = {0};
    s_adapter.frameLog = &log;
    Open(1000);
    Frame();
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, s_r[BUFFER], 0, s_bytes, 900) == mrhi_success,
          "within the staging");
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, s_r[BUFFER], 0, s_bytes, 4) == mrhi_errorCapacity,
          "nothing past its end");
    Drop();
    Frame();
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, s_r[BUFFER], 0, s_bytes, 900) == mrhi_success,
          "again in the next frame");
    (void)Submit();
    CHECK(log.stagingBytes == 1000, "the driver handed the staging and no more");
    CloseDevice();
    s_adapter.frameLog = nullptr;
}

// A frame's staged bytes stay as they were while it runs and the next
// frame stages its own, as the SPI promises a driver.
static void TestStagingInFlight(void)
{
    Open(1024);
    Frame();
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, s_r[BUFFER], 0, s_bytes, 8) == mrhi_success,
          "the first frame's bytes");
    const uint8_t* first = Staged(0);
    mrhiRequestId running = Submit();
    Frame();
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, s_r[BUFFER], 0, s_bytes + 40, 8) == mrhi_success &&
              memcmp(Staged(0), s_bytes + 40, 8) == 0,
          "the second frame's bytes");
    CHECK(memcmp(first, s_bytes, 8) == 0, "the running frame's bytes untouched");
    mrhiRequestId second = Submit();
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frameDef) == mrhi_errorCapacity, "two in flight");
    CHECK(mrhiRequest_WaitFrame(s_device, second, 1) == mrhi_success, "the second finished first");
    Frame();
    CHECK(mrhiPass_WriteBuffer(s_device, s_pass, s_r[BUFFER], 0, s_bytes + 80, 8) == mrhi_success,
          "a third frame's bytes");
    CHECK(memcmp(first, s_bytes, 8) == 0, "the first frame's still untouched");
    Drop();
    CHECK(mrhiRequest_WaitFrame(s_device, running, 1) == mrhi_success, "the first finished");
    CloseDevice();
}

// Writes texels into a texture from s_bytes: the result.
static mrhiResult Write(mrhiTextureCopy destination, size_t byteCount, mrhiTexelLayout layout,
                        mrhiExtent3d size)
{
    destination.resource = s_r[destination.resource.index1];
    return mrhiPass_WriteTexture(s_device, s_pass, &destination, s_bytes, byteCount, &layout,
                                 &size);
}

static mrhiTextureCopy At(uint32_t resource, uint32_t z)
{
    return (mrhiTextureCopy){.resource = {resource, 0}, .z = z};
}

static void TestTextures(void)
{
    Open(16384);
    Frame();
    // 16 by 8 texels of 4 bytes, from offset 3 at a pitch of 64.
    mrhiTexelLayout tight = {.offset = 3, .bytesPerRow = 64};
    CHECK(Write(At(ARRAY, 1), 3 + 64 * 8, tight, (mrhiExtent3d){16, 8, 1}) == mrhi_success,
          "a layer");
    const mrhiCommand* command = Nth(0);
    const mrhiCommandBufferSide* from = (const mrhiCommandBufferSide*)Nth(1);
    const mrhiCommandTextureSide* to = (const mrhiCommandTextureSide*)Nth(2);
    CHECK(command != nullptr && command->type == mrhiCommandWriteTexture && command->b == 16 &&
              command->c == 8 && command->d == 1 && from->object == 0 && from->offset == 0 &&
              from->bytesPerRow == 256 && from->rowsPerImage == 8 &&
              to->object == s_r[ARRAY].index1 && to->z == 1,
          "recorded as a copy from staging");
    bool repacked = true;
    for (uint32_t y = 0; y < 8; ++y)
    {
        repacked = repacked && memcmp(Staged(y * 256), s_bytes + 3 + y * 64, 64) == 0;
    }
    CHECK(repacked, "its rows staged at a pitch of 256");
    // Two layers from a padded layout, 2048 bytes staged at 512.
    mrhiTexelLayout padded = {.bytesPerRow = 80, .rowsPerImage = 9};
    CHECK(Write(At(ARRAY, 1), 80 * 9 + 80 * 7 + 64, padded, (mrhiExtent3d){16, 8, 2}) ==
              mrhi_success,
          "two layers");
    from = (const mrhiCommandBufferSide*)Nth(4);
    CHECK(from->offset == 2048 &&
              memcmp(Staged(2048 + 8 * 256 + 3 * 256), s_bytes + 80 * 9 + 3 * 80, 64) == 0,
          "the second layer's fourth row");
    CHECK(Write(At(ARRAY, 1), 80 * 9 + 80 * 7 + 63, padded, (mrhiExtent3d){16, 8, 2}) ==
              mrhi_errorInvalid,
          "a byte short");
    CHECK(Write(At(ARRAY, 0), 4096, (mrhiTexelLayout){0}, (mrhiExtent3d){16, 8, 1}) ==
              mrhi_errorInvalid,
          "rows without their bytes");
    CHECK(Write(At(ARRAY, 0), 4096, (mrhiTexelLayout){.bytesPerRow = 60},
                (mrhiExtent3d){16, 8, 1}) == mrhi_errorInvalid,
          "fewer bytes per row than a row's");
    CHECK(Write(At(ARRAY, 0), 4096, (mrhiTexelLayout){.bytesPerRow = 64},
                (mrhiExtent3d){16, 8, 2}) == mrhi_errorInvalid,
          "layers without rows per image");
    CHECK(Write(At(ARRAY, 2), 4096, (mrhiTexelLayout){.bytesPerRow = 64, .rowsPerImage = 8},
                (mrhiExtent3d){16, 8, 2}) == mrhi_errorInvalid,
          "past the layers");
    CHECK(Write(At(MULTI, 0), 4096, (mrhiTexelLayout){.bytesPerRow = 32},
                (mrhiExtent3d){8, 8, 1}) == mrhi_errorInvalid,
          "a multisampled texture");
    CHECK(Write(At(DEPTH, 0), 4096, (mrhiTexelLayout){.bytesPerRow = 32},
                (mrhiExtent3d){8, 8, 1}) == mrhi_errorInvalid,
          "depth32float's depth");
    mrhiTextureCopy stencil = At(STENCIL, 0);
    stencil.aspect = mrhi_aspectStencilOnly;
    CHECK(Write(stencil, 64, (mrhiTexelLayout){.bytesPerRow = 8}, (mrhiExtent3d){8, 8, 1}) ==
              mrhi_success,
          "a stencil of a byte per texel");
    mrhiTextureCopy empty = {.resource = s_r[ARRAY]};
    CHECK(mrhiPass_WriteTexture(s_device, s_pass, &empty, nullptr, 0, &(mrhiTexelLayout){0},
                                &(mrhiExtent3d){0, 1, 1}) == mrhi_success,
          "no texels, from no bytes");
    CHECK(Write(At(OUTSIDE, 0), 4096, (mrhiTexelLayout){.bytesPerRow = 32},
                (mrhiExtent3d){8, 8, 1}) == mrhi_errorInvalid,
          "a texture the pass does not declare");
    // A staging of zeros, so that a row staged wider shows.
    uint64_t taken = s_device->stagingTaken;
    memset((uint8_t*)Staged(taken), 0, 1024);
    CHECK(Write(At(BLOCKS, 0), 32, (mrhiTexelLayout){.bytesPerRow = 16}, (mrhiExtent3d){8, 8, 1}) ==
              mrhi_success,
          "two rows of two BC1 blocks");
    const uint8_t zeros[16] = {0};
    CHECK(memcmp(Staged(taken), s_bytes, 16) == 0 && memcmp(Staged(taken + 16), zeros, 16) == 0 &&
              memcmp(Staged(taken + 256), s_bytes + 16, 16) == 0,
          "16 bytes a row");
    CHECK(Write(At(BLOCKS, 0), 31, (mrhiTexelLayout){.bytesPerRow = 16}, (mrhiExtent3d){8, 8, 1}) ==
              mrhi_errorInvalid,
          "a byte short");
    mrhiTexelLayout layout = {.bytesPerRow = 64};
    mrhiExtent3d size = {16, 8, 1};
    mrhiTextureCopy destination = At(ARRAY, 0);
    destination.resource = s_r[ARRAY];
    CHECK(mrhiPass_WriteTexture(s_device, s_pass, nullptr, s_bytes, 512, &layout, &size) ==
                  mrhi_errorInvalid &&
              mrhiPass_WriteTexture(s_device, s_pass, &destination, nullptr, 512, &layout, &size) ==
                  mrhi_errorInvalid &&
              mrhiPass_WriteTexture(s_device, s_pass, &destination, s_bytes, 512, nullptr, &size) ==
                  mrhi_errorInvalid &&
              mrhiPass_WriteTexture(s_device, s_pass, &destination, s_bytes, 512, &layout,
                                    nullptr) == mrhi_errorInvalid,
          "NULL arguments");
    CHECK(mrhiPass_WriteTexture(s_device, s_render, &destination, s_bytes, 512, &layout, &size) ==
              mrhi_errorInvalid,
          "a pass with targets");
    CHECK(mrhiDevice_GetMisuse(s_device) == 14, "each counted");
    CHECK(mrhiPass_WriteTexture(nullptr, s_pass, &destination, s_bytes, 512, &layout, &size) ==
              mrhi_errorInvalid,
          "no device");
    Drop();
    Frame();
    // Three layers staged at a pitch of 256 take 6 KiB of the 16.
    destination.resource = s_r[ARRAY];
    mrhiTexelLayout many = {.bytesPerRow = 64, .rowsPerImage = 8};
    mrhiExtent3d three = {16, 8, 3};
    CHECK(mrhiPass_WriteTexture(s_device, s_pass, &destination, s_bytes, 64 * 8 * 3, &many,
                                &three) == mrhi_success &&
              mrhiPass_WriteTexture(s_device, s_pass, &destination, s_bytes, 64 * 8 * 3, &many,
                                    &three) == mrhi_success,
          "three layers, twice");
    CHECK(mrhiPass_WriteTexture(s_device, s_pass, &destination, s_bytes, 64 * 8 * 3, &many,
                                &three) == mrhi_errorCapacity,
          "not three times");
    CHECK(mrhiPass_End(s_device, s_pass) == mrhi_success, "ended");
    mrhiRequestId token;
    CHECK(mrhiDevice_SubmitFrame(s_device, &token) == mrhi_errorCapacity, "the frame refused");
    Drop();
    CloseDevice();
}

// Rows of 256 bytes exactly stage at that pitch, no row more.
static void TestWholePitch(void)
{
    Open(16384);
    Frame();
    CHECK(Write(At(WIDE, 0), 512, (mrhiTexelLayout){.bytesPerRow = 256},
                (mrhiExtent3d){64, 2, 1}) == mrhi_success,
          "two rows of 256 bytes");
    const mrhiCommandBufferSide* from = (const mrhiCommandBufferSide*)Nth(1);
    CHECK(from != nullptr && from->bytesPerRow == 256 && from->rowsPerImage == 2,
          "staged at a pitch of 256");
    Drop();
    CloseDevice();
}

int main(void)
{
    ResetAdapter();
    TestBuffers();
    TestWholePitch();
    TestFull();
    TestUnevenStaging();
    TestStagingInFlight();
    TestTextures();
    return s_failures == 0 ? 0 : 1;
}
