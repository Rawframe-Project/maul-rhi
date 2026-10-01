// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The WebGPU conformance test suite's validation cases whose calls have
// Maul RHI counterparts (mrhi-0021), on the test driver: each case names
// its CTS test, the call it becomes and the outcome the CTS expects,
// success or the library's refusal. Cases the library decides otherwise
// are listed with the reason. Translated from the CTS at a5e5d74
// (https://github.com/gpuweb/cts), whose cases are under this notice:
//
//   Copyright 2019 WebGPU CTS Contributors
//
//   Redistribution and use in source and binary forms, with or without
//   modification, are permitted provided that the following conditions
//   are met:
//   1. Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//   2. Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in
//      the documentation and/or other materials provided with the
//      distribution.
//   3. Neither the name of the copyright holder nor the names of its
//      contributors may be used to endorse or promote products derived
//      from this software without specific prior written permission.
//
//   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
//   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
//   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
//   INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
//   BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
//   LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
//   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
//   WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
//   POSSIBILITY OF SUCH DAMAGE.

#include "test_device_setup.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/resources.h"

#include <stdio.h>

static mrhiDevice* s_device;

// The CTS's kMaxSafeMultipleOf8: JavaScript's largest safe integer
// rounded down to a multiple of 8.
#define MAX_SAFE_MULTIPLE_OF_8 UINT64_C(9007199254740984)

// Reports a case whose outcome differs from the CTS's.
static void Expect(const char* test, mrhiResult result, bool succeeds)
{
    bool ok = succeeds ? result == mrhi_success : result == mrhi_errorInvalid;
    if (!ok)
    {
        printf("FAIL: %s: %d, the CTS expects %s\n", test, (int)result,
               succeeds ? "success" : "a validation error");
        ++s_failures;
    }
}

static mrhiBufferId MakeBuffer(uint64_t size, mrhiBufferUsage usage)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage;
    mrhiBufferId buffer = {0};
    CHECK(mrhiCreateBuffer(s_device, &def, &buffer) == mrhi_success, "a buffer");
    return buffer;
}

static mrhiAccess Whole(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
}

// A copyBufferToBuffer case: the buffers' sizes and usages, the offsets
// and size, whether one buffer is both ends, and the CTS's outcome.
typedef struct BufferCopy
{
    const char* test;
    uint64_t sourceSize;
    mrhiBufferUsage sourceUsage;
    uint64_t destinationSize;
    mrhiBufferUsage destinationUsage;
    uint64_t sourceOffset;
    uint64_t destinationOffset;
    uint64_t size;
    bool same;
    bool succeeds;
} BufferCopy;

// The case in a frame: its buffers imported, a transfer pass declaring
// the source's copy source and the destination's copy destination, and
// the copy; the first refusal, of the pass or the copy, or success.
static mrhiResult CopyBuffers(const BufferCopy* c)
{
    mrhiBufferId from = MakeBuffer(c->sourceSize, c->sourceUsage);
    mrhiBufferId to = c->same ? from : MakeBuffer(c->destinationSize, c->destinationUsage);
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId source = {0};
    mrhiResourceId destination = {0};
    CHECK(mrhiBeginFrame(s_device, &frame) == mrhi_success &&
              mrhiImportBuffer(s_device, from, &source) == mrhi_success &&
              (c->same || mrhiImportBuffer(s_device, to, &destination) == mrhi_success),
          "the buffers in a frame");
    destination = c->same ? source : destination;
    const mrhiAccess accesses[2] = {Whole(source, mrhi_accessCopySource),
                                    Whole(destination, mrhi_accessCopyDestination)};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.neverCull = true;
    def.accesses = accesses;
    def.accessCount = 2;
    mrhiPassId pass = {0};
    mrhiResult status = mrhiAddPass(s_device, &def, &pass);
    if (status == mrhi_success)
    {
        CHECK(mrhiCompileFrame(s_device) == mrhi_success &&
                  mrhiBeginPass(s_device, pass) == mrhi_success,
              "the pass begun");
        status = mrhiCopyBuffer(s_device, pass, source, c->sourceOffset, destination,
                                c->destinationOffset, c->size);
    }
    CHECK(mrhiDropFrame(s_device) == mrhi_success &&
              mrhiDestroyBuffer(s_device, from) == mrhi_success &&
              (c->same || mrhiDestroyBuffer(s_device, to) == mrhi_success),
          "dropped");
    return status;
}

#define COPY_SRC mrhi_bufferCopySource
#define COPY_DST mrhi_bufferCopyDestination
#define BOTH     (mrhi_bufferCopySource | mrhi_bufferCopyDestination)

