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

#include "maul-rhi/device.h"
#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/shader.h"
#include "shaders/noop_container.h"

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
// A transfer case: the method, the texture's block facts and def, the
// texel copy's origin and mip, the layout, the data's size and the
// copy's size.
typedef struct TransferCase
{
    Method method;
    const Block* block;
    mrhiTextureDef texture;
    mrhiTextureCopy at;
    uint64_t offset;
    uint32_t bytesPerRow;
    uint32_t rowsPerImage;
    uint64_t dataSize;
    mrhiExtent3d size;
} TransferCase;

// The case in a frame: its texture, its buffer or bytes of the data's
// size, a transfer pass declaring both, and the copy; the first refusal,
// of the texture, the pass or the copy, or success.
static mrhiResult RunTransfer(const TransferCase* c)
{
    uint64_t dataSize = c->dataSize;
    if (c->method != WRITE_TEXTURE && (dataSize == 0 || dataSize % 4 != 0))
    {
        uint64_t rounded = dataSize == 0 ? 4 : (dataSize + 3) / 4 * 4;
        Need need =
            DataBytes(c->method, c->block, c->offset, c->bytesPerRow == 0 ? ABSENT : c->bytesPerRow,
                      c->rowsPerImage == 0 ? ABSENT : c->rowsPerImage, c->size);
        if (need.valid && dataSize < need.bytes && need.bytes <= rounded)
        {
            return INEXPRESSIBLE;
        }
        dataSize = rounded;
    }
    mrhiTextureId texture = {0};
    mrhiResult status = mrhiCreateTexture(s_device, &c->texture, &texture);
    if (status != mrhi_success)
    {
        return status;
    }
    mrhiBufferId buffer = {0};
    if (c->method != WRITE_TEXTURE)
    {
        buffer = MakeBuffer(dataSize, mrhi_bufferCopySource | mrhi_bufferCopyDestination);
    }
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiTextureCopy at = c->at;
    mrhiResourceId b = {0};
    CHECK(
        mrhiBeginFrame(s_device, &frame) == mrhi_success &&
            mrhiImportTexture(s_device, texture, &at.resource) == mrhi_success &&
            (c->method == WRITE_TEXTURE || mrhiImportBuffer(s_device, buffer, &b) == mrhi_success),
        "a frame of them");
    bool intoTexture = c->method != TEXTURE_TO_BUFFER;
    const mrhiAccess accesses[2] = {
        Whole(at.resource, intoTexture ? mrhi_accessCopyDestination : mrhi_accessCopySource),
        Whole(b, intoTexture ? mrhi_accessCopySource : mrhi_accessCopyDestination)};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.neverCull = true;
    def.accesses = accesses;
    def.accessCount = c->method == WRITE_TEXTURE ? 1 : 2;
    mrhiPassId pass = {0};
    status = mrhiAddPass(s_device, &def, &pass);
    if (status == mrhi_success)
    {
        CHECK(mrhiCompileFrame(s_device) == mrhi_success &&
                  mrhiBeginPass(s_device, pass) == mrhi_success,
              "the pass begun");
        const mrhiBufferCopy side = {.resource = b,
                                     .offset = c->offset,
                                     .bytesPerRow = c->bytesPerRow,
                                     .rowsPerImage = c->rowsPerImage};
        const mrhiTexelLayout layout = {
            .offset = c->offset, .bytesPerRow = c->bytesPerRow, .rowsPerImage = c->rowsPerImage};
        switch (c->method)
        {
        case WRITE_TEXTURE:
            status = mrhiWriteTexture(s_device, pass, &at, dataSize > 0 ? s_data : nullptr,
                                      dataSize, &layout, &c->size);
            break;
        case BUFFER_TO_TEXTURE:
            status = mrhiCopyBufferToTexture(s_device, pass, &side, &at, &c->size);
            break;
        case TEXTURE_TO_BUFFER:
            status = mrhiCopyTextureToBuffer(s_device, pass, &at, &side, &c->size);
            break;
        }
    }
    CHECK(mrhiDropFrame(s_device) == mrhi_success &&
              mrhiDestroyTexture(s_device, texture) == mrhi_success &&
              (c->method == WRITE_TEXTURE || mrhiDestroyBuffer(s_device, buffer) == mrhi_success),
          "dropped");
    return status;
}

// A def of a texture of the block's format, the kind and size, for
// copies both ways.
static mrhiTextureDef CopyTexture(const Block* block, mrhiTextureKind kind, mrhiExtent3d size)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.kind = kind;
    def.format = block->format;
    def.width = size.width;
    def.height = size.height;
    def.depthOrLayers = size.depthOrLayers;
    def.usage = mrhi_textureCopySource | mrhi_textureCopyDestination;
    return def;
}

// A transfer case at the texture's origin.
static mrhiResult Transfer(Method method, const Block* block, mrhiTextureKind kind,
                           mrhiExtent3d textureSize, uint64_t offset, uint32_t bytesPerRow,
                           uint32_t rowsPerImage, uint64_t dataSize, mrhiExtent3d size)
{
    const TransferCase c = {
        .method = method,
        .block = block,
        .texture = CopyTexture(block, kind, textureSize),
        .offset = offset,
        .bytesPerRow = bytesPerRow,
        .rowsPerImage = rowsPerImage,
        .dataSize = dataSize,
        .size = size,
    };
    return RunTransfer(&c);
}

// Counts the cases Maul RHI cannot express, which are counted rather
// than run: required_bytes_in_copy's data one byte short of a buffer's
// need that is a multiple of 4, and bound_on_offset's offset past
// data of 0 bytes, in copies with a buffer; indirect_offset_oob's
// buffers 1 byte short of the arguments.
static uint32_t s_inexpressible;

// Counts the cases Maul RHI decides otherwise than the CTS: from its
// layout value 0 being absent (see Owed),
// - an empty copy of several layers giving 0 for a layout value, which
//   the CTS takes and Maul RHI refuses as a value missing where needed;
// - a rowsPerImage of 0 for a copy of one layer with rows, which the CTS
//   refuses as fewer than the rows and Maul RHI takes as absent;
// setIndexBuffer's ranges of part of an index, which Maul RHI refuses
// (see TestVertexIndexBuffers); and an occlusion query written in a
// second pass of the frame (see TestQueries).
static uint32_t s_differs;

