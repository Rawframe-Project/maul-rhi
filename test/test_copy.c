// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Copies on a test driver device: between buffers, buffers and
// textures, and textures, each refused where WebGPU refuses it or where
// the pass declares no covering copy access, and recorded with its two
// sides otherwise.

#include "device_core.h"
#include "test_device_setup.h"

#include "maul-rhi/encoder.h"

enum
{
    // The resources, imported into each frame in this order.
    SOURCE,
    DESTINATION,
    ARRAY,
    OTHER,
    SRGB,
    HALF,
    WIDE,
    DEPTH,
    DEPTH_OTHER,
    STENCIL,
    STENCIL_OTHER,
    MULTI,
    MULTI_OTHER,
    VOLUME,
    BLOCKS,
    SINGLE,
    LAYERS,
    TARGET,
    RESOURCES,
};

static mrhiDevice* s_device;
static mrhiBufferId s_buffers[2];
static mrhiTextureId s_textures[RESOURCES];
static mrhiResourceId s_r[RESOURCES];
static mrhiPassId s_pass;
static mrhiPassId s_render;

static const uint32_t COPY = mrhi_textureCopySource | mrhi_textureCopyDestination;

static mrhiTextureId MakeTexture(mrhiTextureKind kind, mrhiFormat format, uint32_t width,
                                 uint32_t height, uint32_t layers, uint32_t mips, uint32_t samples)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.kind = kind;
    def.format = format;
    def.width = width;
    def.height = height;
    def.depthOrLayers = layers;
    def.mipLevels = mips;
    def.sampleCount = samples;
    def.usage = COPY | (samples > 1 ? mrhi_textureRenderTarget : 0);
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(s_device, &def, &texture) == mrhi_success, "a texture");
    return texture;
}

// Opens a ready device with BC formats and the resources copies use:
// two 4 KiB buffers; rgba8 2D arrays of 16 by 8 texels, 3 layers and 2
// mips (an sRGB one and a half-float one beside); a wide rgba8 texture
// of 128 by 2 and 3 mips; depth32float and combined depth-stencil pairs
// of 8 by 8; multisampled rgba8 pairs; an 8 by 8 by 2 volume of 3 mips;
// a BC1 texture of 12 by 12 and 2 mips; a single-sampled rgba8 texture
// of 8 by 8; an rgba8 array of 8 layers of 4 by 4; and a multisampled
// render target.
static void Open(void)
{
    s_adapter.features.textureCompressionBc = true;
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.features.textureCompressionBc = true;
    def.deviceLimits.diagnostics = 8;
    s_device = OpenWith(def, true);
    for (uint32_t i = 0; i < 2; ++i)
    {
        mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
        bufferDef.size = 4096;
        bufferDef.usage = mrhi_bufferCopySource | mrhi_bufferCopyDestination;
        CHECK(mrhiCreateBuffer(s_device, &bufferDef, &s_buffers[i]) == mrhi_success, "a buffer");
    }
    mrhiTextureKind flat = mrhi_texture2dArray;
    s_textures[ARRAY] = MakeTexture(flat, mrhi_formatRgba8Unorm, 16, 8, 3, 2, 1);
    s_textures[OTHER] = MakeTexture(flat, mrhi_formatRgba8Unorm, 16, 8, 3, 2, 1);
    s_textures[SRGB] = MakeTexture(flat, mrhi_formatRgba8UnormSrgb, 16, 8, 3, 2, 1);
    s_textures[HALF] = MakeTexture(flat, mrhi_formatRgba16Float, 16, 8, 3, 2, 1);
    s_textures[WIDE] = MakeTexture(mrhi_texture2d, mrhi_formatRgba8Unorm, 128, 2, 1, 3, 1);
    s_textures[DEPTH] = MakeTexture(mrhi_texture2d, mrhi_formatDepth32Float, 8, 8, 1, 1, 1);
    s_textures[DEPTH_OTHER] = MakeTexture(mrhi_texture2d, mrhi_formatDepth32Float, 8, 8, 1, 1, 1);
    s_textures[STENCIL] = MakeTexture(mrhi_texture2d, mrhi_formatDepthStencil, 8, 8, 1, 1, 1);
    s_textures[STENCIL_OTHER] = MakeTexture(mrhi_texture2d, mrhi_formatDepthStencil, 8, 8, 1, 1, 1);
    s_textures[MULTI] = MakeTexture(mrhi_texture2d, mrhi_formatRgba8Unorm, 8, 8, 1, 1, 4);
    s_textures[MULTI_OTHER] = MakeTexture(mrhi_texture2d, mrhi_formatRgba8Unorm, 8, 8, 1, 1, 4);
    s_textures[VOLUME] = MakeTexture(mrhi_texture3d, mrhi_formatRgba8Unorm, 8, 8, 2, 3, 1);
    s_textures[BLOCKS] = MakeTexture(mrhi_texture2d, mrhi_formatBc1RgbaUnorm, 12, 12, 1, 2, 1);
    s_textures[SINGLE] = MakeTexture(mrhi_texture2d, mrhi_formatRgba8Unorm, 8, 8, 1, 1, 1);
    s_textures[LAYERS] = MakeTexture(flat, mrhi_formatRgba8Unorm, 4, 4, 8, 1, 1);
    s_textures[TARGET] = MakeTexture(mrhi_texture2d, mrhi_formatRgba8Unorm, 8, 8, 1, 1, 4);
}