// api,validation,encoding,cmds,copyBufferToBuffer.
static const BufferCopy s_bufferCopies[] = {
    {"copy_size_alignment copySize=0", 16, COPY_SRC, 16, COPY_DST, 0, 0, 0, false, true},
    {"copy_size_alignment copySize=2", 16, COPY_SRC, 16, COPY_DST, 0, 0, 2, false, false},
    {"copy_size_alignment copySize=4", 16, COPY_SRC, 16, COPY_DST, 0, 0, 4, false, true},
    {"copy_size_alignment copySize=5", 16, COPY_SRC, 16, COPY_DST, 0, 0, 5, false, false},
    {"copy_size_alignment copySize=8", 16, COPY_SRC, 16, COPY_DST, 0, 0, 8, false, true},
    {"copy_offset_alignment 0,0", 16, COPY_SRC, 16, COPY_DST, 0, 0, 8, false, true},
    {"copy_offset_alignment 2,0", 16, COPY_SRC, 16, COPY_DST, 2, 0, 8, false, false},
    {"copy_offset_alignment 4,0", 16, COPY_SRC, 16, COPY_DST, 4, 0, 8, false, true},
    {"copy_offset_alignment 5,0", 16, COPY_SRC, 16, COPY_DST, 5, 0, 8, false, false},
    {"copy_offset_alignment 8,0", 16, COPY_SRC, 16, COPY_DST, 8, 0, 8, false, true},
    {"copy_offset_alignment 0,2", 16, COPY_SRC, 16, COPY_DST, 0, 2, 8, false, false},
    {"copy_offset_alignment 0,4", 16, COPY_SRC, 16, COPY_DST, 0, 4, 8, false, true},
    {"copy_offset_alignment 0,5", 16, COPY_SRC, 16, COPY_DST, 0, 5, 8, false, false},
    {"copy_offset_alignment 0,8", 16, COPY_SRC, 16, COPY_DST, 0, 8, 8, false, true},
    {"copy_offset_alignment 4,4", 16, COPY_SRC, 16, COPY_DST, 4, 4, 8, false, true},
    {"copy_overflow 0,0,max", 16, COPY_SRC, 16, COPY_DST, 0, 0, MAX_SAFE_MULTIPLE_OF_8, false,
     false},
    {"copy_overflow 16,0,max", 16, COPY_SRC, 16, COPY_DST, 16, 0, MAX_SAFE_MULTIPLE_OF_8, false,
     false},
    {"copy_overflow 0,16,max", 16, COPY_SRC, 16, COPY_DST, 0, 16, MAX_SAFE_MULTIPLE_OF_8, false,
     false},
    {"copy_overflow max,0,16", 16, COPY_SRC, 16, COPY_DST, MAX_SAFE_MULTIPLE_OF_8, 0, 16, false,
     false},
    {"copy_overflow 0,max,16", 16, COPY_SRC, 16, COPY_DST, 0, MAX_SAFE_MULTIPLE_OF_8, 16, false,
     false},
    {"copy_overflow max,0,max", 16, COPY_SRC, 16, COPY_DST, MAX_SAFE_MULTIPLE_OF_8, 0,
     MAX_SAFE_MULTIPLE_OF_8, false, false},
    {"copy_overflow 0,max,max", 16, COPY_SRC, 16, COPY_DST, 0, MAX_SAFE_MULTIPLE_OF_8,
     MAX_SAFE_MULTIPLE_OF_8, false, false},
    {"copy_overflow max,max,max", 16, COPY_SRC, 16, COPY_DST, MAX_SAFE_MULTIPLE_OF_8,
     MAX_SAFE_MULTIPLE_OF_8, MAX_SAFE_MULTIPLE_OF_8, false, false},
    {"copy_out_of_bounds 0,0,32", 32, COPY_SRC, 32, COPY_DST, 0, 0, 32, false, true},
    {"copy_out_of_bounds 0,0,36", 32, COPY_SRC, 32, COPY_DST, 0, 0, 36, false, false},
    {"copy_out_of_bounds 36,0,4", 32, COPY_SRC, 32, COPY_DST, 36, 0, 4, false, false},
    {"copy_out_of_bounds 0,36,4", 32, COPY_SRC, 32, COPY_DST, 0, 36, 4, false, false},
    {"copy_out_of_bounds 36,0,0", 32, COPY_SRC, 32, COPY_DST, 36, 0, 0, false, false},
    {"copy_out_of_bounds 0,36,0", 32, COPY_SRC, 32, COPY_DST, 0, 36, 0, false, false},
    {"copy_out_of_bounds 20,0,16", 32, COPY_SRC, 32, COPY_DST, 20, 0, 16, false, false},
    {"copy_out_of_bounds 20,0,12", 32, COPY_SRC, 32, COPY_DST, 20, 0, 12, false, true},
    {"copy_out_of_bounds 0,20,16", 32, COPY_SRC, 32, COPY_DST, 0, 20, 16, false, false},
    {"copy_out_of_bounds 0,20,12", 32, COPY_SRC, 32, COPY_DST, 0, 20, 12, false, true},
    {"copy_within_same_buffer 0,8,4", 16, BOTH, 16, BOTH, 0, 8, 4, true, false},
    {"copy_within_same_buffer 8,0,4", 16, BOTH, 16, BOTH, 8, 0, 4, true, false},
    {"copy_within_same_buffer 0,4,8", 16, BOTH, 16, BOTH, 0, 4, 8, true, false},
    {"copy_within_same_buffer 4,0,8", 16, BOTH, 16, BOTH, 4, 0, 8, true, false},
};

// The usages a buffer is made with, as the CTS's kBufferUsages less
// MAP_READ and MAP_WRITE, which Maul RHI has no counterpart of (its
// uploads and readbacks are the frame's).
static const mrhiBufferUsage s_usages[] = {
    mrhi_bufferCopySource, mrhi_bufferCopyDestination, mrhi_bufferIndex,    mrhi_bufferVertex,
    mrhi_bufferUniform,    mrhi_bufferStorage,         mrhi_bufferIndirect, mrhi_bufferQueryResolve,
};