// The outcome Maul RHI owes a transfer case. Its layout value 0 is an
// absent one, which the web driver passes as undefined, where the CTS's
// 0 is a value: a case giving 0 is owed the outcome of the layout
// without it and of the case's other rules (rest), and counted when that
// differs from the CTS's.
static bool Owed(Method method, const Block* block, uint64_t offset, int64_t bytesPerRow,
                 int64_t rowsPerImage, uint64_t dataSize, mrhiExtent3d size, bool rest, bool cts)
{
    if (bytesPerRow != 0 && rowsPerImage != 0)
    {
        return cts;
    }
    Need need = DataBytes(method, block, offset, bytesPerRow == 0 ? ABSENT : bytesPerRow,
                          rowsPerImage == 0 ? ABSENT : rowsPerImage, size);
    bool owed = rest && need.valid && need.bytes <= dataSize;
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
                                            true, need.valid));
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
                                    true, offset <= data));
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
                                            need.bytes, size, true, true));
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
                                                need.bytes - 1, size, true, false));
                        }
                    }
                }
            }
        }
    }
}

// The CTS's no-op pipelines and what its draw cases bind: a render
// target and a 16-byte index buffer.
static mrhiGraphicsPipelineId s_graphics;
static mrhiComputePipelineId s_compute;
static mrhiTextureId s_target;
static mrhiBufferId s_index;
static mrhiLimits s_limits;

static void Answered(void)
{
    mrhiDeviceNotification record;
    CHECK(mrhiNextDeviceNotification(s_device, &record) == mrhi_success &&
              record.outcome == mrhi_success,
          "a pipeline ready");
}

static void MakePipelines(void)
{
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_noopContainer;
    shaderDef.byteCount = sizeof(s_noopContainer);
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(s_device, &shaderDef, &shader) == mrhi_success, "the noop shader");
    mrhiGraphicsPipelineDef graphics = mrhiDefaultGraphicsPipelineDef();
    graphics.shader = shader;
    graphics.vertexEntry = "vs";
    graphics.vertexEntryLength = 2;
    graphics.fragmentEntry = "fs";
    graphics.fragmentEntryLength = 2;
    graphics.colorTargets[0].format = mrhi_formatRgba8Unorm;
    graphics.colorTargetCount = 1;
    mrhiRequestId request;
    CHECK(mrhiCreateGraphicsPipeline(s_device, &graphics, &s_graphics, &request) == mrhi_success,
          "the graphics pipeline");
    Answered();
    mrhiComputePipelineDef compute = mrhiDefaultComputePipelineDef();
    compute.shader = shader;
    compute.entry = "cs";
    compute.entryLength = 2;
    CHECK(mrhiCreateComputePipeline(s_device, &compute, &s_compute, &request) == mrhi_success,
          "the compute pipeline");
    Answered();
    CHECK(mrhiDestroyShader(s_device, shader) == mrhi_success, "the shader destroyed");
    mrhiTextureDef target = mrhiDefaultTextureDef();
    target.format = mrhi_formatRgba8Unorm;
    target.width = 4;
    target.height = 4;
    target.usage = mrhi_textureRenderTarget;
    CHECK(mrhiCreateTexture(s_device, &target, &s_target) == mrhi_success, "the target");
    s_index = MakeBuffer(16, mrhi_bufferIndex);
    CHECK(mrhiGetDeviceLimits(s_device, &s_limits) == mrhi_success, "the limits");
}

// A pass of a draw or dispatch case: a frame of the case's buffer,
// declared with the access, a render pass (with the index buffer too)
// or a compute pass, begun with its no-op pipeline set; the refusal of
// the pass, or success.
typedef struct CasePass
{
    mrhiPassId pass;
    mrhiResourceId buffer;
    mrhiResourceId index;
} CasePass;

static mrhiResult BeginCase(mrhiBufferId buffer, mrhiAccessKind kind, bool render,
                            CasePass* caseOut)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId target = {0};
    CHECK(mrhiBeginFrame(s_device, &frame) == mrhi_success &&
              mrhiImportBuffer(s_device, buffer, &caseOut->buffer) == mrhi_success &&
              mrhiImportBuffer(s_device, s_index, &caseOut->index) == mrhi_success &&
              mrhiImportTexture(s_device, s_target, &target) == mrhi_success,
          "a frame of the case");
    const mrhiAccess accesses[2] = {{.resource = caseOut->buffer, .kind = kind},
                                    {.resource = caseOut->index, .kind = mrhi_accessIndex}};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.accesses = accesses;
    def.accessCount = render ? 2 : 1;
    if (render)
    {
        def.colorTargets[0] = (mrhiColorTarget){.resource = target, .load = mrhi_loadClear};
        def.colorTargetCount = 1;
    }
    mrhiResult status = mrhiAddPass(s_device, &def, &caseOut->pass);
    if (status == mrhi_success)
    {
        CHECK(mrhiCompileFrame(s_device) == mrhi_success &&
                  mrhiBeginPass(s_device, caseOut->pass) == mrhi_success &&
                  (render ? mrhiSetGraphicsPipeline(s_device, caseOut->pass, s_graphics)
                          : mrhiSetComputePipeline(s_device, caseOut->pass, s_compute)) ==
                      mrhi_success,
              "the pass begun");
    }
    return status;
}

static void EndCase(mrhiBufferId buffer)
{
    CHECK(mrhiDropFrame(s_device) == mrhi_success &&
              mrhiDestroyBuffer(s_device, buffer) == mrhi_success,
          "dropped");
}

// The size a case's buffer is made with: the CTS's size rounded up to a
// positive multiple of 4, or 0 when the need of the call (from its
// offset) falls between the two, which changes the outcome.
static uint64_t Rounded(uint64_t size, uint64_t need)
{
    uint64_t rounded = size == 0 ? 4 : (size + 3) / 4 * 4;
    return size < need && need <= rounded ? 0 : rounded;
}