static void CloseDevice(void)
{
    Close(s_device);
    s_device = nullptr;
}

static mrhiAccess Access(uint32_t resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = s_r[resource],
        .kind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
}

// Begins a frame importing every resource.
static void BeginFrame(void)
{
    mrhiFrameDef def = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &def) == mrhi_success, "begun");
    for (uint32_t i = 0; i < RESOURCES; ++i)
    {
        mrhiResult status = i < 2 ? mrhiImportBuffer(s_device, s_buffers[i], &s_r[i])
                                  : mrhiImportTexture(s_device, s_textures[i], &s_r[i]);
        CHECK(status == mrhi_success, "imported");
    }
}

// Adds a copying pass of the accesses and a render pass declaring them
// too, so that only its targets refuse copies, compiles and begins both.
static void Passes(const mrhiAccess* accesses, uint32_t count)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.neverCull = true;
    def.accesses = accesses;
    def.accessCount = count;
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

static void Drop(void)
{
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
}

// The n-th record of the pass: or NULL past its records.
static const mrhiCommand* Nth(uint32_t n)
{
    const mrhiFramePass* pass = &s_device->framePasses[s_pass.index1 - 1];
    const mrhiCommandChunk* chunk = &s_device->frameChunks[pass->firstChunk - 1];
    return pass->firstChunk != 0 && n < chunk->count ? &chunk->commands[n] : nullptr;
}

static mrhiTextureCopy Texture(uint32_t resource, uint32_t mip, uint32_t z)
{
    return (mrhiTextureCopy){.resource = s_r[resource], .mip = mip, .z = z};
}

static mrhiBufferCopy Buffer(uint32_t resource, uint64_t offset, uint32_t perRow, uint32_t perImage)
{
    return (mrhiBufferCopy){
        .resource = s_r[resource],
        .offset = offset,
        .bytesPerRow = perRow,
        .rowsPerImage = perImage,
    };
}

// Clears of a buffer (mrhi-0022): recorded as one command, the size
// resolved, nothing for an empty one, and every refusal.
// The check that refused the device's latest refusal, draining its
// diagnostics (mrhi-0027); 0 when there is none.
static mrhiDiagnosticCode Refusal(void)
{
    mrhiDiagnosticCode code = 0;
    mrhiDiagnostic record;
    while (mrhiNextDeviceDiagnostic(s_device, &record) == mrhi_success)
    {
        code = record.code;
    }
    return code;
}

static void TestClears(void)
{
    Open();
    BeginFrame();
    mrhiAccess accesses[] = {
        Access(SOURCE, mrhi_accessCopySource),
        Access(DESTINATION, mrhi_accessCopyDestination),
    };
    Passes(accesses, 2);
    mrhiResourceId from = s_r[SOURCE];
    mrhiResourceId to = s_r[DESTINATION];
    CHECK(mrhiClearBuffer(s_device, s_pass, to, 8, 16) == mrhi_success &&
              mrhiClearBuffer(s_device, s_pass, to, 4000, MRHI_WHOLE_SIZE) == mrhi_success,
          "cleared");
    const mrhiCommand* first = Nth(0);
    const mrhiCommand* second = Nth(1);
    CHECK(first != nullptr && first->type == mrhiCommandClearBuffer && first->payload == 0 &&
              first->a == to.index1 && first->c == 8 && first->d == 16,
          "recorded");
    CHECK(second != nullptr && second->c == 4000 && second->d == 96, "the rest resolved");
    CHECK(mrhiClearBuffer(s_device, s_pass, to, 4096, MRHI_WHOLE_SIZE) == mrhi_success &&
              mrhiClearBuffer(s_device, s_pass, to, 0, 0) == mrhi_success && Nth(2) == nullptr,
          "nothing recorded for an empty clear");
    CHECK(mrhiClearBuffer(s_device, s_pass, to, 2, 4) == mrhi_errorInvalid &&
              mrhiClearBuffer(s_device, s_pass, to, 0, 6) == mrhi_errorInvalid &&
              Refusal() == mrhi_diagnosticTransferAlignment,
          "an offset or size not a multiple of 4");
    CHECK(mrhiClearBuffer(s_device, s_pass, to, 8, 4092) == mrhi_errorInvalid &&
              mrhiClearBuffer(s_device, s_pass, to, 4100, MRHI_WHOLE_SIZE) == mrhi_errorInvalid &&
              mrhiClearBuffer(s_device, s_pass, to, UINT64_MAX - 3, 8) == mrhi_errorInvalid &&
              Refusal() == mrhi_diagnosticTransferRange,
          "past the end");
    CHECK(mrhiClearBuffer(s_device, s_pass, from, 0, 4) == mrhi_errorInvalid &&
              Refusal() == mrhi_diagnosticUndeclaredAccess,
          "a buffer declared only as a copy source");
    CHECK(mrhiClearBuffer(s_device, s_pass, s_r[ARRAY], 0, 4) == mrhi_errorInvalid &&
              Refusal() == mrhi_diagnosticResourceKind,
          "a texture");
    CHECK(mrhiClearBuffer(s_device, s_render, to, 0, 4) == mrhi_errorInvalid &&
              Refusal() == mrhi_diagnosticTransferInRenderPass,
          "a pass with targets");
    CHECK(mrhiGetDeviceMisuse(s_device) == 8, "each counted");
    mrhiResourceId none = {0};
    CHECK(mrhiClearBuffer(s_device, s_pass, none, 0, 4) == mrhi_errorStale, "none");
    CHECK(mrhiClearBuffer(nullptr, s_pass, to, 0, 4) == mrhi_errorInvalid, "no device");
    CHECK(mrhiEndPass(s_device, s_pass) == mrhi_success &&
              mrhiClearBuffer(s_device, s_pass, to, 0, 4) == mrhi_errorState,
          "not recording");
    Drop();
    CloseDevice();
}