static void TestBufferCopies(void)
{
    char name[96];
    for (size_t i = 0; i < sizeof(s_bufferCopies) / sizeof(s_bufferCopies[0]); ++i)
    {
        const BufferCopy* c = &s_bufferCopies[i];
        snprintf(name, sizeof(name), "copyBufferToBuffer:%s", c->test);
        Expect(name, CopyBuffers(c), c->succeeds);
    }
    // buffer_usage: success only from a copy source to a copy
    // destination.
    size_t count = sizeof(s_usages) / sizeof(s_usages[0]);
    for (size_t from = 0; from < count; ++from)
    {
        for (size_t to = 0; to < count; ++to)
        {
            const BufferCopy c = {"", 16, s_usages[from], 16, s_usages[to], 0, 0, 8, false, false};
            snprintf(name, sizeof(name), "copyBufferToBuffer:buffer_usage %#x,%#x",
                     (unsigned)s_usages[from], (unsigned)s_usages[to]);
            Expect(name, CopyBuffers(&c), s_usages[from] == COPY_SRC && s_usages[to] == COPY_DST);
        }
    }
}

// A texture of a copyTextureToTexture case.
typedef struct TextureShape
{
    mrhiTextureKind kind;
    uint32_t width;
    uint32_t height;
    uint32_t layers;
    uint32_t mips;
    uint32_t samples;
    mrhiTextureUsage usage;
} TextureShape;

static mrhiTextureId MakeTexture(const TextureShape* shape)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.kind = shape->kind;
    def.format = mrhi_formatRgba8Unorm;
    def.width = shape->width;
    def.height = shape->height;
    def.depthOrLayers = shape->layers;
    def.mipLevels = shape->mips;
    def.sampleCount = shape->samples;
    def.usage = shape->usage;
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(s_device, &def, &texture) == mrhi_success, "a texture");
    return texture;
}

// A copyTextureToTexture case in a frame: its textures (one for a copy
// within a texture, declared by the layers each side touches), a
// transfer pass declaring the source's copy source and the
// destination's copy destination, and the copy; the first refusal, or
// success.
static mrhiResult CopyTextures(const TextureShape* fromShape, const TextureShape* toShape,
                               mrhiTextureCopy from, mrhiTextureCopy to, mrhiExtent3d size)
{
    mrhiTextureId source = MakeTexture(fromShape);
    mrhiTextureId destination = toShape == nullptr ? source : MakeTexture(toShape);
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &frame) == mrhi_success &&
              mrhiImportTexture(s_device, source, &from.resource) == mrhi_success &&
              (toShape == nullptr ||
               mrhiImportTexture(s_device, destination, &to.resource) == mrhi_success),
          "the textures in a frame");
    to.resource = toShape == nullptr ? from.resource : to.resource;
    mrhiAccess accesses[2] = {Whole(from.resource, mrhi_accessCopySource),
                              Whole(to.resource, mrhi_accessCopyDestination)};
    if (toShape == nullptr)
    {
        accesses[0].range = (mrhiTextureRange){
            .mipCount = 1, .baseLayer = from.z, .layerCount = size.depthOrLayers};
        accesses[1].range =
            (mrhiTextureRange){.mipCount = 1, .baseLayer = to.z, .layerCount = size.depthOrLayers};
    }
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.neverCull = true;
    def.accesses = accesses;
    def.accessCount = 2;
    mrhiPassId pass = {0};
    mrhiResult status = mrhiAddPass(s_device, &def, &pass);
    if (status == mrhi_success)
    {
        CHECK(mrhiCompileFrame(s_device) == mrhi_success &&
                  mrhiBeginPass(s_device, pass) == mrhi_success,
              "the pass begun");
        status = mrhiCopyTexture(s_device, pass, &from, &to, &size);
    }
    CHECK(mrhiDropFrame(s_device) == mrhi_success &&
              mrhiDestroyTexture(s_device, source) == mrhi_success &&
              (toShape == nullptr || mrhiDestroyTexture(s_device, destination) == mrhi_success),
          "dropped");
    return status;
}

#define TEX_SRC mrhi_textureCopySource
#define TEX_DST mrhi_textureCopyDestination

// The CTS's 2D and 3D dimensions as Maul RHI's kinds: a 2D texture of
// several layers is a 2D array. Its 1D dimension has no counterpart.
static const mrhiTextureKind s_kinds[] = {mrhi_texture2dArray, mrhi_texture3d};