// An indirect draw in a render pass of a buffer of the size and usage,
// indexed or not, with the index buffer set when indexed.
static mrhiResult DrawIndirect(uint64_t size, mrhiBufferUsage usage, bool indexed, uint64_t offset)
{
    uint64_t rounded = Rounded(size, offset + (indexed ? 20 : 16));
    if (rounded == 0)
    {
        return INEXPRESSIBLE;
    }
    mrhiBufferId buffer = MakeBuffer(rounded, usage);
    CasePass c = {0};
    mrhiResult status = BeginCase(buffer, mrhi_accessIndirect, true, &c);
    if (status == mrhi_success && indexed)
    {
        CHECK(mrhiSetIndexBuffer(s_device, c.pass, c.index, mrhi_indexUint32, 0, MRHI_WHOLE_SIZE) ==
                  mrhi_success,
              "the index buffer");
        status = mrhiDrawIndexedIndirect(s_device, c.pass, c.buffer, offset);
    }
    else if (status == mrhi_success)
    {
        status = mrhiDrawIndirect(s_device, c.pass, c.buffer, offset);
    }
    EndCase(buffer);
    return status;
}

// api,validation,encoding,cmds,render,indirect_draw; its render bundle
// encoder has no counterpart.
static void TestIndirectDraws(void)
{
    char name[128];
    for (int indexed = 0; indexed <= 1; ++indexed)
    {
        uint64_t params = indexed ? 20 : 16;
        for (uint64_t offset = 0; offset <= 4; offset += 2)
        {
            snprintf(name, sizeof(name), "indirect_draw:indirect_offset_alignment indexed=%d %llu",
                     indexed, (unsigned long long)offset);
            ExpectTransfer(name, DrawIndirect(256, mrhi_bufferIndirect, indexed, offset),
                           offset % 4 == 0);
        }
        const struct
        {
            uint64_t offset;
            uint64_t size;
            bool valid;
        } bounds[] = {
            {0, 0, false},          {0, params, true},       {0, params + 1, true},
            {0, params - 1, false}, {0, params - 4, false},  {4, params + 4, true},
            {4, params + 3, false}, {2, params + 4, false},  {3, params + 4, false},
            {5, params + 4, false}, {params, params, false}, {params + 4, params, false},
        };
        for (size_t i = 0; i < sizeof(bounds) / sizeof(bounds[0]); ++i)
        {
            snprintf(name, sizeof(name), "indirect_draw:indirect_offset_oob indexed=%d %llu,%llu",
                     indexed, (unsigned long long)bounds[i].offset,
                     (unsigned long long)bounds[i].size);
            ExpectTransfer(
                name, DrawIndirect(bounds[i].size, mrhi_bufferIndirect, indexed, bounds[i].offset),
                bounds[i].valid);
        }
        static const mrhiBufferUsage usages[] = {mrhi_bufferIndirect, mrhi_bufferCopyDestination,
                                                 mrhi_bufferCopyDestination | mrhi_bufferIndirect};
        for (size_t i = 0; i < 3; ++i)
        {
            snprintf(name, sizeof(name), "indirect_draw:indirect_buffer_usage indexed=%d %#x",
                     indexed, (unsigned)usages[i]);
            Expect(name, DrawIndirect(256, usages[i], indexed, 0),
                   (usages[i] & mrhi_bufferIndirect) != 0);
        }
    }
}

// A dispatch in a compute pass, direct or from a 12-byte indirect
// buffer of the usage.
static mrhiResult Dispatch(bool indirect, mrhiBufferUsage usage, uint64_t size, uint32_t x,
                           uint32_t y, uint32_t z)
{
    mrhiBufferId buffer = MakeBuffer(size, usage);
    CasePass c = {0};
    mrhiResult status = BeginCase(buffer, mrhi_accessIndirect, false, &c);
    if (status == mrhi_success)
    {
        status = indirect ? mrhiDispatchIndirect(s_device, c.pass, c.buffer, 0)
                          : mrhiDispatch(s_device, c.pass, x, y, z);
    }
    EndCase(buffer);
    return status;
}

// api,validation,encoding,cmds,compute_pass.
static void TestDispatches(void)
{
    char name[128];
    uint32_t most = s_limits.workgroupsPerDimension;
    const uint32_t large[] = {0, 1, most, most + 1, 0x7FFFFFFFu, 0xFFFFFFFFu};
    for (int indirect = 0; indirect <= 1; ++indirect)
    {
        for (size_t l = 0; l < 6; ++l)
        {
            for (int dimension = 0; dimension < 3; ++dimension)
            {
                for (uint32_t small = 0; small <= 1; ++small)
                {
                    uint32_t sizes[3] = {small, small, small};
                    sizes[dimension] = large[l];
                    snprintf(name, sizeof(name), "compute_pass:dispatch_sizes %s %u,%u,%u",
                             indirect ? "indirect" : "direct", sizes[0], sizes[1], sizes[2]);
                    // An indirect dispatch's counts are read on the GPU,
                    // where too many dispatch nothing.
                    Expect(name,
                           Dispatch(indirect != 0, mrhi_bufferIndirect, 12, sizes[0], sizes[1],
                                    sizes[2]),
                           indirect || large[l] <= most);
                }
            }
        }
    }
    size_t count = sizeof(s_usages) / sizeof(s_usages[0]);
    for (size_t a = 0; a < count; ++a)
    {
        for (size_t b = 0; b < count; ++b)
        {
            mrhiBufferUsage usage = s_usages[a] | s_usages[b];
            snprintf(name, sizeof(name), "compute_pass:indirect_dispatch_buffer,usage %#x",
                     (unsigned)usage);
            Expect(name, Dispatch(true, usage, 16, 0, 0, 0), (usage & mrhi_bufferIndirect) != 0);
        }
    }
}

// A vertex buffer set, or an index buffer of the format, from a buffer
// of the size in a render pass.
static mrhiResult SetBuffer(bool vertex, uint64_t bytes, uint32_t slot, mrhiIndexFormat format,
                            uint64_t offset, uint64_t size)
{
    mrhiBufferId buffer = MakeBuffer(bytes, vertex ? mrhi_bufferVertex : mrhi_bufferIndex);
    CasePass c = {0};
    CHECK(BeginCase(buffer, vertex ? mrhi_accessVertex : mrhi_accessIndex, true, &c) ==
              mrhi_success,
          "a render pass");
    mrhiResult status = vertex
                            ? mrhiSetVertexBuffer(s_device, c.pass, slot, c.buffer, offset, size)
                            : mrhiSetIndexBuffer(s_device, c.pass, c.buffer, format, offset, size);
    EndCase(buffer);
    return status;
}

// The CTS's buildBufferOffsetAndSizeOOBTestParams(4, 256), an absent
// size being MRHI_WHOLE_SIZE.
typedef struct Range
{
    uint64_t offset;
    uint64_t size;
    bool valid;
} Range;