static void TestBuffers(void)
{
    Open();
    BeginFrame();
    mrhiAccess accesses[] = {
        Access(SOURCE, mrhi_accessCopySource),
        Access(DESTINATION, mrhi_accessCopyDestination),
    };
    Passes(accesses, 2);
    mrhiResourceId from = s_r[SOURCE];
    mrhiResourceId to = s_r[DESTINATION];
    CHECK(mrhiCopyBuffer(s_device, s_pass, from, 4, to, 8, 4088) == mrhi_success, "copied");
    const mrhiCommand* command = Nth(0);
    const mrhiCommandBufferSide* source = (const mrhiCommandBufferSide*)Nth(1);
    const mrhiCommandBufferSide* destination = (const mrhiCommandBufferSide*)Nth(2);
    CHECK(command != nullptr && command->type == mrhiCommandCopyBuffer && command->payload == 2 &&
              command->b == 4088 && source->object == from.index1 && source->offset == 4 &&
              destination->object == to.index1 && destination->offset == 8,
          "recorded with both sides");
    CHECK(mrhiCopyBuffer(s_device, s_pass, from, 0, to, 0, 0) == mrhi_success, "nothing");
    CHECK(mrhiCopyBuffer(s_device, s_pass, from, 0, to, 0, 6) == mrhi_errorInvalid,
          "a size not a multiple of 4");
    CHECK(mrhiCopyBuffer(s_device, s_pass, from, 2, to, 0, 4) == mrhi_errorInvalid &&
              mrhiCopyBuffer(s_device, s_pass, from, 0, to, 2, 4) == mrhi_errorInvalid,
          "an offset not a multiple of 4");
    CHECK(mrhiCopyBuffer(s_device, s_pass, from, 8, to, 0, 4092) == mrhi_errorInvalid &&
              mrhiCopyBuffer(s_device, s_pass, from, 0, to, 8, 4092) == mrhi_errorInvalid,
          "past either end");
    CHECK(mrhiCopyBuffer(s_device, s_pass, from, 4100, to, 0, 0) == mrhi_errorInvalid &&
              mrhiCopyBuffer(s_device, s_pass, from, 0, to, 4100, 0) == mrhi_errorInvalid,
          "starting past either");
    CHECK(mrhiCopyBuffer(s_device, s_pass, from, UINT64_MAX - 3, to, 0, 8) == mrhi_errorInvalid,
          "wrapping");
    CHECK(mrhiCopyBuffer(s_device, s_pass, to, 0, from, 0, 4) == mrhi_errorInvalid,
          "each the other way than declared");
    CHECK(mrhiCopyBuffer(s_device, s_pass, from, 0, from, 0, 4) == mrhi_errorInvalid,
          "one buffer as both");
    CHECK(mrhiCopyBuffer(s_device, s_pass, to, 0, to, 0, 4) == mrhi_errorInvalid,
          "a buffer not declared as a source");
    CHECK(mrhiCopyBuffer(s_device, s_pass, s_r[ARRAY], 0, to, 0, 4) == mrhi_errorInvalid &&
              mrhiCopyBuffer(s_device, s_pass, from, 0, s_r[ARRAY], 0, 4) == mrhi_errorInvalid,
          "a texture");
    CHECK(mrhiCopyBuffer(s_device, s_render, from, 0, to, 0, 4) == mrhi_errorInvalid,
          "a pass with targets");
    CHECK(mrhiGetDeviceMisuse(s_device) == 14, "each counted");
    mrhiResourceId none = {0};
    CHECK(mrhiCopyBuffer(s_device, s_pass, none, 0, to, 0, 4) == mrhi_errorStale &&
              mrhiCopyBuffer(s_device, s_pass, from, 0, none, 0, 4) == mrhi_errorStale,
          "none");
    CHECK(mrhiCopyBuffer(nullptr, s_pass, from, 0, to, 0, 4) == mrhi_errorInvalid, "no device");
    CHECK(mrhiEndPass(s_device, s_pass) == mrhi_success &&
              mrhiCopyBuffer(s_device, s_pass, from, 0, to, 0, 4) == mrhi_errorState,
          "not recording");
    Drop();
    // A texture declared as a copy source, whose slot a buffer's shares.
    BeginFrame();
    mrhiAccess texture[] = {
        Access(ARRAY, mrhi_accessCopySource),
        Access(DESTINATION, mrhi_accessCopyDestination),
    };
    Passes(texture, 2);
    CHECK(mrhiCopyBuffer(s_device, s_pass, s_r[ARRAY], 0, s_r[DESTINATION], 0, 4) ==
              mrhi_errorInvalid,
          "a texture as a buffer");
    Drop();
    CloseDevice();
}