// api,validation,encoding,cmds,copyTextureToTexture.
static void TestTextureCopies(void)
{
    char name[128];
    // mipmap_level.
    static const uint32_t levels[][4] = {
        {1, 1, 0, 0}, {1, 1, 1, 0}, {1, 1, 0, 1}, {3, 3, 0, 0},
        {3, 3, 2, 0}, {3, 3, 3, 0}, {3, 3, 0, 2}, {3, 3, 0, 3},
    };
    for (size_t k = 0; k < 2; ++k)
    {
        for (size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i)
        {
            const TextureShape from = {s_kinds[k], 32, 1, 1, levels[i][0], 1, TEX_SRC};
            const TextureShape to = {s_kinds[k], 32, 1, 1, levels[i][1], 1, TEX_DST};
            snprintf(name, sizeof(name), "copyTextureToTexture:mipmap_level kind=%d %u,%u,%u,%u",
                     (int)s_kinds[k], levels[i][0], levels[i][1], levels[i][2], levels[i][3]);
            Expect(name,
                   CopyTextures(&from, &to, (mrhiTextureCopy){.mip = levels[i][2]},
                                (mrhiTextureCopy){.mip = levels[i][3]}, (mrhiExtent3d){1, 1, 1}),
                   levels[i][2] < levels[i][0] && levels[i][3] < levels[i][1]);
        }
    }
    // sample_count.
    static const uint32_t samples[] = {1, 4};
    for (size_t s = 0; s < 2; ++s)
    {
        for (size_t d = 0; d < 2; ++d)
        {
            const TextureShape from = {
                mrhi_texture2d, 4, 4, 1, 1, samples[s], TEX_SRC | mrhi_textureRenderTarget};
            const TextureShape to = {
                mrhi_texture2d, 4, 4, 1, 1, samples[d], TEX_DST | mrhi_textureRenderTarget};
            snprintf(name, sizeof(name), "copyTextureToTexture:sample_count %u,%u", samples[s],
                     samples[d]);
            Expect(name,
                   CopyTextures(&from, &to, (mrhiTextureCopy){0}, (mrhiTextureCopy){0},
                                (mrhiExtent3d){4, 4, 1}),
                   samples[s] == samples[d]);
        }
    }
    // copy_within_same_texture: one 2D texture of 7 layers.
    static const uint32_t origins[] = {0, 2, 4};
    for (size_t s = 0; s < 3; ++s)
    {
        for (size_t d = 0; d < 3; ++d)
        {
            for (uint32_t depth = 1; depth <= 3; ++depth)
            {
                const TextureShape shape = {mrhi_texture2dArray, 16, 16, 7, 1, 1,
                                            TEX_SRC | TEX_DST};
                uint32_t low = origins[s] < origins[d] ? origins[s] : origins[d];
                uint32_t high = origins[s] < origins[d] ? origins[d] : origins[s];
                snprintf(name, sizeof(name),
                         "copyTextureToTexture:copy_within_same_texture %u,%u,%u", origins[s],
                         origins[d], depth);
                Expect(name,
                       CopyTextures(&shape, nullptr, (mrhiTextureCopy){.z = origins[s]},
                                    (mrhiTextureCopy){.z = origins[d]},
                                    (mrhiExtent3d){16, 16, depth}),
                       low + depth <= high);
            }
        }
    }
    // copy_ranges: a 16 by 8 by 3 texture of 4 mips, a box offset from a
    // whole copy at each pair of levels, both ways round.
    static const int32_t boxes[][6] = {
        {0, 0, 0, 0, 0, -2},  {1, 0, 0, 0, 0, -2}, {1, 0, 0, -1, 0, -2}, {0, 1, 0, 0, 0, -2},
        {0, 1, 0, 0, -1, -2}, {0, 0, 1, 0, 1, -2}, {0, 0, 2, 0, 1, 0},   {0, 0, 0, 1, 0, -2},
        {0, 0, 0, 0, 1, -2},  {0, 0, 0, 0, 0, 1},  {0, 0, 0, 0, 0, 0},   {0, 0, 1, 0, 0, -1},
        {0, 0, 2, 0, 0, -1},
    };
    static const uint32_t copyLevels[] = {0, 1, 3};
    for (size_t k = 0; k < 2; ++k)
    {
        bool volume = s_kinds[k] == mrhi_texture3d;
        for (size_t b = 0; b < sizeof(boxes) / sizeof(boxes[0]); ++b)
        {
            for (size_t sl = 0; sl < 3; ++sl)
            {
                for (size_t dl = 0; dl < 3; ++dl)
                {
                    const TextureShape from = {s_kinds[k], 16, 8, 3, 4, 1, TEX_SRC};
                    const TextureShape to = {s_kinds[k], 16, 8, 3, 4, 1, TEX_DST};
                    uint32_t fromLevel = copyLevels[sl];
                    uint32_t toLevel = copyLevels[dl];
                    int32_t fromWidth = 16 >> fromLevel > 0 ? 16 >> fromLevel : 1;
                    int32_t fromHeight = 8 >> fromLevel > 0 ? 8 >> fromLevel : 1;
                    int32_t fromDepth = volume ? (3 >> fromLevel > 0 ? 3 >> fromLevel : 1) : 3;
                    int32_t toWidth = 16 >> toLevel > 0 ? 16 >> toLevel : 1;
                    int32_t toHeight = 8 >> toLevel > 0 ? 8 >> toLevel : 1;
                    int32_t toDepth = volume ? (3 >> toLevel > 0 ? 3 >> toLevel : 1) : 3;
                    const int32_t* box = boxes[b];
                    int32_t width = (fromWidth < toWidth ? fromWidth : toWidth) + box[3] - box[0];
                    int32_t height =
                        (fromHeight < toHeight ? fromHeight : toHeight) + box[4] - box[1];
                    width = width > 0 ? width : 0;
                    height = height > 0 ? height : 0;
                    int32_t depth = 3 + box[5] - box[2];
                    const mrhiExtent3d size = {(uint32_t)width, (uint32_t)height, (uint32_t)depth};
                    // The box at the destination, then at the source.
                    bool into = width <= fromWidth && height <= fromHeight &&
                                box[0] + width <= toWidth && box[1] + height <= toHeight &&
                                depth <= fromDepth && box[2] + depth <= toDepth;
                    bool outOf = box[0] + width <= fromWidth && box[1] + height <= fromHeight &&
                                 width <= toWidth && height <= toHeight && depth <= toDepth &&
                                 box[2] + depth <= fromDepth;
                    const mrhiTextureCopy origin = {.mip = 0};
                    mrhiTextureCopy at = {
                        .x = (uint32_t)box[0], .y = (uint32_t)box[1], .z = (uint32_t)box[2]};
                    mrhiTextureCopy fromAt = origin;
                    fromAt.mip = fromLevel;
                    at.mip = toLevel;
                    snprintf(name, sizeof(name),
                             "copyTextureToTexture:copy_ranges kind=%d box=%zu levels=%u,%u into",
                             (int)s_kinds[k], b, fromLevel, toLevel);
                    Expect(name, CopyTextures(&from, &to, fromAt, at, size), into);
                    at.mip = fromLevel;
                    mrhiTextureCopy toAt = origin;
                    toAt.mip = toLevel;
                    snprintf(name, sizeof(name),
                             "copyTextureToTexture:copy_ranges kind=%d box=%zu levels=%u,%u out",
                             (int)s_kinds[k], b, fromLevel, toLevel);
                    Expect(name, CopyTextures(&from, &to, at, toAt, size), outOf);
                }
            }
        }
    }
}