static const Range s_ranges[] = {
    {0, 0, true},
    {0, 1, true},
    {0, 4, true},
    {0, 5, true},
    {0, 256, true},
    {0, 260, false},
    {4, 256, false},
    {4, 252, true},
    {252, 4, true},
    {256, 1, false},
    {0, MRHI_WHOLE_SIZE, true},
    {4, MRHI_WHOLE_SIZE, true},
    {252, MRHI_WHOLE_SIZE, true},
    {256, MRHI_WHOLE_SIZE, true},
    {260, MRHI_WHOLE_SIZE, false},
};

// api,validation,encoding,cmds,render,setVertexBuffer and
// setIndexBuffer; their render bundle encoder has no counterpart.
static void TestVertexIndexBuffers(void)
{
    char name[128];
    uint32_t most = s_limits.vertexBuffers;
    const uint32_t slots[] = {0, most - 1, most};
    for (size_t i = 0; i < 3; ++i)
    {
        snprintf(name, sizeof(name), "setVertexBuffer:slot %u", slots[i]);
        Expect(name, SetBuffer(true, 16, slots[i], mrhi_indexUint32, 0, MRHI_WHOLE_SIZE),
               slots[i] < most);
    }
    for (uint64_t offset = 0; offset <= 4; offset += 2)
    {
        snprintf(name, sizeof(name), "setVertexBuffer:offset_alignment %llu",
                 (unsigned long long)offset);
        Expect(name, SetBuffer(true, 16, 0, mrhi_indexUint32, offset, MRHI_WHOLE_SIZE),
               offset % 4 == 0);
    }
    for (size_t i = 0; i < sizeof(s_ranges) / sizeof(s_ranges[0]); ++i)
    {
        snprintf(name, sizeof(name), "setVertexBuffer:offset_and_size_oob %llu,%lld",
                 (unsigned long long)s_ranges[i].offset, (long long)s_ranges[i].size);
        Expect(name,
               SetBuffer(true, 256, 0, mrhi_indexUint32, s_ranges[i].offset, s_ranges[i].size),
               s_ranges[i].valid);
    }
    static const struct
    {
        mrhiIndexFormat format;
        uint64_t offset;
    } aligned[] = {
        {mrhi_indexUint16, 0}, {mrhi_indexUint16, 1}, {mrhi_indexUint16, 2},
        {mrhi_indexUint32, 0}, {mrhi_indexUint32, 2}, {mrhi_indexUint32, 4},
    };
    for (size_t i = 0; i < 6; ++i)
    {
        uint64_t width = aligned[i].format == mrhi_indexUint16 ? 2 : 4;
        snprintf(name, sizeof(name), "setIndexBuffer:offset_alignment %u,%llu",
                 (unsigned)aligned[i].format, (unsigned long long)aligned[i].offset);
        Expect(name, SetBuffer(false, 16, 0, aligned[i].format, aligned[i].offset, MRHI_WHOLE_SIZE),
               aligned[i].offset % width == 0);
    }
    for (size_t i = 0; i < sizeof(s_ranges) / sizeof(s_ranges[0]); ++i)
    {
        snprintf(name, sizeof(name), "setIndexBuffer:offset_and_size_oob %llu,%lld",
                 (unsigned long long)s_ranges[i].offset, (long long)s_ranges[i].size);
        // Maul RHI takes an index range in whole indices, where WebGPU
        // takes any bytes: a part of an index is never drawn, so a range
        // ending in one is a mistake.
        bool whole = s_ranges[i].size == MRHI_WHOLE_SIZE || s_ranges[i].size % 4 == 0;
        s_differs += s_ranges[i].valid && !whole ? 1 : 0;
        Expect(name,
               SetBuffer(false, 256, 0, mrhi_indexUint32, s_ranges[i].offset, s_ranges[i].size),
               s_ranges[i].valid && whole);
    }
}

// The CTS's valuesToTestDivisibilityBy: 0 to twice the number, and
// three times it; returns how many, at most 32 for a number up to 15.
static size_t Divisibility(uint32_t number, uint32_t* valuesOut)
{
    size_t count = 0;
    for (uint32_t i = 0; i <= 2 * number; ++i)
    {
        valuesOut[count++] = i;
    }
    valuesOut[count++] = 3 * number;
    return count;
}

// The CTS's createAlignedTexture for a copy at an origin.
static mrhiExtent3d AlignedAt(const Block* block, mrhiExtent3d size, uint32_t x, uint32_t y,
                              uint32_t z)
{
    return AlignedSize(block,
                       (mrhiExtent3d){size.width + x, size.height + y, size.depthOrLayers + z});
}