// Copies from the source buffer into a texture, its layers from z, with
// the layout: the result.
static mrhiResult Upload(mrhiBufferCopy buffer, mrhiTextureCopy texture, mrhiExtent3d size)
{
    return mrhiCopyBufferToTexture(s_device, s_pass, &buffer, &texture, &size);
}

static mrhiResult Download(mrhiTextureCopy texture, mrhiBufferCopy buffer, mrhiExtent3d size)
{
    return mrhiCopyTextureToBuffer(s_device, s_pass, &texture, &buffer, &size);
}

// A pass declaring the buffers as source and destination, and a texture
// with a kind over a range.
static void Declare(uint32_t texture, mrhiAccessKind kind, mrhiTextureRange range)
{
    BeginFrame();
    mrhiAccess accesses[] = {
        Access(SOURCE, mrhi_accessCopySource),
        Access(DESTINATION, mrhi_accessCopyDestination),
        Access(texture, kind),
    };
    accesses[2].range = range;
    Passes(accesses, 3);
}

static const mrhiTextureRange ALL = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING};

static void TestLayouts(void)
{
    Open();
    mrhiTextureRange layers = {.mipCount = MRHI_REMAINING, .baseLayer = 1, .layerCount = 2};
    Declare(ARRAY, mrhi_accessCopyDestination, layers);
    mrhiExtent3d one = {16, 8, 1};
    CHECK(Upload(Buffer(SOURCE, 0, 256, 0), Texture(ARRAY, 0, 1), one) == mrhi_success, "a layer");
    const mrhiCommand* command = Nth(0);
    const mrhiCommandBufferSide* buffer = (const mrhiCommandBufferSide*)Nth(1);
    const mrhiCommandTextureSide* texture = (const mrhiCommandTextureSide*)Nth(2);
    CHECK(command != nullptr && command->type == mrhiCommandCopyBufferToTexture &&
              command->b == 16 && command->c == 8 && command->d == 1 &&
              buffer->object == s_r[SOURCE].index1 && buffer->bytesPerRow == 256 &&
              texture->object == s_r[ARRAY].index1 && texture->z == 1,
          "recorded, the buffer first");
    mrhiExtent3d row = {16, 1, 1};
    CHECK(Upload(Buffer(SOURCE, 4032, 0, 0), Texture(ARRAY, 0, 2), row) == mrhi_success,
          "one row without a layout, ending at the buffer's end");
    CHECK(Upload(Buffer(SOURCE, 4036, 0, 0), Texture(ARRAY, 0, 2), row) == mrhi_errorInvalid,
          "past it");
    CHECK(Upload(Buffer(SOURCE, 0, 0, 0), Texture(ARRAY, 0, 1), one) == mrhi_errorInvalid,
          "rows without their bytes");
    CHECK(Upload(Buffer(SOURCE, 0, 128, 0), Texture(ARRAY, 0, 1), one) == mrhi_errorInvalid,
          "bytes per row off 256");
    mrhiExtent3d two = {16, 8, 2};
    CHECK(Upload(Buffer(SOURCE, 0, 256, 8), Texture(ARRAY, 0, 1), two) == mrhi_success,
          "two layers");
    CHECK(Upload(Buffer(SOURCE, 0, 256, 0), Texture(ARRAY, 0, 1), two) == mrhi_errorInvalid,
          "without rows per image");
    CHECK(Upload(Buffer(SOURCE, 0, 256, 7), Texture(ARRAY, 0, 1), two) == mrhi_errorInvalid,
          "fewer rows per image than rows");
    CHECK(Upload(Buffer(SOURCE, 0, 0, 1), Texture(ARRAY, 0, 1), (mrhiExtent3d){16, 1, 2}) ==
              mrhi_errorInvalid,
          "layers of one row without bytes per row");
    CHECK(Upload(Buffer(SOURCE, 192, 256, 8), Texture(ARRAY, 0, 1), two) == mrhi_success &&
              Upload(Buffer(SOURCE, 196, 256, 8), Texture(ARRAY, 0, 1), two) == mrhi_errorInvalid,
          "the last layer's last row ends the buffer");
    CHECK(Upload(Buffer(SOURCE, 0, 256, 0xFFFFFFFFu), Texture(ARRAY, 0, 1), two) ==
              mrhi_errorInvalid,
          "images past the buffer");
    CHECK(Upload(Buffer(SOURCE, 0xFFFFFFFFFFFFFF00u, 256, 8), Texture(ARRAY, 0, 1), two) ==
              mrhi_errorInvalid,
          "an offset past it");
    CHECK(Upload(Buffer(SOURCE, 2, 256, 0), Texture(ARRAY, 0, 1), one) == mrhi_errorInvalid,
          "an offset off the texel's bytes");
    CHECK(Upload(Buffer(SOURCE, 4, 256, 0), Texture(ARRAY, 0, 1), one) == mrhi_success, "on them");
    // The fifth copy: four of three records before it, then its command.
    CHECK(((const mrhiCommandBufferSide*)Nth(4 * 3 + 1))->offset == 4, "recorded at them");
    CHECK(mrhiGetDeviceMisuse(s_device) == 10, "each counted");
    Drop();
    Declare(WIDE, mrhi_accessCopyDestination, ALL);
    mrhiExtent3d wide = {128, 2, 1};
    CHECK(Upload(Buffer(SOURCE, 0, 256, 0), Texture(WIDE, 0, 0), wide) == mrhi_errorInvalid,
          "fewer bytes per row than a row's");
    CHECK(Upload(Buffer(SOURCE, 0, 512, 0), Texture(WIDE, 0, 0), wide) == mrhi_success, "a row's");
    CHECK(Upload(Buffer(SOURCE, 0, 0, 0), Texture(WIDE, 2, 0), (mrhiExtent3d){32, 1, 1}) ==
              mrhi_success,
          "a mip whose height halved to nothing is one texel");
    Drop();
    // Images of 2^62 bytes: four of them wrap 64 bits to nothing.
    Declare(LAYERS, mrhi_accessCopyDestination, ALL);
    CHECK(Upload(Buffer(SOURCE, 0, 1u << 31, 1u << 31), Texture(LAYERS, 0, 0),
                 (mrhiExtent3d){4, 1, 5}) == mrhi_errorInvalid,
          "a layout past 64 bits");
    Drop();
    CloseDevice();
}