// api,validation,image_copy,layout_related: WebGPU's texel block facts
// as the CTS's tables give them (the oracle never asks the library), and
// the features a compressed format needs.
typedef struct Block
{
    mrhiFormat format;
    uint32_t bytes;
    uint32_t width;
    uint32_t height;
    bool compressed;
} Block;

static const Block s_blocks[] = {
    {mrhi_formatR8Unorm, 1, 1, 1, false},      {mrhi_formatRg8Unorm, 2, 1, 1, false},
    {mrhi_formatRgba8Unorm, 4, 1, 1, false},   {mrhi_formatRgba16Float, 8, 1, 1, false},
    {mrhi_formatRgba32Float, 16, 1, 1, false}, {mrhi_formatBc1RgbaUnorm, 8, 4, 4, true},
    {mrhi_formatBc7RgbaUnorm, 16, 4, 4, true}, {mrhi_formatEtc2Rgb8Unorm, 8, 4, 4, true},
    {mrhi_formatAstc8x6Unorm, 16, 8, 6, true},
};

// The CTS's three copy methods.
typedef enum Method
{
    WRITE_TEXTURE,
    BUFFER_TO_TEXTURE,
    TEXTURE_TO_BUFFER,
} Method;

static const char* const s_methods[] = {"WriteTexture", "CopyB2T", "CopyT2B"};

// A layout value the CTS leaves undefined.
#define ABSENT INT64_C(-1)

// The CTS's dataBytesForCopyOrOverestimate: the bytes a copy needs from
// its offset (an overestimate where the layout is invalid) and whether
// the layout is valid. Layout values may be ABSENT.
typedef struct Need
{
    uint64_t bytes;
    bool valid;
} Need;

static Need DataBytes(Method method, const Block* block, uint64_t offset, int64_t bytesPerRow,
                      int64_t rowsPerImage, mrhiExtent3d size)
{
    uint64_t width = size.width / block->width;
    uint64_t height = size.height / block->height;
    uint64_t depth = size.depthOrLayers;
    uint64_t lastRow = width * block->bytes;
    bool valid = true;
    if (method != WRITE_TEXTURE &&
        (offset % block->bytes != 0 || (bytesPerRow > 0 && bytesPerRow % 256 != 0)))
    {
        valid = false;
    }
    if ((bytesPerRow != ABSENT && (uint64_t)bytesPerRow < lastRow) ||
        (bytesPerRow == ABSENT && (height > 1 || depth > 1)))
    {
        bytesPerRow = ABSENT;
        valid = false;
    }
    if ((rowsPerImage != ABSENT && (uint64_t)rowsPerImage < height) ||
        (rowsPerImage == ABSENT && depth > 1))
    {
        rowsPerImage = ABSENT;
        valid = false;
    }
    uint64_t perRow =
        bytesPerRow != ABSENT ? (uint64_t)bytesPerRow : (block->bytes * width + 255) / 256 * 256;
    uint64_t perImage = rowsPerImage != ABSENT ? (uint64_t)rowsPerImage : height;
    uint64_t required = 0;
    if (depth > 1)
    {
        required += perRow * perImage * (depth - 1);
    }
    if (depth > 0)
    {
        required += height > 1 ? perRow * (height - 1) : 0;
        required += height > 0 ? lastRow : 0;
    }
    return (Need){offset + required, valid};
}

// What a transfer case comes to: the library's outcome, or none where
// Maul RHI cannot express its data. The CTS's buffers take any size,
// Maul RHI's a positive multiple of 4: a case runs with its size rounded
// up when the copy's need does not fall between the two, which keeps
// its outcome.
#define INEXPRESSIBLE ((mrhiResult)1000)

static uint8_t s_data[4u << 20];