// api,validation,image_copy,texture_related; its 1D dimension has no
// counterpart, and compressed formats are 2D only.
static void TestTextureRelated(void)
{
    char name[160];
    const Block* rgba8 = &s_blocks[2];
    static const struct
    {
        bool volume;
        uint32_t layers;
    } shapes[] = {{false, 1}, {false, 3}, {true, 3}};
    // The CTS's kTextureUsages less TRANSIENT_ATTACHMENT, which it skips.
    static const mrhiTextureUsage usages[] = {mrhi_textureCopySource, mrhi_textureCopyDestination,
                                              mrhi_textureSampled, mrhi_textureStorage,
                                              mrhi_textureRenderTarget};
    for (int m = 0; m < 3; ++m)
    {
        Method method = (Method)m;
        const mrhiExtent3d empty = {0, 0, 0};
        // usage: an empty copy, which still needs the copy usage.
        for (size_t s = 0; s < 3; ++s)
        {
            for (size_t a = 0; a < 5; ++a)
            {
                for (size_t b = 0; b < 5; ++b)
                {
                    mrhiTextureUsage usage = usages[a] | usages[b];
                    if (shapes[s].volume && (usage & mrhi_textureRenderTarget) != 0)
                    {
                        continue;
                    }
                    TransferCase c = {
                        .method = method,
                        .block = rgba8,
                        .texture = CopyTexture(rgba8, KindOf(shapes[s].volume, shapes[s].layers),
                                               (mrhiExtent3d){4, 4, shapes[s].layers}),
                        .dataSize = 1,
                        .size = empty,
                    };
                    c.texture.usage = usage;
                    mrhiTextureUsage needed = method == TEXTURE_TO_BUFFER
                                                  ? mrhi_textureCopySource
                                                  : mrhi_textureCopyDestination;
                    snprintf(name, sizeof(name), "texture_related:usage %s %s layers=%u %#x",
                             s_methods[m], shapes[s].volume ? "3d" : "2d", shapes[s].layers,
                             (unsigned)usage);
                    ExpectTransfer(name, RunTransfer(&c), (usage & needed) != 0);
                }
            }
        }
        // sample_count.
        for (uint32_t samples = 1; samples <= 4; samples += 3)
        {
            TransferCase c = {
                .method = method,
                .block = rgba8,
                .texture = CopyTexture(rgba8, mrhi_texture2d, (mrhiExtent3d){4, 4, 1}),
                .dataSize = 1,
                .size = empty,
            };
            c.texture.sampleCount = samples;
            c.texture.usage |= mrhi_textureSampled | mrhi_textureRenderTarget;
            snprintf(name, sizeof(name), "texture_related:sample_count %s %u", s_methods[m],
                     samples);
            ExpectTransfer(name, RunTransfer(&c), samples == 1);
        }
        // mip_level.
        static const uint32_t mipCounts[] = {1, 3, 5};
        static const uint32_t mips[] = {0, 1, 3, 4};
        for (size_t s = 0; s < 3; ++s)
        {
            for (size_t n = 0; n < 3; ++n)
            {
                for (size_t l = 0; l < 4; ++l)
                {
                    TransferCase c = {
                        .method = method,
                        .block = rgba8,
                        .texture = CopyTexture(rgba8, KindOf(shapes[s].volume, shapes[s].layers),
                                               (mrhiExtent3d){32, 32, shapes[s].layers}),
                        .at = {.mip = mips[l]},
                        .dataSize = 1,
                        .size = empty,
                    };
                    c.texture.mipLevels = mipCounts[n];
                    snprintf(name, sizeof(name),
                             "texture_related:mip_level %s %s layers=%u mips=%u mip=%u",
                             s_methods[m], shapes[s].volume ? "3d" : "2d", shapes[s].layers,
                             mipCounts[n], mips[l]);
                    ExpectTransfer(name, RunTransfer(&c), mips[l] < mipCounts[n]);
                }
            }
        }
        for (size_t f = 0; f < sizeof(s_blocks) / sizeof(s_blocks[0]); ++f)
        {
            const Block* block = &s_blocks[f];
            uint32_t values[32];
            // origin_alignment: an empty copy of 1 or 3 layers at an origin
            // whose x or y is tested against the block, or whose z is any.
            for (size_t s = 0; s < 3; ++s)
            {
                if (shapes[s].volume && block->compressed)
                {
                    continue;
                }
                for (int axis = 0; axis < 3; ++axis)
                {
                    uint32_t step = axis == 0 ? block->width : axis == 1 ? block->height : 1;
                    size_t count = Divisibility(step, values);
                    for (size_t v = 0; v < count; ++v)
                    {
                        uint32_t origin[3] = {0, 0, 0};
                        origin[axis] = values[v];
                        const mrhiExtent3d size = {0, 0, shapes[s].layers};
                        mrhiExtent3d textureSize =
                            AlignedAt(block, size, origin[0], origin[1], origin[2]);
                        TransferCase c = {
                            .method = method,
                            .block = block,
                            .texture = CopyTexture(
                                block, KindOf(shapes[s].volume, textureSize.depthOrLayers),
                                textureSize),
                            .at = {.x = origin[0], .y = origin[1], .z = origin[2]},
                            .dataSize = 1,
                            .size = size,
                        };
                        bool rest = origin[0] % block->width == 0 && origin[1] % block->height == 0;
                        snprintf(
                            name, sizeof(name),
                            "texture_related:origin_alignment %s f=%zu %s layers=%u axis=%d %u",
                            s_methods[m], f, shapes[s].volume ? "3d" : "2d", shapes[s].layers, axis,
                            values[v]);
                        ExpectTransfer(name, RunTransfer(&c),
                                       Owed(method, block, 0, 0, 0, 1, size, rest, rest));
                    }
                }
            }
            // size_alignment: an empty copy but for the axis tested.
            for (int volume = 0; volume <= (block->compressed ? 0 : 1); ++volume)
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    uint32_t step = axis == 0 ? block->width : axis == 1 ? block->height : 1;
                    size_t count = Divisibility(step, values);
                    for (size_t v = 0; v < count; ++v)
                    {
                        uint32_t extent[3] = {0, 0, 0};
                        extent[axis] = values[v];
                        const mrhiExtent3d size = {extent[0], extent[1], extent[2]};
                        mrhiExtent3d textureSize = AlignedSize(block, size);
                        uint32_t blocksWide = (size.width + block->width - 1) / block->width;
                        uint32_t bytesPerRow =
                            ((blocksWide > 0 ? blocksWide : 1) * block->bytes + 255) / 256 * 256;
                        uint32_t rowsPerImage = (size.height + block->height - 1) / block->height;
                        TransferCase c = {
                            .method = method,
                            .block = block,
                            .texture = CopyTexture(
                                block, KindOf(volume != 0, textureSize.depthOrLayers), textureSize),
                            .bytesPerRow = bytesPerRow,
                            .rowsPerImage = rowsPerImage,
                            .dataSize = 1,
                            .size = size,
                        };
                        bool rest =
                            size.width % block->width == 0 && size.height % block->height == 0;
                        snprintf(name, sizeof(name),
                                 "texture_related:size_alignment %s f=%zu %s axis=%d %u",
                                 s_methods[m], f, volume ? "3d" : "2d", axis, values[v]);
                        ExpectTransfer(
                            name, RunTransfer(&c),
                            Owed(method, block, 0, bytesPerRow, rowsPerImage, 1, size, rest, rest));
                    }
                }
            }
        }
        // copy_rectangle: an empty copy but for the axis tested, whose
        // origin and size end inside the texture's mip or past it.
        for (int volume = 0; volume <= 1; ++volume)
        {
            for (uint32_t origin = 7; origin <= 8; ++origin)
            {
                for (uint32_t extent = 7; extent <= 8; ++extent)
                {
                    for (uint32_t total = 14; total <= 15; ++total)
                    {
                        for (uint32_t mip = 0; mip <= 2; mip += 2)
                        {
                            for (int axis = 0; axis < 3; ++axis)
                            {
                                uint32_t sizes[3] = {16u << mip, 16u << mip, 16};
                                sizes[axis] = axis < 2 || volume ? total << mip : total;
                                uint32_t at[3] = {0, 0, 0};
                                uint32_t copy[3] = {0, 0, 0};
                                at[axis] = origin;
                                copy[axis] = extent;
                                const mrhiExtent3d size = {copy[0], copy[1], copy[2]};
                                uint32_t bytesPerRow = (copy[0] + 255) / 256 * 256;
                                TransferCase c = {
                                    .method = method,
                                    .block = rgba8,
                                    .texture = CopyTexture(
                                        rgba8, volume ? mrhi_texture3d : mrhi_texture2dArray,
                                        (mrhiExtent3d){sizes[0], sizes[1], sizes[2]}),
                                    .at = {.x = at[0], .y = at[1], .z = at[2], .mip = mip},
                                    .bytesPerRow = bytesPerRow,
                                    .rowsPerImage = copy[1],
                                    .dataSize = 1,
                                    .size = size,
                                };
                                c.texture.mipLevels = 3;
                                bool rest = origin + extent <= total;
                                snprintf(name, sizeof(name),
                                         "texture_related:copy_rectangle %s %s %u,%u,%u mip=%u "
                                         "axis=%d",
                                         s_methods[m], volume ? "3d" : "2d", origin, extent, total,
                                         mip, axis);
                                ExpectTransfer(name, RunTransfer(&c),
                                               Owed(method, rgba8, 0, bytesPerRow, copy[1], 1, size,
                                                    rest, rest));
                            }
                        }
                    }
                }
            }
        }
    }
}