static void TestRegions(void)
{
    Open();
    mrhiTextureRange layers = {.mipCount = MRHI_REMAINING, .baseLayer = 1, .layerCount = 2};
    Declare(ARRAY, mrhi_accessCopyDestination, layers);
    mrhiBufferCopy buffer = Buffer(SOURCE, 0, 256, 8);
    mrhiTextureCopy texture = Texture(ARRAY, 0, 1);
    texture.x = 8;
    CHECK(Upload(buffer, texture, (mrhiExtent3d){8, 8, 1}) == mrhi_success, "the right half");
    CHECK(Upload(buffer, texture, (mrhiExtent3d){9, 8, 1}) == mrhi_errorInvalid, "past it");
    texture = Texture(ARRAY, 0, 1);
    texture.y = 4;
    CHECK(Upload(buffer, texture, (mrhiExtent3d){16, 5, 1}) == mrhi_errorInvalid, "past the end");
    texture = Texture(ARRAY, 1, 1);
    CHECK(Upload(buffer, texture, (mrhiExtent3d){8, 4, 1}) == mrhi_success, "all of mip 1");
    CHECK(Upload(buffer, texture, (mrhiExtent3d){16, 4, 1}) == mrhi_errorInvalid &&
              Upload(buffer, texture, (mrhiExtent3d){8, 8, 1}) == mrhi_errorInvalid,
          "past mip 1");
    CHECK(Upload(buffer, Texture(ARRAY, 2, 1), (mrhiExtent3d){1, 1, 1}) == mrhi_errorInvalid,
          "a mip past the texture's");
    CHECK(Upload(buffer, Texture(ARRAY, 0, 2), (mrhiExtent3d){16, 8, 2}) == mrhi_errorInvalid,
          "a layer past its layers");
    CHECK(Upload(buffer, Texture(ARRAY, 0, 0), (mrhiExtent3d){16, 8, 1}) == mrhi_errorInvalid,
          "a layer not declared");
    texture = Texture(ARRAY, 0, 1);
    texture.aspect = mrhi_aspectDepthOnly;
    CHECK(Upload(buffer, texture, (mrhiExtent3d){16, 8, 1}) == mrhi_errorInvalid,
          "an aspect the format lacks");
    texture.aspect = 3;
    CHECK(Upload(buffer, texture, (mrhiExtent3d){16, 8, 1}) == mrhi_errorInvalid,
          "an unknown aspect");
    CHECK(Download(Texture(ARRAY, 0, 1), Buffer(DESTINATION, 0, 256, 0),
                   (mrhiExtent3d){16, 8, 1}) == mrhi_errorInvalid,
          "a texture declared as a destination copied from");
    CHECK(mrhiGetDeviceMisuse(s_device) == 10, "each counted");
    Drop();
    Declare(ARRAY, mrhi_accessCopySource, ALL);
    CHECK(Download(Texture(ARRAY, 1, 2), Buffer(DESTINATION, 256, 256, 0),
                   (mrhiExtent3d){8, 4, 1}) == mrhi_success,
          "downloaded");
    const mrhiCommand* command = Nth(0);
    const mrhiCommandTextureSide* texture2 = (const mrhiCommandTextureSide*)Nth(1);
    const mrhiCommandBufferSide* buffer2 = (const mrhiCommandBufferSide*)Nth(2);
    CHECK(command != nullptr && command->type == mrhiCommandCopyTextureToBuffer &&
              texture2->mip == 1 && texture2->z == 2 &&
              buffer2->object == s_r[DESTINATION].index1 && buffer2->offset == 256,
          "recorded, the texture first");
    CHECK(Download(Texture(ARRAY, 0, 0), Buffer(SOURCE, 0, 256, 0), (mrhiExtent3d){16, 8, 1}) ==
              mrhi_errorInvalid,
          "into the source buffer");
    Drop();
    // The volume is 8 by 8 by 2, its mip 1 4 by 4 by 1, its mip 2 2 by 2
    // by 1.
    Declare(VOLUME, mrhi_accessCopyDestination, ALL);
    CHECK(Upload(Buffer(SOURCE, 0, 256, 8), Texture(VOLUME, 0, 1), (mrhiExtent3d){8, 8, 1}) ==
              mrhi_success,
          "a volume's second slice");
    CHECK(Upload(Buffer(SOURCE, 0, 256, 4), Texture(VOLUME, 1, 0), (mrhiExtent3d){4, 4, 1}) ==
              mrhi_success,
          "a volume's mip");
    CHECK(Upload(Buffer(SOURCE, 0, 256, 4), Texture(VOLUME, 1, 0), (mrhiExtent3d){4, 4, 2}) ==
              mrhi_errorInvalid,
          "past its depth");
    CHECK(Upload(Buffer(SOURCE, 0, 256, 2), Texture(VOLUME, 2, 0), (mrhiExtent3d){2, 2, 1}) ==
              mrhi_success,
          "a mip whose depth halved to nothing is one slice");
    Drop();
    CloseDevice();
}