// A transfer case in a frame: a texture of the block's format, its
// buffer or bytes of dataSize, a transfer pass declaring both, and the
// copy at the texture's origin.
static mrhiResult Transfer(Method method, const Block* block, mrhiTextureKind kind,
                           mrhiExtent3d textureSize, uint64_t offset, uint32_t bytesPerRow,
                           uint32_t rowsPerImage, uint64_t dataSize, mrhiExtent3d size)
{
    if (method != WRITE_TEXTURE && (dataSize == 0 || dataSize % 4 != 0))
    {
        uint64_t rounded = dataSize == 0 ? 4 : (dataSize + 3) / 4 * 4;
        Need need = DataBytes(method, block, offset, bytesPerRow == 0 ? ABSENT : bytesPerRow,
                              rowsPerImage == 0 ? ABSENT : rowsPerImage, size);
        if (need.valid && dataSize < need.bytes && need.bytes <= rounded)
        {
            return INEXPRESSIBLE;
        }
        dataSize = rounded;
    }
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.kind = kind;
    textureDef.format = block->format;
    textureDef.width = textureSize.width;
    textureDef.height = textureSize.height;
    textureDef.depthOrLayers = textureSize.depthOrLayers;
    textureDef.usage = mrhi_textureCopySource | mrhi_textureCopyDestination;
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(s_device, &textureDef, &texture) == mrhi_success, "a texture");
    mrhiBufferId buffer = {0};
    if (method != WRITE_TEXTURE)
    {
        buffer = MakeBuffer(dataSize, mrhi_bufferCopySource | mrhi_bufferCopyDestination);
    }
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId t = {0};
    mrhiResourceId b = {0};
    CHECK(mrhiBeginFrame(s_device, &frame) == mrhi_success &&
              mrhiImportTexture(s_device, texture, &t) == mrhi_success &&
              (method == WRITE_TEXTURE || mrhiImportBuffer(s_device, buffer, &b) == mrhi_success),
          "a frame of them");
    bool intoTexture = method != TEXTURE_TO_BUFFER;
    const mrhiAccess accesses[2] = {
        Whole(t, intoTexture ? mrhi_accessCopyDestination : mrhi_accessCopySource),
        Whole(b, intoTexture ? mrhi_accessCopySource : mrhi_accessCopyDestination)};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.neverCull = true;
    def.accesses = accesses;
    def.accessCount = method == WRITE_TEXTURE ? 1 : 2;
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success &&
              mrhiCompileFrame(s_device) == mrhi_success &&
              mrhiBeginPass(s_device, pass) == mrhi_success,
          "the pass begun");
    const mrhiTextureCopy at = {.resource = t};
    const mrhiBufferCopy side = {
        .resource = b, .offset = offset, .bytesPerRow = bytesPerRow, .rowsPerImage = rowsPerImage};
    const mrhiTexelLayout layout = {
        .offset = offset, .bytesPerRow = bytesPerRow, .rowsPerImage = rowsPerImage};
    mrhiResult status = mrhi_success;
    switch (method)
    {
    case WRITE_TEXTURE:
        status = mrhiWriteTexture(s_device, pass, &at, dataSize > 0 ? s_data : nullptr, dataSize,
                                  &layout, &size);
        break;
    case BUFFER_TO_TEXTURE:
        status = mrhiCopyBufferToTexture(s_device, pass, &side, &at, &size);
        break;
    case TEXTURE_TO_BUFFER:
        status = mrhiCopyTextureToBuffer(s_device, pass, &at, &side, &size);
        break;
    }
    CHECK(mrhiDropFrame(s_device) == mrhi_success &&
              mrhiDestroyTexture(s_device, texture) == mrhi_success &&
              (method == WRITE_TEXTURE || mrhiDestroyBuffer(s_device, buffer) == mrhi_success),
          "dropped");
    return status;
}

// Counts the cases Maul RHI cannot express, which are counted rather
// than run: required_bytes_in_copy's data one byte short of a buffer's
// need that is a multiple of 4, and bound_on_offset's offset past
// data of 0 bytes, in copies with a buffer.
static uint32_t s_inexpressible;

// Counts the cases Maul RHI decides otherwise than the CTS, all from
// its layout value 0 being absent (see Owed):
// - an empty copy of several layers giving 0 for a layout value, which
//   the CTS takes and Maul RHI refuses as a value missing where needed;
// - a rowsPerImage of 0 for a copy of one layer with rows, which the CTS
//   refuses as fewer than the rows and Maul RHI takes as absent.
static uint32_t s_differs;

// The outcome Maul RHI owes a transfer case. Its layout value 0 is an
// absent one, which the web driver passes as undefined, where the CTS's
// 0 is a value: a case giving 0 is owed the outcome of the layout
// without it, and counted when that differs from the CTS's.
static bool Owed(Method method, const Block* block, uint64_t offset, int64_t bytesPerRow,
                 int64_t rowsPerImage, uint64_t dataSize, mrhiExtent3d size, bool cts)
{
    if (bytesPerRow != 0 && rowsPerImage != 0)
    {
        return cts;
    }
    Need need = DataBytes(method, block, offset, bytesPerRow == 0 ? ABSENT : bytesPerRow,
                          rowsPerImage == 0 ? ABSENT : rowsPerImage, size);
    bool owed = need.valid && need.bytes <= dataSize;
    s_differs += owed != cts ? 1 : 0;
    return owed;
}

// A CTS layout value as Maul RHI's: absent is 0.
static uint32_t Value(int64_t value)
{
    return value == ABSENT ? 0 : (uint32_t)value;
}