// An attachment of a render pass case: its texture (square, of the
// CTS's createTestTexture defaults when zero) and the mip and layer the
// pass uses. A format of mrhi_formatNone is no attachment.
typedef struct Attachment
{
    mrhiFormat format;
    uint32_t size;
    uint32_t layers;
    uint32_t mips;
    uint32_t samples;
    mrhiTextureUsage usage;
    uint32_t mip;
    uint32_t layer;
} Attachment;

static mrhiTextureId MakeAttachment(const Attachment* a)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = a->format;
    def.width = a->size != 0 ? a->size : 16;
    def.height = def.width;
    def.depthOrLayers = a->layers != 0 ? a->layers : 1;
    def.kind = def.depthOrLayers > 1 ? mrhi_texture2dArray : mrhi_texture2d;
    def.mipLevels = a->mips != 0 ? a->mips : 1;
    def.sampleCount = a->samples != 0 ? a->samples : 1;
    def.usage = a->usage != 0 ? a->usage : mrhi_textureRenderTarget;
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(s_device, &def, &texture) == mrhi_success, "an attachment");
    return texture;
}

// A render pass case: its color attachments, each with a resolve
// target or none, and a depth attachment or none, declared; the pass's
// refusal, or success. The CTS's loads clear and its stores store.
static mrhiResult RenderPass(const Attachment* colors, const Attachment* resolves, uint32_t count,
                             Attachment depth)
{
    mrhiTextureId textures[2 * MRHI_COLOR_TARGETS + 2] = {0};
    uint32_t made = 0;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &frame) == mrhi_success, "a frame");
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    for (uint32_t i = 0; i < count && i < MRHI_COLOR_TARGETS; ++i)
    {
        mrhiColorTarget* target = &def.colorTargets[i];
        textures[made] = MakeAttachment(&colors[i]);
        CHECK(mrhiImportTexture(s_device, textures[made++], &target->resource) == mrhi_success,
              "imported");
        target->mip = colors[i].mip;
        target->layer = colors[i].layer;
        target->load = mrhi_loadClear;
        target->store = mrhi_storeKeep;
        if (resolves != nullptr && resolves[i].format != mrhi_formatNone)
        {
            textures[made] = MakeAttachment(&resolves[i]);
            CHECK(mrhiImportTexture(s_device, textures[made++], &target->resolve) == mrhi_success,
                  "imported");
            target->resolveMip = resolves[i].mip;
            target->resolveLayer = resolves[i].layer;
        }
    }
    def.colorTargetCount = count;
    if (depth.format != mrhi_formatNone)
    {
        textures[made] = MakeAttachment(&depth);
        CHECK(mrhiImportTexture(s_device, textures[made++], &def.depthTarget.resource) ==
                  mrhi_success,
              "imported");
        def.depthTarget.depthLoad = mrhi_loadClear;
        def.depthTarget.depthStore = mrhi_storeKeep;
        def.depthTarget.clearDepth = 1.0F;
        def.depthTarget.stencilLoad = mrhi_loadClear;
        def.depthTarget.stencilStore = mrhi_storeKeep;
    }
    mrhiPassId pass = {0};
    mrhiResult status = mrhiAddPass(s_device, &def, &pass);
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
    for (uint32_t i = 0; i < made; ++i)
    {
        CHECK(mrhiDestroyTexture(s_device, textures[i]) == mrhi_success, "destroyed");
    }
    return status;
}

#define RGBA8 mrhi_formatRgba8Unorm
#define DEPTH mrhi_formatDepthStencil
static const Attachment s_none = {0};