static void TestBlocks(void)
{
    Open();
    Declare(BLOCKS, mrhi_accessCopyDestination, ALL);
    // Mip 1 is 6 by 6 texels, 8 by 8 in whole blocks of 8 bytes.
    mrhiBufferCopy buffer = Buffer(SOURCE, 0, 256, 0);
    CHECK(Upload(buffer, Texture(BLOCKS, 1, 0), (mrhiExtent3d){8, 8, 1}) == mrhi_success,
          "a mip's physical size");
    CHECK(Upload(buffer, Texture(BLOCKS, 1, 0), (mrhiExtent3d){6, 6, 1}) == mrhi_errorInvalid,
          "part of a block");
    CHECK(Upload(buffer, Texture(BLOCKS, 1, 0), (mrhiExtent3d){8, 6, 1}) == mrhi_errorInvalid,
          "part of a block down");
    CHECK(Upload(buffer, Texture(BLOCKS, 1, 0), (mrhiExtent3d){6, 8, 1}) == mrhi_errorInvalid,
          "part of a block across");
    CHECK(Upload(buffer, Texture(BLOCKS, 1, 0), (mrhiExtent3d){12, 8, 1}) == mrhi_errorInvalid,
          "past the physical size");
    mrhiTextureCopy texture = Texture(BLOCKS, 0, 0);
    texture.x = 2;
    CHECK(Upload(buffer, texture, (mrhiExtent3d){4, 4, 1}) == mrhi_errorInvalid,
          "an origin off a block");
    texture.x = 8;
    texture.y = 4;
    CHECK(Upload(buffer, texture, (mrhiExtent3d){4, 4, 1}) == mrhi_success, "on one");
    texture.x = 0;
    texture.y = 2;
    CHECK(Upload(buffer, texture, (mrhiExtent3d){4, 4, 1}) == mrhi_errorInvalid,
          "an origin off a block down");
    CHECK(Upload(Buffer(SOURCE, 4, 256, 0), Texture(BLOCKS, 0, 0), (mrhiExtent3d){4, 4, 1}) ==
              mrhi_errorInvalid,
          "an offset off the block's 8 bytes");
    CHECK(Upload(Buffer(SOURCE, 8, 0, 0), Texture(BLOCKS, 0, 0), (mrhiExtent3d){12, 4, 1}) ==
              mrhi_success,
          "one row of blocks without a layout");
    CHECK(Upload(Buffer(SOURCE, 4072, 0, 0), Texture(BLOCKS, 0, 0), (mrhiExtent3d){12, 4, 1}) ==
                  mrhi_success &&
              Upload(Buffer(SOURCE, 4080, 0, 0), Texture(BLOCKS, 0, 0), (mrhiExtent3d){12, 4, 1}) ==
                  mrhi_errorInvalid,
          "its 24 bytes end the buffer");
    Drop();
    CloseDevice();
}