static void ExpectTransfer(const char* test, mrhiResult result, bool succeeds)
{
    if (result == INEXPRESSIBLE)
    {
        ++s_inexpressible;
        return;
    }
    Expect(test, result, succeeds);
}

// The CTS's createAlignedTexture: the copy's size, at least one texel,
// in whole blocks.
static mrhiExtent3d AlignedSize(const Block* block, mrhiExtent3d size)
{
    uint32_t width = size.width > 0 ? size.width : 1;
    uint32_t height = size.height > 0 ? size.height : 1;
    return (mrhiExtent3d){(width + block->width - 1) / block->width * block->width,
                          (height + block->height - 1) / block->height * block->height,
                          size.depthOrLayers > 0 ? size.depthOrLayers : 1};
}

// The CTS's 2D dimension as a 2D texture, or a 2D array of its layers.
static mrhiTextureKind KindOf(bool volume, uint32_t layers)
{
    return volume ? mrhi_texture3d : layers > 1 ? mrhi_texture2dArray : mrhi_texture2d;
}

static void TestLayouts(void)
{
    char name[160];
    const Block* rgba8 = &s_blocks[2];
    for (int m = 0; m < 3; ++m)
    {
        Method method = (Method)m;
        // bound_on_rows_per_image.
        static const struct
        {
            bool volume;
            uint32_t layers;
        } shapes[] = {{false, 1}, {false, 3}, {true, 3}};
        static const int64_t imageRows[] = {ABSENT, 0, 1, 2, 1024};
        for (size_t s = 0; s < 3; ++s)
        {
            for (size_t r = 0; r < 5; ++r)
            {
                for (uint32_t rows = 0; rows <= 2; ++rows)
                {
                    for (uint32_t depth = 1; depth <= 3; depth += 2)
                    {
                        if (depth > shapes[s].layers)
                        {
                            continue;
                        }
                        const mrhiExtent3d size = {0, rows, depth};
                        Need need = DataBytes(method, rgba8, 0, 1024, imageRows[r], size);
                        snprintf(name, sizeof(name),
                                 "layout_related:bound_on_rows_per_image %s %s rpi=%lld h=%u d=%u",
                                 s_methods[m], shapes[s].volume ? "3d" : "2d",
                                 (long long)imageRows[r], rows, depth);
                        ExpectTransfer(name,
                                       Transfer(method, rgba8,
                                                KindOf(shapes[s].volume, shapes[s].layers),
                                                (mrhiExtent3d){4, 4, shapes[s].layers}, 0, 1024,
                                                Value(imageRows[r]), need.bytes, size),
                                       Owed(method, rgba8, 0, 1024, imageRows[r], need.bytes, size,
                                            need.valid));
                    }
                }
            }
        }
        // copy_end_overflows_u64.
        for (uint32_t depth = 1; depth <= 16; depth += 15)
        {
            snprintf(name, sizeof(name), "layout_related:copy_end_overflows_u64 %s d=%u",
                     s_methods[m], depth);
            ExpectTransfer(name,
                           Transfer(method, rgba8, KindOf(false, depth),
                                    (mrhiExtent3d){1, 1, depth}, 0, 1u << 31, 1u << 31, 10000,
                                    (mrhiExtent3d){1, 1, depth}),
                           depth == 1);
        }
        // bound_on_offset: an empty copy, whose offset still may not pass
        // the data.
        for (uint64_t offset = 0; offset <= 8; offset += 4)
        {
            for (uint64_t data = 0; data <= 8; data += 4)
            {
                snprintf(name, sizeof(name), "layout_related:bound_on_offset %s %llu,%llu",
                         s_methods[m], (unsigned long long)offset, (unsigned long long)data);
                ExpectTransfer(name,
                               Transfer(method, rgba8, mrhi_texture2d, (mrhiExtent3d){4, 4, 1},
                                        offset, 0, 0, data, (mrhiExtent3d){0, 0, 0}),
                               Owed(method, rgba8, offset, 0, ABSENT, data, (mrhiExtent3d){0, 0, 0},
                                    offset <= data));
            }
        }
        for (size_t f = 0; f < sizeof(s_blocks) / sizeof(s_blocks[0]); ++f)
        {
            const Block* block = &s_blocks[f];
            const mrhiExtent3d one = {block->width, block->height, 1};
            // rows_per_image_alignment: no alignment beyond the block.
            for (uint32_t rows = 0; rows <= 3 * block->height; ++rows)
            {
                if ((rows > 2 * block->height && rows != 3 * block->height) || rows < block->height)
                {
                    continue;
                }
                snprintf(name, sizeof(name), "layout_related:rows_per_image_alignment %s f=%zu %u",
                         s_methods[m], f, rows);
                ExpectTransfer(
                    name,
                    Transfer(method, block, mrhi_texture2d, one, 0, 256, rows, block->bytes, one),
                    true);
            }
            // offset_alignment.
            for (uint32_t offset = 0; offset <= 3 * block->bytes; ++offset)
            {
                if (offset > 2 * block->bytes && offset != 3 * block->bytes)
                {
                    continue;
                }
                snprintf(name, sizeof(name), "layout_related:offset_alignment %s f=%zu %u",
                         s_methods[m], f, offset);
                ExpectTransfer(name,
                               Transfer(method, block, mrhi_texture2d, one, offset, 256, 0,
                                        offset + block->bytes, one),
                               method == WRITE_TEXTURE || offset % block->bytes == 0);
            }
            // bound_on_bytes_per_row.
            for (uint32_t rows = 1; rows <= 2; ++rows)
            {
                for (uint32_t depth = 1; depth <= 2; ++depth)
                {
                    uint32_t b = block->bytes;
                    const struct
                    {
                        int64_t bytesPerRow;
                        uint32_t width;
                        uint32_t copyWidth;
                        bool succeeds;
                    } rows_[] = {
                        {256, 256 / b, 256 / b, true},
                        {256, 256 / b, 256 / b - 1, true},
                        {128, 128 / b, 128 / b, method == WRITE_TEXTURE},
                        {384, 384 / b, 384 / b, method == WRITE_TEXTURE},
                        {256, 512 / b, 512 / b, false},
                        {ABSENT, 256 / b, 256 / b, !(rows > 1 || depth > 1)},
                    };
                    for (size_t c = 0; c < 6; ++c)
                    {
                        const mrhiExtent3d size = {rows_[c].copyWidth * block->width,
                                                   rows * block->height, depth};
                        Need need = DataBytes(method, block, 0, rows_[c].bytesPerRow, rows, size);
                        snprintf(
                            name, sizeof(name),
                            "layout_related:bound_on_bytes_per_row %s f=%zu h=%u d=%u case=%zu",
                            s_methods[m], f, rows, depth, c);
                        ExpectTransfer(name,
                                       Transfer(method, block, KindOf(false, depth),
                                                (mrhiExtent3d){rows_[c].width * block->width,
                                                               rows * block->height, depth},
                                                0, Value(rows_[c].bytesPerRow), rows, need.bytes,
                                                size),
                                       rows_[c].succeeds);
                    }
                }
            }
            // required_bytes_in_copy: the least data succeeds, one byte
            // less fails.
            static const uint32_t paddings[][2] = {{0, 0}, {0, 6}, {6, 0}, {15, 17}};
            static const uint32_t copies[][4] = {
                {3, 4, 5, 0},  {5, 4, 3, 11}, {256, 3, 2, 0}, {0, 4, 5, 0}, {3, 0, 5, 0},
                {3, 4, 0, 13}, {1, 4, 5, 0},  {3, 1, 5, 15},  {5, 4, 1, 0}, {7, 1, 1, 0},
            };
            uint32_t alignment = method == WRITE_TEXTURE ? 1 : 256;
            for (int volume = 0; volume <= (block->compressed ? 0 : 1); ++volume)
            {
                for (size_t p = 0; p < 4; ++p)
                {
                    for (size_t c = 0; c < 10; ++c)
                    {
                        const mrhiExtent3d size = {copies[c][0] * block->width,
                                                   copies[c][1] * block->height, copies[c][2]};
                        uint64_t offset = (uint64_t)copies[c][3] * block->bytes;
                        uint32_t rowsPerImage = size.height + paddings[p][1] * block->height;
                        uint32_t row = copies[c][0] * block->bytes;
                        uint32_t bytesPerRow = (row + alignment - 1) / alignment * alignment +
                                               paddings[p][0] * alignment;
                        Need need =
                            DataBytes(method, block, offset, bytesPerRow, rowsPerImage, size);
                        mrhiExtent3d textureSize = AlignedSize(block, size);
                        mrhiTextureKind kind = KindOf(volume != 0, textureSize.depthOrLayers);
                        snprintf(name, sizeof(name),
                                 "layout_related:required_bytes_in_copy %s f=%zu %s pad=%zu "
                                 "copy=%zu",
                                 s_methods[m], f, volume ? "3d" : "2d", p, c);
                        ExpectTransfer(name,
                                       Transfer(method, block, kind, textureSize, offset,
                                                bytesPerRow, rowsPerImage, need.bytes, size),
                                       Owed(method, block, offset, bytesPerRow, rowsPerImage,
                                            need.bytes, size, true));
                        if (need.bytes > 0)
                        {
                            snprintf(name, sizeof(name),
                                     "layout_related:required_bytes_in_copy %s f=%zu %s pad=%zu "
                                     "copy=%zu short",
                                     s_methods[m], f, volume ? "3d" : "2d", p, c);
                            ExpectTransfer(name,
                                           Transfer(method, block, kind, textureSize, offset,
                                                    bytesPerRow, rowsPerImage, need.bytes - 1,
                                                    size),
                                           Owed(method, block, offset, bytesPerRow, rowsPerImage,
                                                need.bytes - 1, size, false));
                        }
                    }
                }
            }
        }
    }
}

int main(void)
{
    ResetAdapter();
    // The compressed families, for their formats' layouts.
    s_adapter.features.textureCompressionBc = true;
    s_adapter.features.textureCompressionEtc2 = true;
    s_adapter.features.textureCompressionAstc = true;
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.features = s_adapter.features;
    s_device = OpenWith(deviceDef, true);
    TestBufferCopies();
    TestTextureCopies();
    TestLayouts();
    // The counts are pinned so that a change of either is seen.
    CHECK(s_differs == 192, "the cases decided otherwise");
    CHECK(s_inexpressible == 962, "the cases not expressible");
    Close(s_device);
    return s_failures == 0 ? 0 : 1;
}