// api,validation,render_pass,render_pass_descriptor: the cases whose
// attachments Maul RHI's targets express. Its targets name one mip and
// one layer, so the cases of views of several (attachments,layer_count
// and mip_level_count, resolveTarget,array_layer_count and
// mipmap_level_count) have no counterpart; nor have those of depth
// slices, of texture views or bundles, or of a pass without targets,
// which is a compute pass in Maul RHI.
static void TestRenderPasses(void)
{
    char name[128];
    const Attachment r8 = {.format = mrhi_formatR8Unorm};
    const Attachment one = {.format = RGBA8, .size = 1};
    const Attachment two = {.format = RGBA8, .size = 2};
    const Attachment color = {.format = RGBA8};
    const Attachment multi = {.format = RGBA8, .samples = 4};
    const Attachment depth = {.format = DEPTH};
    const Attachment multiDepth = {.format = DEPTH, .samples = 4};
    // attachments,one_color_attachment and one_depth_stencil_attachment.
    Expect("render_pass_descriptor:attachments,one_color_attachment",
           RenderPass(&color, nullptr, 1, s_none), true);
    Expect("render_pass_descriptor:attachments,one_depth_stencil_attachment",
           RenderPass(nullptr, nullptr, 0, depth), true);
    // color_attachments,limits,maxColorAttachments.
    const Attachment many[MRHI_COLOR_TARGETS + 1] = {r8, r8, r8, r8, r8, r8, r8, r8, r8};
    Expect("render_pass_descriptor:color_attachments,limits,maxColorAttachments 8",
           RenderPass(many, nullptr, MRHI_COLOR_TARGETS, s_none), true);
    Expect("render_pass_descriptor:color_attachments,limits,maxColorAttachments 9",
           RenderPass(many, nullptr, MRHI_COLOR_TARGETS + 1, s_none), false);
    // attachments,same_size.
    const Attachment ones[2] = {one, one};
    const Attachment oneTwo[2] = {one, two};
    Expect("render_pass_descriptor:attachments,same_size all 1x1",
           RenderPass(ones, nullptr, 2, (Attachment){.format = DEPTH, .size = 1}), true);
    Expect("render_pass_descriptor:attachments,same_size a color 2x2",
           RenderPass(oneTwo, nullptr, 2, s_none), false);
    Expect("render_pass_descriptor:attachments,same_size the depth 2x2",
           RenderPass(ones, nullptr, 2, (Attachment){.format = DEPTH, .size = 2}), false);
    // attachments,color_depth_mismatch.
    Expect("render_pass_descriptor:attachments,color_depth_mismatch depth as color",
           RenderPass(&depth, nullptr, 1, s_none), false);
    Expect("render_pass_descriptor:attachments,color_depth_mismatch color as depth",
           RenderPass(nullptr, nullptr, 0, color), false);
    // color_attachments,sample_count.
    const Attachment mixed[2] = {color, multi};
    Expect("render_pass_descriptor:color_attachments,sample_count multisampled alone",
           RenderPass(&multi, nullptr, 1, s_none), true);
    Expect("render_pass_descriptor:color_attachments,sample_count 1 and 4",
           RenderPass(mixed, nullptr, 2, s_none), false);
    // resolveTarget,sample_count, error_state, single_sample_count,
    // different_format, different_size.
    Expect("render_pass_descriptor:resolveTarget,sample_count",
           RenderPass(&multi, &multi, 1, s_none), false);
    Expect("render_pass_descriptor:resolveTarget,error_state",
           RenderPass(&multi, &(Attachment){.format = RGBA8, .layer = 2}, 1, s_none), false);
    Expect("render_pass_descriptor:resolveTarget,single_sample_count",
           RenderPass(&multi, &color, 1, s_none), true);
    Expect("render_pass_descriptor:resolveTarget,different_format",
           RenderPass(&multi, &(Attachment){.format = mrhi_formatBgra8Unorm}, 1, s_none), false);
    const Attachment larger = {.format = RGBA8, .size = 32, .mips = 2};
    const Attachment largerMip1 = {.format = RGBA8, .size = 32, .mips = 2, .mip = 1};
    Expect("render_pass_descriptor:resolveTarget,different_size mip 0",
           RenderPass(&multi, &larger, 1, s_none), false);
    Expect("render_pass_descriptor:resolveTarget,different_size mip 1",
           RenderPass(&multi, &largerMip1, 1, s_none), true);
    // resolveTarget,usage.
    static const mrhiTextureUsage usages[] = {
        mrhi_textureCopySource | mrhi_textureCopyDestination,
        mrhi_textureStorage | mrhi_textureSampled,
        mrhi_textureStorage,
        mrhi_textureRenderTarget | mrhi_textureSampled,
    };
    for (size_t i = 0; i < 4; ++i)
    {
        snprintf(name, sizeof(name), "render_pass_descriptor:resolveTarget,usage %#x",
                 (unsigned)usages[i]);
        Expect(name,
               RenderPass(&multi, &(Attachment){.format = RGBA8, .usage = usages[i]}, 1, s_none),
               (usages[i] & mrhi_textureRenderTarget) != 0);
    }
    // depth_stencil_attachment,sample_counts_mismatch.
    Expect("render_pass_descriptor:depth_stencil_attachment,sample_counts_mismatch 4 and 1",
           RenderPass(&multi, nullptr, 1, depth), false);
    Expect("render_pass_descriptor:depth_stencil_attachment,sample_counts_mismatch 1 and 4",
           RenderPass(&color, nullptr, 1, multiDepth), false);
}

static mrhiQuerySetId MakeQuerySet(mrhiQueryType type, uint32_t count)
{
    mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
    def.type = type;
    def.count = count;
    mrhiQuerySetId set = {0};
    CHECK(mrhiCreateQuerySet(s_device, &def, &set) == mrhi_success, "a query set");
    return set;
}

// A resolveQuerySet case: an occlusion set of the count, resolved into
// a buffer of the size and usage from a pass without targets.
static mrhiResult Resolve(uint32_t setCount, uint64_t size, mrhiBufferUsage usage, uint32_t first,
                          uint32_t count, uint64_t offset)
{
    mrhiQuerySetId set = MakeQuerySet(mrhi_queryOcclusion, setCount);
    mrhiBufferId buffer = MakeBuffer(size, usage);
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId destination = {0};
    CHECK(mrhiBeginFrame(s_device, &frame) == mrhi_success &&
              mrhiImportBuffer(s_device, buffer, &destination) == mrhi_success,
          "a frame of the buffer");
    const mrhiAccess access = {.resource = destination, .kind = mrhi_accessQueryResolve};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.accesses = &access;
    def.accessCount = 1;
    mrhiPassId pass = {0};
    mrhiResult status = mrhiAddPass(s_device, &def, &pass);
    if (status == mrhi_success)
    {
        CHECK(mrhiCompileFrame(s_device) == mrhi_success &&
                  mrhiBeginPass(s_device, pass) == mrhi_success,
              "the pass begun");
        status = mrhiResolveQueries(s_device, pass, set, first, count, destination, offset);
    }
    CHECK(mrhiDropFrame(s_device) == mrhi_success &&
              mrhiDestroyBuffer(s_device, buffer) == mrhi_success &&
              mrhiDestroyQuerySet(s_device, set) == mrhi_success,
          "dropped");
    return status;
}

// An occlusion query case: render passes of a 16 by 16 target and an
// occlusion set of the count, each making its calls (a query begun, or
// END) and ended; the first refusal, or success.
#define END UINT32_MAX