static void TestDepth(void)
{
    Open();
    mrhiExtent3d whole = {8, 8, 1};
    Declare(DEPTH, mrhi_accessCopySource, ALL);
    mrhiTextureCopy depth = Texture(DEPTH, 0, 0);
    CHECK(Download(depth, Buffer(DESTINATION, 0, 256, 0), whole) == mrhi_success,
          "depth32float's depth copied from");
    depth.aspect = mrhi_aspectDepthOnly;
    CHECK(Download(depth, Buffer(DESTINATION, 4, 256, 0), whole) == mrhi_success,
          "named by its aspect, at an offset of 4");
    CHECK(Download(depth, Buffer(DESTINATION, 2, 256, 0), whole) == mrhi_errorInvalid, "not of 2");
    CHECK(Download(depth, Buffer(DESTINATION, 0, 256, 0), (mrhiExtent3d){4, 4, 1}) ==
              mrhi_errorInvalid,
          "not in part");
    CHECK(Download(depth, Buffer(DESTINATION, 0, 256, 0), (mrhiExtent3d){8, 8, 0}) ==
              mrhi_errorInvalid,
          "nor without its layer");
    mrhiBufferCopy buffer = Buffer(DESTINATION, 0, 256, 0);
    CHECK(mrhiCopyTextureToBuffer(s_device, s_pass, nullptr, &buffer, &whole) ==
                  mrhi_errorInvalid &&
              mrhiCopyTextureToBuffer(s_device, s_pass, &depth, nullptr, &whole) ==
                  mrhi_errorInvalid &&
              mrhiCopyTextureToBuffer(s_device, s_pass, &depth, &buffer, nullptr) ==
                  mrhi_errorInvalid &&
              mrhiCopyBufferToTexture(s_device, s_pass, &buffer, &depth, nullptr) ==
                  mrhi_errorInvalid,
          "NULL arguments");
    CHECK(mrhiCopyTextureToBuffer(nullptr, s_pass, &depth, &buffer, &whole) == mrhi_errorInvalid,
          "no device");
    CHECK(mrhiCopyTextureToBuffer(s_device, s_render, &depth, &buffer, &whole) == mrhi_errorInvalid,
          "a pass with targets");
    Drop();
    Declare(DEPTH, mrhi_accessCopyDestination, ALL);
    CHECK(Upload(Buffer(SOURCE, 0, 256, 0), Texture(DEPTH, 0, 0), whole) == mrhi_errorInvalid,
          "nor copied to");
    Drop();
    mrhiTextureRange stencil = {
        .mipCount = MRHI_REMAINING,
        .layerCount = MRHI_REMAINING,
        .aspect = mrhi_aspectStencilOnly,
    };
    Declare(STENCIL, mrhi_accessCopyDestination, stencil);
    mrhiTextureCopy texture = Texture(STENCIL, 0, 0);
    texture.aspect = mrhi_aspectStencilOnly;
    CHECK(Upload(Buffer(SOURCE, 4, 256, 0), texture, whole) == mrhi_success,
          "the combined format's stencil copied to");
    CHECK(Upload(Buffer(SOURCE, 1, 256, 0), texture, whole) == mrhi_errorInvalid,
          "at an offset of 4, not its byte");
    texture.aspect = mrhi_aspectDepthOnly;
    CHECK(Upload(Buffer(SOURCE, 0, 256, 0), texture, whole) == mrhi_errorInvalid, "not its depth");
    Drop();
    Declare(STENCIL, mrhi_accessCopySource, ALL);
    texture = Texture(STENCIL, 0, 0);
    texture.aspect = mrhi_aspectStencilOnly;
    CHECK(Download(texture, Buffer(DESTINATION, 0, 256, 0), whole) == mrhi_success,
          "its stencil copied from");
    texture.aspect = mrhi_aspectDepthOnly;
    CHECK(Download(texture, Buffer(DESTINATION, 0, 256, 0), whole) == mrhi_errorInvalid,
          "not its depth");
    texture.aspect = mrhi_aspectAll;
    CHECK(Download(texture, Buffer(DESTINATION, 0, 256, 0), whole) == mrhi_errorInvalid,
          "nor both");
    Drop();
    Declare(MULTI_OTHER, mrhi_accessCopyDestination, ALL);
    CHECK(Upload(Buffer(SOURCE, 0, 256, 0), Texture(MULTI_OTHER, 0, 0), whole) == mrhi_errorInvalid,
          "a multisampled texture");
    Drop();
    CloseDevice();
}

// Copies from one texture to another, both declared whole: the result.
static mrhiResult Between(uint32_t from, mrhiTextureAspect fromAspect, uint32_t to,
                          mrhiTextureAspect toAspect, mrhiExtent3d size)
{
    BeginFrame();
    mrhiAccess accesses[] = {
        Access(from, mrhi_accessCopySource),
        Access(to, mrhi_accessCopyDestination),
    };
    Passes(accesses, 2);
    mrhiTextureCopy source = Texture(from, 0, 0);
    source.aspect = fromAspect;
    mrhiTextureCopy destination = Texture(to, 0, 0);
    destination.aspect = toAspect;
    mrhiResult status = mrhiCopyTexture(s_device, s_pass, &source, &destination, &size);
    Drop();
    return status;
}

static void TestTextures(void)
{
    Open();
    mrhiTextureRange first = {.mipCount = MRHI_REMAINING, .layerCount = 1};
    mrhiTextureRange rest = {.mipCount = MRHI_REMAINING, .baseLayer = 1, .layerCount = 2};
    BeginFrame();
    mrhiAccess accesses[] = {
        Access(ARRAY, mrhi_accessCopySource),
        Access(ARRAY, mrhi_accessCopyDestination),
    };
    accesses[0].range = first;
    accesses[1].range = rest;
    Passes(accesses, 2);
    mrhiTextureCopy source = Texture(ARRAY, 0, 0);
    source.x = 4;
    mrhiTextureCopy destination = Texture(ARRAY, 0, 2);
    mrhiExtent3d size = {12, 8, 1};
    CHECK(mrhiCopyTexture(s_device, s_pass, &source, &destination, &size) == mrhi_success,
          "one layer to another");
    const mrhiCommand* command = Nth(0);
    const mrhiCommandTextureSide* from = (const mrhiCommandTextureSide*)Nth(1);
    const mrhiCommandTextureSide* to = (const mrhiCommandTextureSide*)Nth(2);
    CHECK(command != nullptr && command->type == mrhiCommandCopyTexture && command->b == 12 &&
              from->z == 0 && from->x == 4 && to->z == 2 && to->x == 0 &&
              to->object == s_r[ARRAY].index1,
          "recorded");
    size.width = 13;
    CHECK(mrhiCopyTexture(s_device, s_pass, &source, &destination, &size) == mrhi_errorInvalid &&
              Refusal() == mrhi_diagnosticTextureRegion,
          "past the source");
    size.width = 12;
    CHECK(mrhiCopyTexture(s_device, s_pass, &destination, &source, &size) == mrhi_errorInvalid &&
              Refusal() == mrhi_diagnosticUndeclaredAccess,
          "the other way than declared");
    mrhiTextureCopy layer1 = Texture(ARRAY, 0, 1);
    CHECK(mrhiCopyTexture(s_device, s_pass, &layer1, &destination, &size) == mrhi_errorInvalid,
          "from a layer declared as a destination");
    mrhiTextureCopy layer0 = Texture(ARRAY, 0, 0);
    CHECK(mrhiCopyTexture(s_device, s_pass, &source, &layer0, &size) == mrhi_errorInvalid,
          "to a layer declared as a source");
    CHECK(mrhiCopyTexture(s_device, s_pass, nullptr, &destination, &size) == mrhi_errorInvalid &&
              mrhiCopyTexture(s_device, s_pass, &source, nullptr, &size) == mrhi_errorInvalid &&
              mrhiCopyTexture(s_device, s_pass, &source, &destination, nullptr) ==
                  mrhi_errorInvalid,
          "NULL arguments");
    CHECK(mrhiCopyTexture(s_device, s_render, &source, &destination, &size) == mrhi_errorInvalid,
          "a pass with targets");
    mrhiTextureCopy buffer = Texture(SOURCE, 0, 0);
    CHECK(mrhiCopyTexture(s_device, s_pass, &buffer, &destination, &size) == mrhi_errorInvalid,
          "a buffer");
    CHECK(mrhiGetDeviceMisuse(s_device) == 9, "each counted");
    mrhiTextureCopy none = {0};
    CHECK(mrhiCopyTexture(s_device, s_pass, &none, &destination, &size) == mrhi_errorStale &&
              mrhiCopyTexture(s_device, s_pass, &source, &none, &size) == mrhi_errorStale,
          "none");
    CHECK(mrhiCopyTexture(nullptr, s_pass, &source, &destination, &size) == mrhi_errorInvalid,
          "no device");
    Drop();
    mrhiTextureAspect all = mrhi_aspectAll;
    mrhiExtent3d layer = {16, 8, 1};
    CHECK(Between(ARRAY, all, OTHER, all, layer) == mrhi_success, "to another texture");
    CHECK(Between(ARRAY, all, SRGB, all, layer) == mrhi_success &&
              Between(SRGB, all, ARRAY, all, layer) == mrhi_success,
          "between sRGB twins");
    CHECK(Between(ARRAY, all, HALF, all, layer) == mrhi_errorInvalid &&
              Refusal() == mrhi_diagnosticCopyTextureMismatch,
          "not another format");
    mrhiExtent3d whole = {8, 8, 1};
    CHECK(Between(MULTI, all, MULTI_OTHER, all, whole) == mrhi_success, "multisampled, whole");
    CHECK(Between(MULTI, all, MULTI_OTHER, all, (mrhiExtent3d){4, 8, 1}) == mrhi_errorInvalid,
          "not in part");
    CHECK(Between(MULTI, all, SINGLE, all, whole) == mrhi_errorInvalid, "nor into a single sample");
    CHECK(Between(DEPTH, all, DEPTH_OTHER, all, whole) == mrhi_success &&
              Between(DEPTH, mrhi_aspectDepthOnly, DEPTH_OTHER, all, whole) == mrhi_success,
          "depth32float, whole, by its one aspect");
    CHECK(Between(DEPTH, all, DEPTH_OTHER, all, (mrhiExtent3d){8, 4, 1}) == mrhi_errorInvalid,
          "not in part");
    CHECK(Between(STENCIL, all, STENCIL_OTHER, all, whole) == mrhi_success,
          "the combined format, both aspects");
    CHECK(
        Between(STENCIL, mrhi_aspectStencilOnly, STENCIL_OTHER, all, whole) == mrhi_errorInvalid &&
            Between(STENCIL, all, STENCIL_OTHER, mrhi_aspectDepthOnly, whole) == mrhi_errorInvalid,
        "not one of them");
    CloseDevice();
}

int main(void)
{
    ResetAdapter();
    TestBuffers();
    TestClears();
    TestLayouts();
    TestRegions();
    TestBlocks();
    TestDepth();
    TestTextures();
    return s_failures == 0 ? 0 : 1;
}