static mrhiResult Occlusion(uint32_t setCount, const uint32_t* calls, size_t callCount,
                            uint32_t passes)
{
    mrhiQuerySetId set = MakeQuerySet(mrhi_queryOcclusion, setCount);
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId target = {0};
    CHECK(mrhiBeginFrame(s_device, &frame) == mrhi_success &&
              mrhiImportTexture(s_device, s_target, &target) == mrhi_success,
          "a frame of the target");
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.colorTargets[0] = (mrhiColorTarget){.resource = target, .load = mrhi_loadClear};
    def.colorTargetCount = 1;
    def.occlusionQuerySet = set;
    mrhiPassId ids[2] = {0};
    for (uint32_t i = 0; i < passes; ++i)
    {
        CHECK(mrhiAddPass(s_device, &def, &ids[i]) == mrhi_success, "a pass");
    }
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    mrhiResult status = mrhi_success;
    for (uint32_t i = 0; i < passes && status == mrhi_success; ++i)
    {
        CHECK(mrhiBeginPass(s_device, ids[i]) == mrhi_success, "the pass begun");
        for (size_t c = 0; c < callCount && status == mrhi_success; ++c)
        {
            status = calls[c] == END ? mrhiEndOcclusionQuery(s_device, ids[i])
                                     : mrhiBeginOcclusionQuery(s_device, ids[i], calls[c]);
        }
        status = status == mrhi_success ? mrhiEndPass(s_device, ids[i]) : status;
    }
    CHECK(mrhiDropFrame(s_device) == mrhi_success &&
              mrhiDestroyQuerySet(s_device, set) == mrhi_success,
          "dropped");
    return status;
}

// api,validation,encoding,queries.
static void TestQueries(void)
{
    char name[128];
    // resolveQuerySet, kQueryCount 2.
    static const uint32_t ranges[][2] = {{0, 2}, {0, 3}, {1, 2}, {2, 1}};
    for (size_t i = 0; i < 4; ++i)
    {
        snprintf(name, sizeof(name), "resolveQuerySet:first_query_and_query_count %u,%u",
                 ranges[i][0], ranges[i][1]);
        Expect(name, Resolve(2, 16, mrhi_bufferQueryResolve, ranges[i][0], ranges[i][1], 0),
               ranges[i][0] + ranges[i][1] <= 2);
    }
    Expect("resolveQuerySet:destination_buffer_usage STORAGE",
           Resolve(2, 16, mrhi_bufferStorage, 0, 2, 0), false);
    Expect("resolveQuerySet:destination_buffer_usage QUERY_RESOLVE",
           Resolve(2, 16, mrhi_bufferQueryResolve, 0, 2, 0), true);
    for (uint64_t offset = 0; offset <= 384; offset += 128)
    {
        snprintf(name, sizeof(name), "resolveQuerySet:destination_offset_alignment %llu",
                 (unsigned long long)offset);
        Expect(name, Resolve(2, 512, mrhi_bufferQueryResolve, 0, 2, offset), offset % 256 == 0);
    }
    static const struct
    {
        uint32_t count;
        uint64_t size;
        uint64_t offset;
        bool succeeds;
    } bounds[] = {
        {2, 16, 0, true},    {3, 16, 0, false},    {2, 16, 256, false},
        {2, 272, 256, true}, {2, 264, 256, false},
    };
    for (size_t i = 0; i < 5; ++i)
    {
        snprintf(name, sizeof(name), "resolveQuerySet:resolve_buffer_oob %u,%llu,%llu",
                 bounds[i].count, (unsigned long long)bounds[i].size,
                 (unsigned long long)bounds[i].offset);
        Expect(name,
               Resolve(bounds[i].count, bounds[i].size, mrhi_bufferQueryResolve, 0, bounds[i].count,
                       bounds[i].offset),
               bounds[i].succeeds);
    }
    // begin_end: occlusion_query,begin_end_balance.
    static const uint32_t balance[][2] = {{0, 1}, {1, 0}, {1, 1}, {1, 2}, {2, 1}};
    for (size_t i = 0; i < 5; ++i)
    {
        uint32_t calls[4];
        size_t count = 0;
        for (uint32_t b = 0; b < balance[i][0]; ++b)
        {
            calls[count++] = b;
        }
        for (uint32_t e = 0; e < balance[i][1]; ++e)
        {
            calls[count++] = END;
        }
        snprintf(name, sizeof(name), "begin_end:occlusion_query,begin_end_balance %u,%u",
                 balance[i][0], balance[i][1]);
        Expect(name, Occlusion(2, calls, count, 1), balance[i][0] == balance[i][1]);
    }
    // occlusion_query,begin_end_invalid_nesting.
    static const uint32_t nesting[][4] = {{0, END, 1, END}, {0, 0, END, END}, {0, 1, END, END}};
    for (size_t i = 0; i < 3; ++i)
    {
        snprintf(name, sizeof(name), "begin_end:occlusion_query,begin_end_invalid_nesting %zu", i);
        Expect(name, Occlusion(2, nesting[i], 4, 1), i == 0);
    }
    // occlusion_query,disjoint_queries_with_same_query_index. Maul RHI
    // writes a query at most once in a frame, where the CTS allows once
    // a pass: a set is reset once, before the frame's first use, as
    // Vulkan needs (mrhi-0012).
    static const uint32_t twice[] = {0, END, 0, END};
    Expect("begin_end:occlusion_query,disjoint_queries_with_same_query_index same pass",
           Occlusion(1, twice, 4, 1), false);
    s_differs += 1;
    Expect("begin_end:occlusion_query,disjoint_queries_with_same_query_index other pass",
           Occlusion(1, twice, 2, 2), false);
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
    CHECK(s_differs == 192 && s_inexpressible == 962, "the layout cases set aside");
    TestTextureRelated();
    CHECK(s_differs == 876 && s_inexpressible == 962, "the texture copy cases set aside");
    MakePipelines();
    TestIndirectDraws();
    CHECK(s_differs == 876 && s_inexpressible == 966, "the indirect draw cases set aside");
    TestDispatches();
    TestVertexIndexBuffers();
    CHECK(s_differs == 878 && s_inexpressible == 966, "the draw cases set aside");
    TestRenderPasses();
    TestQueries();
    CHECK(s_differs == 879 && s_inexpressible == 966, "the pass and query cases set aside");
    Close(s_device);
    return s_failures == 0 ? 0 : 1;
}
