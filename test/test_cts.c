// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The WebGPU conformance test suite's validation cases whose calls have
// Maul RHI counterparts, on the test driver: each case names its CTS
// test, the call it becomes and the outcome the CTS expects, success or
// the library's refusal. Cases the library decides otherwise are listed
// with the reason. Translated from the CTS at a5e5d74
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
#include "shaders/instanced_container.h"
#include "shaders/noop_container.h"

#include <math.h>
#include <stdio.h>

static mrhiDevice* s_device;

// The CTS's kMaxSafeMultipleOf8: JavaScript's largest safe integer
// rounded down to a multiple of 8.
#define MAX_SAFE_MULTIPLE_OF_8 UINT64_C(9007199254740984)

// Reports a case whose outcome differs from the CTS's. A validation
// error is input a correct program never passes: invalid, or past a
// fixed bound of the library.
static void Expect(const char* test, mrhiResult result, bool succeeds)
{
    bool ok = succeeds ? result == mrhi_success
                       : result == mrhi_errorInvalid || result == mrhi_errorLimit;
    if (!ok)
    {
        printf("FAIL: %s: %d, the CTS expects %s\n", test, (int)result,
               succeeds ? "success" : "a validation error");
        ++s_failures;
    }
}

// Reports a case past one of the device's limits whose outcome differs
// from the CTS's. The CTS's validation error is mrhi_errorCapacity here:
// the adapter's limits are above the device's, so a device asking for
// more would take it.
static void ExpectPastLimit(const char* test, mrhiResult result, bool succeeds)
{
    bool ok = result == (succeeds ? mrhi_success : mrhi_errorCapacity);
    if (!ok)
    {
        printf("FAIL: %s: %d, the CTS expects %s\n", test, (int)result,
               succeeds ? "success" : "a validation error at a device limit");
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
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success &&
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
        CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success &&
                  mrhiPass_Begin(s_device, pass) == mrhi_success,
              "the pass begun");
        status = mrhiPass_CopyBuffer(s_device, pass, source, c->sourceOffset, destination,
                                     c->destinationOffset, c->size);
    }
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success &&
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
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success &&
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
        CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success &&
                  mrhiPass_Begin(s_device, pass) == mrhi_success,
              "the pass begun");
        status = mrhiPass_CopyTexture(s_device, pass, &from, &to, &size);
    }
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success &&
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

static uint8_t s_data[4u << 20];

// What became of a case's texture before the frame: nothing, destroyed,
// or never made, as the CTS's invalid texture, whose id is the zero id
// a refused def gives.
typedef enum TextureState
{
    TEXTURE_VALID,
    TEXTURE_DESTROYED,
    TEXTURE_INVALID,
} TextureState;

// A transfer case: the method, the texture's block facts and def, the
// texel copy's origin and mip, the layout, the data's size and the
// copy's size; the buffer's usages, both copy usages when none, and
// whether it is destroyed before the frame; and the texture's state.
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
    mrhiBufferUsage bufferUsage;
    bool destroyed;
    TextureState textureState;
} TransferCase;

// The case in a frame: its texture, its buffer or bytes of the data's
// size, a transfer pass declaring both, and the copy; the first refusal,
// of the texture, the imports, the pass or the copy, or success.
static mrhiResult RunTransfer(const TransferCase* c)
{
    uint64_t dataSize = c->dataSize;
    mrhiTextureId texture = {0};
    mrhiResult status = c->textureState == TEXTURE_INVALID
                            ? mrhi_success
                            : mrhiCreateTexture(s_device, &c->texture, &texture);
    if (status != mrhi_success)
    {
        return status;
    }
    CHECK(c->textureState != TEXTURE_DESTROYED ||
              mrhiDestroyTexture(s_device, texture) == mrhi_success,
          "the texture destroyed");
    mrhiBufferId buffer = {0};
    if (c->method != WRITE_TEXTURE)
    {
        buffer = MakeBuffer(dataSize, c->bufferUsage != 0 ? c->bufferUsage : COPY_SRC | COPY_DST);
        CHECK(!c->destroyed || mrhiDestroyBuffer(s_device, buffer) == mrhi_success,
              "the buffer destroyed");
    }
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiTextureCopy at = c->at;
    mrhiResourceId b = {0};
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success, "a frame");
    status = mrhiImportTexture(s_device, texture, &at.resource);
    status = status != mrhi_success || c->method == WRITE_TEXTURE
                 ? status
                 : mrhiImportBuffer(s_device, buffer, &b);
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
    status = status == mrhi_success ? mrhiAddPass(s_device, &def, &pass) : status;
    if (status == mrhi_success)
    {
        CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success &&
                  mrhiPass_Begin(s_device, pass) == mrhi_success,
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
            status = mrhiPass_WriteTexture(s_device, pass, &at, dataSize > 0 ? s_data : nullptr,
                                           dataSize, &layout, &c->size);
            break;
        case BUFFER_TO_TEXTURE:
            status = mrhiPass_CopyBufferToTexture(s_device, pass, &side, &at, &c->size);
            break;
        case TEXTURE_TO_BUFFER:
            status = mrhiPass_CopyTextureToBuffer(s_device, pass, &at, &side, &c->size);
            break;
        }
    }
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success &&
              (c->textureState != TEXTURE_VALID ||
               mrhiDestroyTexture(s_device, texture) == mrhi_success) &&
              (c->method == WRITE_TEXTURE || c->destroyed ||
               mrhiDestroyBuffer(s_device, buffer) == mrhi_success),
          "dropped");
    return status;
}

// A clearBuffer case: a buffer of the size and usage, declared as a copy
// destination by a transfer pass, and the clear.
static mrhiResult Clear(uint64_t bufferSize, mrhiBufferUsage usage, uint64_t offset, uint64_t size)
{
    mrhiBufferId buffer = MakeBuffer(bufferSize, usage);
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId resource = {0};
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success &&
              mrhiImportBuffer(s_device, buffer, &resource) == mrhi_success,
          "a frame of the buffer");
    const mrhiAccess access = Whole(resource, mrhi_accessCopyDestination);
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.neverCull = true;
    def.accesses = &access;
    def.accessCount = 1;
    mrhiPassId pass = {0};
    mrhiResult status = mrhiAddPass(s_device, &def, &pass);
    if (status == mrhi_success)
    {
        CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success &&
                  mrhiPass_Begin(s_device, pass) == mrhi_success,
              "the pass begun");
        status = mrhiPass_ClearBuffer(s_device, pass, resource, offset, size);
    }
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success &&
              mrhiDestroyBuffer(s_device, buffer) == mrhi_success,
          "dropped");
    return status;
}

// api,validation,encoding,cmds,clearBuffer: an absent offset is 0, an
// absent size MRHI_WHOLE_SIZE.
static void TestClears(void)
{
    char name[96];
    static const uint64_t sizes[][2] = {
        {0, 1}, {2, 0}, {4, 1}, {5, 0}, {8, 1}, {20, 0}, {MRHI_WHOLE_SIZE, 1}};
    for (size_t i = 0; i < 7; ++i)
    {
        snprintf(name, sizeof(name), "clearBuffer:size_alignment %lld", (long long)sizes[i][0]);
        Expect(name, Clear(16, COPY_DST, 0, sizes[i][0]), sizes[i][1] != 0);
    }
    static const uint64_t offsets[][2] = {{0, 1}, {2, 0}, {4, 1}, {5, 0}, {8, 1}, {20, 0}};
    for (size_t i = 0; i < 6; ++i)
    {
        snprintf(name, sizeof(name), "clearBuffer:offset_alignment %llu",
                 (unsigned long long)offsets[i][0]);
        Expect(name, Clear(16, COPY_DST, offsets[i][0], 8), offsets[i][1] != 0);
    }
    Expect("clearBuffer:default_args both", Clear(16, COPY_DST, 0, MRHI_WHOLE_SIZE), true);
    Expect("clearBuffer:default_args offset 4", Clear(16, COPY_DST, 4, MRHI_WHOLE_SIZE), true);
    Expect("clearBuffer:default_args size 8", Clear(16, COPY_DST, 0, 8), true);
    static const uint64_t overflows[][2] = {{0, MAX_SAFE_MULTIPLE_OF_8},
                                            {16, MAX_SAFE_MULTIPLE_OF_8},
                                            {MAX_SAFE_MULTIPLE_OF_8, 16},
                                            {MAX_SAFE_MULTIPLE_OF_8, MAX_SAFE_MULTIPLE_OF_8}};
    for (size_t i = 0; i < 4; ++i)
    {
        snprintf(name, sizeof(name), "clearBuffer:overflow %zu", i);
        Expect(name, Clear(16, COPY_DST, overflows[i][0], overflows[i][1]), false);
    }
    static const uint64_t bounds[][3] = {{0, 32, 1}, {0, 36, 0}, {32, 0, 1},  {32, 4, 0},
                                         {36, 4, 0}, {36, 0, 0}, {20, 16, 0}, {20, 12, 1}};
    for (size_t i = 0; i < 8; ++i)
    {
        snprintf(name, sizeof(name), "clearBuffer:out_of_bounds %llu,%llu",
                 (unsigned long long)bounds[i][0], (unsigned long long)bounds[i][1]);
        Expect(name, Clear(32, COPY_DST, bounds[i][0], bounds[i][1]), bounds[i][2] != 0);
    }
    for (size_t i = 0; i < sizeof(s_usages) / sizeof(s_usages[0]); ++i)
    {
        snprintf(name, sizeof(name), "clearBuffer:buffer_usage %#x", (unsigned)s_usages[i]);
        Expect(name, Clear(16, s_usages[i], 0, 16), s_usages[i] == COPY_DST);
    }
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
// than run: createTexture's and createView's 1D textures and views, and
// createBuffer's mapping, which Maul RHI does without.
static uint32_t s_inexpressible;

// Counts the cases Maul RHI decides otherwise than the CTS: from its
// layout value 0 being absent (see Owed),
// - an empty copy of several layers giving 0 for a layout value, which
//   the CTS takes and Maul RHI refuses as a value missing where needed;
// - a rowsPerImage of 0 for a copy of one layer with rows, which the CTS
//   refuses as fewer than the rows and Maul RHI takes as absent;
// setIndexBuffer's ranges of part of an index, which Maul RHI refuses
// (see TestVertexIndexBuffers); an occlusion query written in a second
// pass of the frame (see TestQueries); a view of a destroyed texture
// (see TestViewStates); a depth target's operations and read-only flags
// (see TestDepthOps); and a buffer or texture destroyed after a frame
// has used it (see TestDestroyedTargets and TestQueueDestroys).
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
                        Expect(name,
                               Transfer(method, rgba8, KindOf(shapes[s].volume, shapes[s].layers),
                                        (mrhiExtent3d){4, 4, shapes[s].layers}, 0, 1024,
                                        Value(imageRows[r]), need.bytes, size),
                               Owed(method, rgba8, 0, 1024, imageRows[r], need.bytes, size, true,
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
            Expect(name,
                   Transfer(method, rgba8, KindOf(false, depth), (mrhiExtent3d){1, 1, depth}, 0,
                            1u << 31, 1u << 31, 10000, (mrhiExtent3d){1, 1, depth}),
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
                Expect(name,
                       Transfer(method, rgba8, mrhi_texture2d, (mrhiExtent3d){4, 4, 1}, offset, 0,
                                0, data, (mrhiExtent3d){0, 0, 0}),
                       Owed(method, rgba8, offset, 0, ABSENT, data, (mrhiExtent3d){0, 0, 0}, true,
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
                Expect(
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
                Expect(name,
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
                        Expect(name,
                               Transfer(method, block, KindOf(false, depth),
                                        (mrhiExtent3d){rows_[c].width * block->width,
                                                       rows * block->height, depth},
                                        0, Value(rows_[c].bytesPerRow), rows, need.bytes, size),
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
                        Expect(name,
                               Transfer(method, block, kind, textureSize, offset, bytesPerRow,
                                        rowsPerImage, need.bytes, size),
                               Owed(method, block, offset, bytesPerRow, rowsPerImage, need.bytes,
                                    size, true, true));
                        if (need.bytes > 0)
                        {
                            snprintf(name, sizeof(name),
                                     "layout_related:required_bytes_in_copy %s f=%zu %s pad=%zu "
                                     "copy=%zu short",
                                     s_methods[m], f, volume ? "3d" : "2d", p, c);
                            Expect(name,
                                   Transfer(method, block, kind, textureSize, offset, bytesPerRow,
                                            rowsPerImage, need.bytes - 1, size),
                                   Owed(method, block, offset, bytesPerRow, rowsPerImage,
                                        need.bytes - 1, size, true, false));
                        }
                    }
                }
            }
        }
    }
}

// compute_pipeline:overrides,value,type_error and
// overrides,value,validation_error, and render_pipeline's cases of each
// stage, need a shader with constants of every type, which only a suite
// that builds containers in memory makes: they are in test_pipeline.c.

// The CTS's no-op pipelines and what its draw cases bind: a render
// target and a 16-byte index buffer.
static mrhiGraphicsPipelineId s_graphics;
static mrhiComputePipelineId s_compute;
static mrhiTextureId s_target;
static mrhiBufferId s_index;
static mrhiLimits s_limits;

// Takes the device's next record, which must answer the request with
// success, as a record of the kind.
static void TakeAnswer(mrhiDeviceNotificationKind kind, mrhiRequestId request, const char* what)
{
    mrhiDeviceNotification record;
    CHECK(mrhiDevice_TakeNotification(s_device, &record) == mrhi_success && record.kind == kind &&
              record.request.index1 == request.index1 &&
              record.request.generation == request.generation && record.outcome == mrhi_success,
          what);
}

static void Answered(mrhiRequestId request)
{
    TakeAnswer(mrhi_devicePipelineReady, request, "a pipeline ready");
}

// Whether a submitted frame finished, taking its record: a later answer
// taken is then the one it waits for.
static bool Finished(mrhiRequestId token)
{
    if (mrhiRequest_WaitFrame(s_device, token, 1) != mrhi_success)
    {
        return false;
    }
    TakeAnswer(mrhi_deviceFrameDone, token, "a frame done");
    return true;
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
    Answered(request);
    mrhiComputePipelineDef compute = mrhiDefaultComputePipelineDef();
    compute.shader = shader;
    compute.entry = "cs";
    compute.entryLength = 2;
    CHECK(mrhiCreateComputePipeline(s_device, &compute, &s_compute, &request) == mrhi_success,
          "the compute pipeline");
    Answered(request);
    CHECK(mrhiDestroyShader(s_device, shader) == mrhi_success, "the shader destroyed");
    mrhiTextureDef target = mrhiDefaultTextureDef();
    target.format = mrhi_formatRgba8Unorm;
    target.width = 4;
    target.height = 4;
    target.usage = mrhi_textureRenderTarget;
    CHECK(mrhiCreateTexture(s_device, &target, &s_target) == mrhi_success, "the target");
    s_index = MakeBuffer(16, mrhi_bufferIndex);
    CHECK(mrhiDevice_GetLimits(s_device, &s_limits) == mrhi_success, "the limits");
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
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success &&
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
        CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success &&
                  mrhiPass_Begin(s_device, caseOut->pass) == mrhi_success &&
                  (render ? mrhiPass_SetGraphicsPipeline(s_device, caseOut->pass, s_graphics)
                          : mrhiPass_SetComputePipeline(s_device, caseOut->pass, s_compute)) ==
                      mrhi_success,
              "the pass begun");
    }
    return status;
}

static void EndCase(mrhiBufferId buffer)
{
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success &&
              mrhiDestroyBuffer(s_device, buffer) == mrhi_success,
          "dropped");
}

// An indirect draw in a render pass of a buffer of the size and usage,
// indexed or not, with the index buffer set when indexed.
static mrhiResult DrawIndirect(uint64_t size, mrhiBufferUsage usage, bool indexed, uint64_t offset)
{
    mrhiBufferId buffer = MakeBuffer(size, usage);
    CasePass c = {0};
    mrhiResult status = BeginCase(buffer, mrhi_accessIndirect, true, &c);
    if (status == mrhi_success && indexed)
    {
        CHECK(mrhiPass_SetIndexBuffer(s_device, c.pass, c.index, mrhi_indexUint32, 0,
                                      MRHI_WHOLE_SIZE) == mrhi_success,
              "the index buffer");
        status = mrhiPass_DrawIndexedIndirect(s_device, c.pass, c.buffer, offset);
    }
    else if (status == mrhi_success)
    {
        status = mrhiPass_DrawIndirect(s_device, c.pass, c.buffer, offset);
    }
    EndCase(buffer);
    return status;
}

// limits,maxBindGroupsPlusVertexBuffers:draw,at_over, preferring
// vertex buffers, on a device of 32 vertex buffers: one vertex buffer
// set at the slot that brings the slots used to the limit or one past
// it, then a draw of each kind with the no-op pipeline, which reads no
// vertex buffer. Its preference for bind groups has no counterpart: a
// table is set only for a pipeline that reads it, and the no-op
// pipeline reads none.
static void TestBindGroupsPlusVertexBuffers(void)
{
    s_adapter.limits.vertexBuffers = 32;
    s_adapter.limits.tablesPlusVertexBuffers = 64;
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.limits.vertexBuffers = 32;
    s_device = OpenWith(deviceDef, true);
    MakePipelines();
    static const char* const draws[] = {"draw", "drawIndexed", "drawIndirect",
                                        "drawIndexedIndirect"};
    char name[160];
    for (uint32_t over = 0; over < 2; ++over)
    {
        uint32_t slots = s_limits.tablesPlusVertexBuffers + over;
        for (uint32_t d = 0; d < 4; ++d)
        {
            mrhiBufferId buffer = MakeBuffer(32, mrhi_bufferVertex | mrhi_bufferIndirect);
            mrhiFrameDef frame = mrhiDefaultFrameDef();
            mrhiResourceId target = {0};
            mrhiResourceId vertices = {0};
            mrhiResourceId index = {0};
            CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success &&
                      mrhiImportBuffer(s_device, buffer, &vertices) == mrhi_success &&
                      mrhiImportBuffer(s_device, s_index, &index) == mrhi_success &&
                      mrhiImportTexture(s_device, s_target, &target) == mrhi_success,
                  "a frame of the case");
            const mrhiAccess accesses[3] = {{.resource = vertices, .kind = mrhi_accessVertex},
                                            {.resource = vertices, .kind = mrhi_accessIndirect},
                                            {.resource = index, .kind = mrhi_accessIndex}};
            mrhiPassDef def = mrhiDefaultPassDef();
            def.neverCull = true;
            def.accesses = accesses;
            def.accessCount = 3;
            def.colorTargets[0] = (mrhiColorTarget){.resource = target, .load = mrhi_loadClear};
            def.colorTargetCount = 1;
            mrhiPassId pass = {0};
            CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success &&
                      mrhiDevice_CompileFrame(s_device) == mrhi_success &&
                      mrhiPass_Begin(s_device, pass) == mrhi_success &&
                      mrhiPass_SetVertexBuffer(s_device, pass, slots - 1, vertices, 0,
                                               MRHI_WHOLE_SIZE) == mrhi_success &&
                      mrhiPass_SetGraphicsPipeline(s_device, pass, s_graphics) == mrhi_success &&
                      mrhiPass_SetIndexBuffer(s_device, pass, index, mrhi_indexUint16, 0,
                                              MRHI_WHOLE_SIZE) == mrhi_success,
                  "the pass begun");
            mrhiResult status = d == 0   ? mrhiPass_Draw(s_device, pass, 0, 1, 0, 0)
                                : d == 1 ? mrhiPass_DrawIndexed(s_device, pass, 0, 1, 0, 0, 0)
                                : d == 2
                                    ? mrhiPass_DrawIndirect(s_device, pass, vertices, 0)
                                    : mrhiPass_DrawIndexedIndirect(s_device, pass, vertices, 0);
            snprintf(name, sizeof(name),
                     "limits,maxBindGroupsPlusVertexBuffers:draw,at_over, %s, %s, vertex buffers",
                     over ? "over" : "at", draws[d]);
            ExpectPastLimit(name, status, over == 0);
            CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success &&
                      mrhiDestroyBuffer(s_device, buffer) == mrhi_success,
                  "dropped");
        }
    }
    Close(s_device);
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
            Expect(name, DrawIndirect(256, mrhi_bufferIndirect, indexed, offset), offset % 4 == 0);
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
            Expect(name,
                   DrawIndirect(bounds[i].size, mrhi_bufferIndirect, indexed, bounds[i].offset),
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
        status = indirect ? mrhiPass_DispatchIndirect(s_device, c.pass, c.buffer, 0)
                          : mrhiPass_Dispatch(s_device, c.pass, x, y, z);
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
                    ExpectPastLimit(name,
                                    Dispatch(indirect != 0, mrhi_bufferIndirect, 12, sizes[0],
                                             sizes[1], sizes[2]),
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
    mrhiResult status =
        vertex ? mrhiPass_SetVertexBuffer(s_device, c.pass, slot, c.buffer, offset, size)
               : mrhiPass_SetIndexBuffer(s_device, c.pass, c.buffer, format, offset, size);
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
        ExpectPastLimit(name, SetBuffer(true, 16, slots[i], mrhi_indexUint32, 0, MRHI_WHOLE_SIZE),
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
                    Expect(name, RunTransfer(&c), (usage & needed) != 0);
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
            Expect(name, RunTransfer(&c), samples == 1);
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
                    Expect(name, RunTransfer(&c), mips[l] < mipCounts[n]);
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
                        Expect(name, RunTransfer(&c),
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
                        Expect(
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
                                Expect(name, RunTransfer(&c),
                                       Owed(method, rgba8, 0, bytesPerRow, copy[1], 1, size, rest,
                                            rest));
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
    // A depth target's: kept rather than cleared, and the clear value.
    bool keepDepth;
    float clearDepth;
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
// target or none, or left undefined by format none, and a depth
// attachment or none, declared; the pass's refusal, or success. The
// CTS's loads clear and its stores store.
static mrhiResult RenderPass(const Attachment* colors, const Attachment* resolves, uint32_t count,
                             Attachment depth)
{
    mrhiTextureId textures[2 * MRHI_MAX_COLOR_TARGETS + 2] = {0};
    uint32_t made = 0;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success, "a frame");
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    for (uint32_t i = 0; i < count && i < MRHI_MAX_COLOR_TARGETS; ++i)
    {
        mrhiColorTarget* target = &def.colorTargets[i];
        if (colors[i].format == mrhi_formatNone)
        {
            continue;
        }
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
        def.depthTarget.depthLoad = depth.keepDepth ? mrhi_loadKeep : mrhi_loadClear;
        def.depthTarget.depthStore = mrhi_storeKeep;
        def.depthTarget.clearDepth = depth.clearDepth;
        def.depthTarget.stencilLoad = mrhi_loadClear;
        def.depthTarget.stencilStore = mrhi_storeKeep;
    }
    mrhiPassId pass = {0};
    mrhiResult status = mrhiAddPass(s_device, &def, &pass);
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success, "dropped");
    for (uint32_t i = 0; i < made; ++i)
    {
        CHECK(mrhiDestroyTexture(s_device, textures[i]) == mrhi_success, "destroyed");
    }
    return status;
}

#define RGBA8 mrhi_formatRgba8Unorm
#define DEPTH mrhi_formatDepthStencil
static const Attachment s_none = {0};

// A pass with one color target of a format, made with the usages,
// loaded and stored as given; the pass's refusal, or success.
static mrhiResult OpsPass(mrhiFormat format, mrhiTextureUsage usage, mrhiLoadOp load,
                          mrhiStoreOp store)
{
    mrhiTextureId texture = MakeAttachment(&(Attachment){.format = format, .usage = usage});
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success, "a frame");
    mrhiResourceId resource = {0};
    CHECK(mrhiImportTexture(s_device, texture, &resource) == mrhi_success, "imported");
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.colorTargets[0] = (mrhiColorTarget){.resource = resource, .load = load, .store = store};
    def.colorTargetCount = 1;
    mrhiPassId pass = {0};
    mrhiResult status = mrhiAddPass(s_device, &def, &pass);
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success &&
              mrhiDestroyTexture(s_device, texture) == mrhi_success,
          "dropped");
    return status;
}

// color_attachments,loadOp_storeOp: a transient texture is cleared and
// discarded.
static void TestTransientTargets(void)
{
    char name[128];
    static const mrhiFormat formats[] = {RGBA8, mrhi_formatRgba16Float};
    const mrhiTextureUsage transient = mrhi_textureRenderTarget | mrhi_textureTransient;
    for (size_t f = 0; f < sizeof(formats) / sizeof(formats[0]); ++f)
    {
        for (uint32_t t = 0; t < 2; ++t)
        {
            for (uint32_t op = 0; op < 4; ++op)
            {
                mrhiLoadOp load = op & 1 ? mrhi_loadKeep : mrhi_loadClear;
                mrhiStoreOp store = op & 2 ? mrhi_storeKeep : mrhi_storeDiscard;
                snprintf(name, sizeof(name),
                         "render_pass_descriptor:color_attachments,loadOp_storeOp format %d, %s, "
                         "%s, %s",
                         (int)formats[f], t ? "transient" : "not transient",
                         op & 1 ? "load" : "clear", op & 2 ? "store" : "discard");
                Expect(name,
                       OpsPass(formats[f], t ? transient : mrhi_textureRenderTarget, load, store),
                       t == 0 || op == 0);
            }
        }
    }
}

// A depth_stencil_attachment,loadOp_storeOp_match_depthReadOnly_stencilReadOnly
// parameter: undefined (0), or the CTS's first (1) or second (2) choice:
// true and false for a read-only flag, clear and load for a load, and
// discard and store for a store.
//
// The CTS case a depth target's C call stands for. C always gives both
// operations, and keep and keep stand for none given: so a present,
// written aspect's undefined operation is keep, a load or a store, and
// an absent or read-only aspect's load or store is keep, none given.
static void Spelled(bool depth, bool stencil, const uint32_t p[6], uint32_t q[6])
{
    const bool written[2] = {depth && p[0] != 1, stencil && p[3] != 1};
    for (uint32_t a = 0; a < 2; ++a)
    {
        q[3 * a] = p[3 * a];
        for (uint32_t i = 1; i < 3; ++i)
        {
            uint32_t v = p[3 * a + i];
            q[3 * a + i] = written[a] ? (v == 0 ? 2 : v) : (v == 1 ? 1 : 0);
        }
    }
}

// The CTS's outcome of a case on a format with the aspects given.
static bool OpsMatch(bool depth, bool stencil, bool transient, const uint32_t p[6])
{
    bool depthReadOnly = p[0] == 1;
    bool stencilReadOnly = p[3] == 1;
    bool depthSettings = p[1] != 0 && p[2] != 0 && !depthReadOnly;
    bool stencilSettings = p[4] != 0 && p[5] != 0 && !stencilReadOnly;
    bool present = (!depthSettings || depth) && (!stencilSettings || stencil);
    bool depthCombo = depth && !depthReadOnly ? p[1] != 0 && p[2] != 0 : p[1] == 0 && p[2] == 0;
    bool stencilCombo =
        stencil && !stencilReadOnly ? p[4] != 0 && p[5] != 0 : p[4] == 0 && p[5] == 0;
    bool transientOps = !transient || ((!depth || (p[1] == 1 && p[2] == 1)) &&
                                       (!stencil || (p[4] == 1 && p[5] == 1)));
    return present && depthCombo && stencilCombo && transientOps;
}

// The case as a depth target: one read-only flag for both aspects, and
// each operation clear or discard as given, else keep.
static mrhiResult DepthOpsPass(mrhiFormat format, bool transient, bool readOnly,
                               const uint32_t p[6])
{
    mrhiTextureUsage usage = mrhi_textureRenderTarget | (transient ? mrhi_textureTransient : 0);
    mrhiTextureId texture = MakeAttachment(&(Attachment){.format = format, .usage = usage});
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId resource = {0};
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success &&
              mrhiImportTexture(s_device, texture, &resource) == mrhi_success,
          "a frame of the target");
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.depthTarget = (mrhiDepthTarget){
        .resource = resource,
        .depthLoad = p[1] == 1 ? mrhi_loadClear : mrhi_loadKeep,
        .depthStore = p[2] == 1 ? mrhi_storeDiscard : mrhi_storeKeep,
        .stencilLoad = p[4] == 1 ? mrhi_loadClear : mrhi_loadKeep,
        .stencilStore = p[5] == 1 ? mrhi_storeDiscard : mrhi_storeKeep,
        .readOnly = readOnly,
    };
    mrhiPassId pass = {0};
    mrhiResult status = mrhiAddPass(s_device, &def, &pass);
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success &&
              mrhiDestroyTexture(s_device, texture) == mrhi_success,
          "dropped");
    return status;
}

// The CTS's kDepthStencilFormats and their aspects. depth32float is the
// library's, and depth24plus-stencil8 becomes its depth-stencil format;
// the other four have no counterpart, mrhi_formatNone.
typedef struct DepthStencilFormat
{
    const char* name;
    mrhiFormat format;
    bool depth;
    bool stencil;
} DepthStencilFormat;

static const DepthStencilFormat s_depthStencilFormats[] = {
    {"stencil8", mrhi_formatNone, false, true},
    {"depth16unorm", mrhi_formatNone, true, false},
    {"depth32float", mrhi_formatDepth32Float, true, false},
    {"depth24plus", mrhi_formatNone, true, false},
    {"depth24plus-stencil8", DEPTH, true, true},
    {"depth32float-stencil8", mrhi_formatNone, true, true},
};

#define DEPTH_STENCIL_FORMATS (sizeof(s_depthStencilFormats) / sizeof(s_depthStencilFormats[0]))

// render_pass_descriptor:depth_stencil_attachment,loadOp_storeOp_match_depthReadOnly_stencilReadOnly,
// over the CTS's kDepthStencilFormats, whose four without a counterpart
// are counted. Each case runs as the C call
// Spelled finds, whose outcome is that of the CTS case it stands for;
// where that differs from the case's own, as keep stands for none given,
// the case differs. The library's read-only flag covers both aspects, so
// a case whose aspects differ in it has no call and differs too.
static void TestDepthOps(void)
{
    const DepthStencilFormat* formats = s_depthStencilFormats;
    static const char* const readOnlys[] = {"undefined", "true", "false"};
    static const char* const loads[] = {"undefined", "clear", "load"};
    static const char* const stores[] = {"undefined", "discard", "store"};
    char name[256];
    for (size_t f = 0; f < DEPTH_STENCIL_FORMATS; ++f)
    {
        for (uint32_t t = 0; t < 2; ++t)
        {
            for (uint32_t c = 0; c < 729; ++c)
            {
                // depthReadOnly, depthLoadOp, depthStoreOp, stencilReadOnly,
                // stencilLoadOp, stencilStoreOp, the last fastest.
                const uint32_t p[6] = {c / 243,   c / 81 % 3, c / 27 % 3,
                                       c / 9 % 3, c / 3 % 3,  c % 3};
                bool depth = formats[f].depth;
                bool stencil = formats[f].stencil;
                bool depthReadOnly = p[0] == 1;
                bool stencilReadOnly = p[3] == 1;
                if (formats[f].format == mrhi_formatNone)
                {
                    ++s_inexpressible;
                    continue;
                }
                if (depth && stencil && depthReadOnly != stencilReadOnly)
                {
                    ++s_differs;
                    continue;
                }
                snprintf(name, sizeof(name),
                         "render_pass_descriptor:depth_stencil_attachment,loadOp_storeOp_match_"
                         "depthReadOnly_stencilReadOnly %s %s depth %s,%s,%s stencil %s,%s,%s",
                         formats[f].name, t ? "transient" : "not transient", readOnlys[p[0]],
                         loads[p[1]], stores[p[2]], readOnlys[p[3]], loads[p[4]], stores[p[5]]);
                uint32_t q[6];
                Spelled(depth, stencil, p, q);
                bool valid = OpsMatch(depth, stencil, t == 1, q);
                Expect(name, DepthOpsPass(formats[f].format, t == 1, depthReadOnly, p), valid);
                s_differs += valid != OpsMatch(depth, stencil, t == 1, p) ? 1 : 0;
            }
        }
    }
}

// api,validation,createView:texture_view_usage_of_multiple_usages: on an
// rgba8unorm texture of two usages, a view takes no usage or any of them,
// but one of a transient texture takes none or all.
static void TestViewUsages(void)
{
    char name[160];
    // The CTS's usages, in its order.
    static const mrhiTextureUsage usages[] = {
        mrhi_textureCopySource, mrhi_textureCopyDestination, mrhi_textureSampled,
        mrhi_textureStorage,    mrhi_textureRenderTarget,    mrhi_textureTransient,
    };
    const mrhiTextureUsage transient = mrhi_textureRenderTarget | mrhi_textureTransient;
    const size_t count = sizeof(usages) / sizeof(usages[0]);
    for (size_t i = 0; i < count; ++i)
    {
        for (size_t j = i; j < count; ++j)
        {
            mrhiTextureUsage usage = usages[i] | usages[j];
            // The CTS's valid combinations: transient only with a target.
            if ((usage & mrhi_textureTransient) != 0 && usage != transient)
            {
                continue;
            }
            mrhiTextureDef textureDef = mrhiDefaultTextureDef();
            textureDef.format = RGBA8;
            textureDef.width = 1;
            textureDef.height = 1;
            textureDef.usage = usage;
            mrhiTextureId texture = {0};
            CHECK(mrhiCreateTexture(s_device, &textureDef, &texture) == mrhi_success,
                  "a texture of two usages");
            const mrhiTextureUsage views[] = {0, usages[i], usages[j], usage};
            for (size_t v = 0; v < sizeof(views) / sizeof(views[0]); ++v)
            {
                mrhiViewDef def = mrhiDefaultViewDef();
                def.texture = texture;
                def.usage = views[v];
                mrhiViewId view = {0};
                mrhiResult status = mrhiCreateView(s_device, &def, &view);
                bool valid =
                    (usage & mrhi_textureTransient) == 0 || views[v] == 0 || views[v] == usage;
                snprintf(name, sizeof(name),
                         "createView:texture_view_usage_of_multiple_usages usage 0x%x, view 0x%x",
                         (unsigned)usage, (unsigned)views[v]);
                Expect(name, status, valid);
                if (status == mrhi_success)
                {
                    CHECK(mrhiDestroyView(s_device, view) == mrhi_success, "the view");
                }
            }
            CHECK(mrhiDestroyTexture(s_device, texture) == mrhi_success, "the texture");
        }
    }
}

// A target one texel wide of the format, layers and mips, transient or
// not, viewed as the view format if one is given: its refusal, or success.
static mrhiResult MakeTarget(mrhiFormat format, uint32_t layers, uint32_t mips, bool transient,
                             mrhiFormat view)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.kind = layers > 1 ? mrhi_texture2dArray : mrhi_texture2d;
    def.format = format;
    def.width = 1;
    def.height = 1;
    def.depthOrLayers = layers;
    def.mipLevels = mips;
    def.usage = mrhi_textureRenderTarget | (transient ? mrhi_textureTransient : 0);
    def.viewFormats[0] = view;
    mrhiTextureId texture = {0};
    mrhiResult status = mrhiCreateTexture(s_device, &def, &texture);
    if (status == mrhi_success)
    {
        CHECK(mrhiDestroyTexture(s_device, texture) == mrhi_success, "the target");
    }
    return status;
}

// createTexture:depthOrArrayLayers_and_mipLevelCount_for_transient_attachments
// and createTexture:transient_viewFormats: a transient texture is one
// layer of one mip, without view formats. depth24plus becomes the
// library's depth-stencil format. At the CTS's size of one texel, two
// mips are past the texture's mip count too.
static void TestTransientTextures(void)
{
    char name[192];
    static const mrhiFormat formats[] = {RGBA8, DEPTH};
    for (size_t f = 0; f < sizeof(formats) / sizeof(formats[0]); ++f)
    {
        for (uint32_t layers = 1; layers <= 2; ++layers)
        {
            for (uint32_t mips = 1; mips <= 2; ++mips)
            {
                snprintf(name, sizeof(name),
                         "createTexture:depthOrArrayLayers_and_mipLevelCount_for_transient_"
                         "attachments format %d, %u layers, %u mips",
                         (int)formats[f], layers, mips);
                Expect(name, MakeTarget(formats[f], layers, mips, true, mrhi_formatNone),
                       layers == 1 && mips == 1);
            }
        }
    }
    static const mrhiFormat pairs[][2] = {{RGBA8, mrhi_formatRgba8UnormSrgb},
                                          {mrhi_formatRgba8UnormSrgb, RGBA8}};
    // The CTS's subcases: transient or not, and its view format, none,
    // the texture's own or the other one of the pair.
    static const struct
    {
        bool transient;
        uint32_t view;
    } subcases[] = {{true, 0}, {false, 1}, {false, 2}, {true, 1}, {true, 2}};
    for (size_t p = 0; p < 2; ++p)
    {
        for (size_t s = 0; s < sizeof(subcases) / sizeof(subcases[0]); ++s)
        {
            uint32_t view = subcases[s].view;
            bool transient = subcases[s].transient;
            snprintf(name, sizeof(name), "createTexture:transient_viewFormats format %d, %s, %s",
                     (int)pairs[p][0], transient ? "transient" : "not transient",
                     view == 0   ? "no view format"
                     : view == 1 ? "its own format"
                                 : "the twin");
            mrhiFormat format = view == 0 ? mrhi_formatNone : pairs[p][view - 1];
            Expect(name, MakeTarget(pairs[p][0], 1, 1, transient, format), !transient || view == 0);
        }
    }
}

// The CTS's texture dimensions: absent, which is 2D, and its three.
typedef enum Dimension
{
    DIMENSION_DEFAULT,
    DIMENSION_1D,
    DIMENSION_2D,
    DIMENSION_3D,
} Dimension;

static const char* const s_dimensions[] = {"default", "1d", "2d", "3d"};

#define LAST_FORMAT mrhi_formatAstc12x12UnormSrgb

// WebGPU's texel block of a format, as the CTS's tables give it: one
// texel for the uncompressed formats, 4x4 for BC, ETC2 and EAC, and an
// ASTC format's own, in pairs of unorm and sRGB.
static mrhiExtent3d BlockOf(mrhiFormat format)
{
    static const uint32_t astc[][2] = {{4, 4},  {5, 4},   {5, 5},   {6, 5},  {6, 6},
                                       {8, 5},  {8, 6},   {8, 8},   {10, 5}, {10, 6},
                                       {10, 8}, {10, 10}, {12, 10}, {12, 12}};
    if (format < mrhi_formatBc1RgbaUnorm)
    {
        return (mrhiExtent3d){1, 1, 1};
    }
    if (format < mrhi_formatAstc4x4Unorm)
    {
        return (mrhiExtent3d){4, 4, 1};
    }
    const uint32_t* size = astc[(format - mrhi_formatAstc4x4Unorm) / 2];
    return (mrhiExtent3d){size[0], size[1], 1};
}

// Whether the CTS runs a format in a dimension. It filters out a depth
// or compressed format in 1D and a depth format in 3D, and skips a
// compressed one in 3D on a device without the sliced 3D features,
// which the library does not have.
static bool Runs(Dimension dimension, mrhiFormat format)
{
    bool compressed = format >= mrhi_formatBc1RgbaUnorm;
    bool depth = format == mrhi_formatDepth32Float || format == DEPTH;
    return (dimension != DIMENSION_1D && dimension != DIMENSION_3D) || (!compressed && !depth);
}

// The CTS's maxMipLevelCount: a full chain over the width and height,
// and the depth too in 3D.
static uint32_t MaxMips(Dimension dimension, mrhiExtent3d size)
{
    uint32_t largest = size.width > size.height ? size.width : size.height;
    if (dimension == DIMENSION_3D && size.depthOrLayers > largest)
    {
        largest = size.depthOrLayers;
    }
    uint32_t levels = 1;
    for (; largest > 1; largest >>= 1)
    {
        ++levels;
    }
    return levels;
}

// A texture of a dimension, format, size, mips, sample count and
// usages, a 2D one of several layers a 2D array: its refusal, or
// success.
static mrhiResult CreateSampled(Dimension dimension, mrhiFormat format, mrhiExtent3d size,
                                uint32_t mips, uint32_t samples, mrhiTextureUsage usage)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.kind = KindOf(dimension == DIMENSION_3D, size.depthOrLayers);
    def.format = format;
    def.width = size.width;
    def.height = size.height;
    def.depthOrLayers = size.depthOrLayers;
    def.mipLevels = mips;
    def.sampleCount = samples;
    def.usage = usage;
    mrhiTextureId texture = {0};
    mrhiResult status = mrhiCreateTexture(s_device, &def, &texture);
    if (status == mrhi_success)
    {
        CHECK(mrhiDestroyTexture(s_device, texture) == mrhi_success, "the texture");
    }
    return status;
}

// One of one sample.
static mrhiResult CreateTexture(Dimension dimension, mrhiFormat format, mrhiExtent3d size,
                                uint32_t mips, mrhiTextureUsage usage)
{
    return CreateSampled(dimension, format, size, mips, 1, usage);
}

// createTexture:zero_size_and_usage: one block of the format, with the
// width, height, layers, mips or usages 0 in turn. rgb10a2unorm and
// depth24plus-stencil8 become the library's rgb10a2 and depth-stencil
// formats. The library has no 1D texture, so those cases are counted.
static void TestZeroSizes(void)
{
    static const mrhiFormat formats[] = {RGBA8, mrhi_formatRgb10a2Unorm, mrhi_formatBc1RgbaUnorm,
                                         DEPTH};
    static const char* const zeros[] = {"none",          "width", "height", "depthOrArrayLayers",
                                        "mipLevelCount", "usage"};
    char name[160];
    for (Dimension d = DIMENSION_DEFAULT; d <= DIMENSION_3D; ++d)
    {
        for (size_t f = 0; f < sizeof(formats) / sizeof(formats[0]); ++f)
        {
            if (!Runs(d, formats[f]))
            {
                continue;
            }
            for (uint32_t z = 0; z < 6; ++z)
            {
                if (d == DIMENSION_1D)
                {
                    ++s_inexpressible;
                    continue;
                }
                mrhiExtent3d size = BlockOf(formats[f]);
                size.width = z == 1 ? 0 : size.width;
                size.height = z == 2 ? 0 : size.height;
                size.depthOrLayers = z == 3 ? 0 : 1;
                snprintf(name, sizeof(name),
                         "createTexture:zero_size_and_usage %s, format %d, %s zero",
                         s_dimensions[d], (int)formats[f], zeros[z]);
                Expect(name,
                       CreateTexture(d, formats[f], size, z == 4 ? 0 : 1,
                                     z == 5 ? 0 : mrhi_textureSampled),
                       z == 0);
            }
        }
    }
}

// createTexture:mipLevelCount,format: every format at one block, with
// its width, height or layers raised to 17 to 32 texels in whole
// blocks, and 1, 2, 3, 6 or 7 mips; valid up to a full chain. A 1D
// texture takes only the width, and is counted.
static void TestMipCounts(void)
{
    static const uint32_t counts[] = {1, 2, 3, 6, 7};
    char name[160];
    for (Dimension d = DIMENSION_DEFAULT; d <= DIMENSION_3D; ++d)
    {
        for (mrhiFormat format = RGBA8; format <= LAST_FORMAT; ++format)
        {
            if (!Runs(d, format))
            {
                continue;
            }
            mrhiExtent3d block = BlockOf(format);
            for (uint32_t m = 0; m < sizeof(counts) / sizeof(counts[0]); ++m)
            {
                for (uint32_t largest = 0; largest < (d == DIMENSION_1D ? 1u : 3u); ++largest)
                {
                    if (d == DIMENSION_1D)
                    {
                        ++s_inexpressible;
                        continue;
                    }
                    mrhiExtent3d size = block;
                    size.width = largest == 0 ? 31 / block.width * block.width : size.width;
                    size.height = largest == 1 ? 31 / block.height * block.height : size.height;
                    size.depthOrLayers = largest == 2 ? 31 : 1;
                    snprintf(name, sizeof(name),
                             "createTexture:mipLevelCount,format %s, format %d, %u mips, "
                             "largest dimension %u",
                             s_dimensions[d], (int)format, counts[m], largest);
                    Expect(name, CreateTexture(d, format, size, counts[m], mrhi_textureSampled),
                           counts[m] <= MaxMips(d, size));
                }
            }
        }
    }
}

// createTexture:mipLevelCount,bound_check and its
// bigger_than_integer_bit_width: a full chain is valid and one more mip
// is not, on power of two, odd and uneven sizes; a 2D array's layers
// take no mips. bc1-rgba-unorm takes only the 2D sizes in whole blocks.
// The 1D sizes are counted.
static void TestMipBounds(void)
{
    static const struct
    {
        Dimension dimension;
        mrhiExtent3d size;
    } sizes[] = {
        {DIMENSION_DEFAULT, {32, 32, 1}},  {DIMENSION_DEFAULT, {31, 32, 1}},
        {DIMENSION_DEFAULT, {28, 32, 1}},  {DIMENSION_DEFAULT, {32, 31, 1}},
        {DIMENSION_DEFAULT, {32, 28, 1}},  {DIMENSION_DEFAULT, {31, 31, 1}},
        {DIMENSION_1D, {32, 1, 1}},        {DIMENSION_1D, {31, 1, 1}},
        {DIMENSION_3D, {32, 32, 32}},      {DIMENSION_3D, {32, 31, 31}},
        {DIMENSION_3D, {31, 32, 31}},      {DIMENSION_3D, {31, 31, 32}},
        {DIMENSION_3D, {31, 31, 31}},      {DIMENSION_DEFAULT, {32, 8, 1}},
        {DIMENSION_DEFAULT, {32, 32, 64}}, {DIMENSION_3D, {32, 32, 64}},
    };
    static const mrhiFormat formats[] = {RGBA8, mrhi_formatBc1RgbaUnorm};
    char name[160];
    for (size_t f = 0; f < 2; ++f)
    {
        for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); ++s)
        {
            Dimension d = sizes[s].dimension;
            mrhiExtent3d size = sizes[s].size;
            if (formats[f] != RGBA8 &&
                (d != DIMENSION_DEFAULT || size.width % 4 != 0 || size.height % 4 != 0))
            {
                continue;
            }
            if (d == DIMENSION_1D)
            {
                ++s_inexpressible;
                continue;
            }
            uint32_t mips = MaxMips(d, size);
            snprintf(name, sizeof(name),
                     "createTexture:mipLevelCount,bound_check format %d, %s %ux%ux%u, %u mips",
                     (int)formats[f], s_dimensions[d], size.width, size.height, size.depthOrLayers,
                     mips);
            Expect(name, CreateTexture(d, formats[f], size, mips, mrhi_textureSampled), true);
            Expect(name, CreateTexture(d, formats[f], size, mips + 1, mrhi_textureSampled), false);
        }
    }
    Expect("createTexture:mipLevelCount,bound_check,bigger_than_integer_bit_width",
           CreateTexture(DIMENSION_DEFAULT, RGBA8, (mrhiExtent3d){32, 32, 1}, 100,
                         mrhi_textureSampled),
           false);
}

// createTexture:dimension_type_and_format_compatibility: one block of
// every format in every dimension. A compressed or depth format takes
// no 1D or 3D texture on a device without the sliced 3D features. The
// 1D cases are counted.
static void TestDimensionFormats(void)
{
    char name[160];
    for (Dimension d = DIMENSION_DEFAULT; d <= DIMENSION_3D; ++d)
    {
        for (mrhiFormat format = RGBA8; format <= LAST_FORMAT; ++format)
        {
            if (d == DIMENSION_1D)
            {
                ++s_inexpressible;
                continue;
            }
            snprintf(name, sizeof(name),
                     "createTexture:dimension_type_and_format_compatibility %s, format %d",
                     s_dimensions[d], (int)format);
            Expect(name, CreateTexture(d, format, BlockOf(format), 1, mrhi_textureSampled),
                   Runs(d, format));
        }
    }
}

// Reports a case refused for a format capability. The CTS's validation
// error is mrhi_errorUnsupported here: the library takes the
// capabilities an adapter grants, and the test adapter grants what
// WebGPU's table gives a device without optional features.
static void ExpectUngranted(const char* test, mrhiResult result, bool succeeds)
{
    bool ok = result == (succeeds ? mrhi_success : mrhi_errorUnsupported);
    if (!ok)
    {
        printf("FAIL: %s: %d, the CTS expects %s\n", test, (int)result,
               succeeds ? "success" : "a validation error for an ungranted capability");
        ++s_failures;
    }
}

// WebGPU's table on a device without optional features, as the CTS's
// format info gives it: whether a format multisamples, which needs
// rg11b10ufloat-renderable for rg11b10ufloat, and whether it takes
// write-only storage.
static bool Multisamples(mrhiFormat format)
{
    switch (format)
    {
    case mrhi_formatRg32Float:
    case mrhi_formatRgba32Float:
    case mrhi_formatR32Uint:
    case mrhi_formatR32Sint:
    case mrhi_formatRg11b10Ufloat:
        return false;
    default:
        return format < mrhi_formatBc1RgbaUnorm;
    }
}

static bool Stores(mrhiFormat format)
{
    switch (format)
    {
    case RGBA8:
    case mrhi_formatRgba16Float:
    case mrhi_formatR32Float:
    case mrhi_formatRg32Float:
    case mrhi_formatRgba32Float:
    case mrhi_formatR32Uint:
    case mrhi_formatR32Sint:
        return true;
    default:
        return false;
    }
}

// The CTS's isTextureFormatPossiblyStorageReadable: storage, or
// read-only and write-only storage with texture-formats-tier1.
static bool PossiblyStores(mrhiFormat format)
{
    switch (format)
    {
    case mrhi_formatR8Unorm:
    case mrhi_formatRg8Unorm:
    case mrhi_formatR16Float:
    case mrhi_formatRg16Float:
    case mrhi_formatRgb10a2Unorm:
    case mrhi_formatRg11b10Ufloat:
        return true;
    default:
        return Stores(format);
    }
}

// The color formats, which the CTS may render to: rg11b10ufloat only
// with rg11b10ufloat-renderable, so its cases are skipped.
static bool IsColor(mrhiFormat format)
{
    return format < mrhi_formatDepth32Float;
}

// Whether a format renders on a device without optional features: a
// depth format, or a color one but rg11b10ufloat and the compressed
// ones.
static bool Renders(mrhiFormat format)
{
    return format < mrhi_formatBc1RgbaUnorm && format != mrhi_formatRg11b10Ufloat;
}

// createTexture:sampleCount,various_sampleCount_with_all_formats: every
// format, 32 blocks wide and high, with 0 to 256 samples, a render
// target when several. 1 is valid, and 4 where the format multisamples.
// A count of 2 is refused as ungranted: the library takes it where an
// adapter grants it, which a WebGPU adapter never does.
static void TestSampleCounts(void)
{
    static const uint32_t counts[] = {0, 1, 2, 4, 8, 16, 32, 256};
    static const Dimension dimensions[] = {DIMENSION_DEFAULT, DIMENSION_2D};
    char name[160];
    for (size_t d = 0; d < 2; ++d)
    {
        for (mrhiFormat format = RGBA8; format <= LAST_FORMAT; ++format)
        {
            mrhiExtent3d size = BlockOf(format);
            size.width *= 32;
            size.height *= 32;
            for (size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); ++c)
            {
                uint32_t count = counts[c];
                mrhiResult status =
                    CreateSampled(dimensions[d], format, size, 1, count,
                                  mrhi_textureSampled | (count > 1 ? mrhi_textureRenderTarget : 0));
                snprintf(name, sizeof(name),
                         "createTexture:sampleCount,various_sampleCount_with_all_formats %s, "
                         "format %d, %u samples",
                         s_dimensions[dimensions[d]], (int)format, count);
                bool succeeds = count == 1 || (count == 4 && Multisamples(format));
                if (count == 2 || count == 4)
                {
                    ExpectUngranted(name, status, succeeds);
                    continue;
                }
                Expect(name, status, succeeds);
            }
        }
    }
}

// The CTS's texture usages as the library's, in the CTS's order.
static const mrhiTextureUsage s_ctsUsages[] = {
    mrhi_textureCopySource, mrhi_textureCopyDestination, mrhi_textureSampled,
    mrhi_textureStorage,    mrhi_textureRenderTarget,    mrhi_textureTransient,
};

// The CTS's kValidCombinationsOfOneOrTwoTextureUsages: each usage, and
// each pair, a transient one only with a render target.
static uint32_t UsageCombinations(mrhiTextureUsage combinations[16])
{
    const mrhiTextureUsage* usages = s_ctsUsages;
    uint32_t count = 0;
    for (uint32_t i = 0; i < 6; ++i)
    {
        for (uint32_t j = i; j < 6; ++j)
        {
            mrhiTextureUsage usage = usages[i] | usages[j];
            bool transient = (usage & mrhi_textureTransient) != 0;
            if (!transient || usage == (mrhi_textureTransient | mrhi_textureRenderTarget))
            {
                combinations[count++] = usage;
            }
        }
    }
    return count;
}

// A case of sampleCount,valid_sampleCount_with_other_parameter_varies.
typedef struct SampleCase
{
    Dimension dimension;
    mrhiFormat format;
    uint32_t samples;
    uint32_t layers;
    uint32_t mips;
    mrhiTextureUsage usage;
} SampleCase;

// Whether the CTS runs the case: its filters, and its skip of a render
// target the device cannot render to.
static bool RunsSampleCase(const SampleCase* c)
{
    bool render = (c->usage & mrhi_textureRenderTarget) != 0;
    bool storage = (c->usage & mrhi_textureStorage) != 0;
    bool transient = (c->usage & mrhi_textureTransient) != 0;
    return Runs(c->dimension, c->format) &&
           (c->layers == 1 || c->dimension == DIMENSION_DEFAULT || c->dimension == DIMENSION_2D) &&
           (!render || (IsColor(c->format) && c->dimension == DIMENSION_2D)) &&
           (!storage || PossiblyStores(c->format)) &&
           (c->mips == 1 || c->dimension != DIMENSION_1D) &&
           (!transient || (c->mips == 1 && c->layers == 1)) &&
           (!render || c->format != mrhi_formatRg11b10Ufloat);
}

// The case's texture, 32 blocks wide and high, of its layers or 32
// deep in 3D, against the CTS's outcome. Several samples need a 2D
// render target of one layer and mip without storage, which is
// refused as invalid, and a format that multisamples; one sample with
// storage needs a format that stores. A format's lack is ungranted.
static void ExpectSampleCase(const SampleCase* c)
{
    mrhiExtent3d size = BlockOf(c->format);
    size.width *= 32;
    size.height *= 32;
    size.depthOrLayers = c->dimension == DIMENSION_3D ? 32 : c->layers;
    bool render = (c->usage & mrhi_textureRenderTarget) != 0;
    bool storage = (c->usage & mrhi_textureStorage) != 0;
    bool shaped =
        c->samples == 1 || ((c->dimension == DIMENSION_DEFAULT || c->dimension == DIMENSION_2D) &&
                            c->mips == 1 && c->layers == 1 && render && !storage);
    bool granted = c->samples == 1 ? !storage || Stores(c->format) : Multisamples(c->format);
    char name[192];
    snprintf(name, sizeof(name),
             "createTexture:sampleCount,valid_sampleCount_with_other_parameter_varies %s, "
             "format %d, %u samples, %u layers, %u mips, usage 0x%x",
             s_dimensions[c->dimension], (int)c->format, c->samples, c->layers, c->mips,
             (unsigned)c->usage);
    mrhiResult status = CreateSampled(c->dimension, c->format, size, c->mips, c->samples, c->usage);
    if (!shaped)
    {
        Expect(name, status, false);
        return;
    }
    ExpectUngranted(name, status, granted);
}

// createTexture:sampleCount,valid_sampleCount_with_other_parameter_varies:
// 1 or 4 samples on every format and dimension, with 1 or 2 layers, 1
// or 2 mips and each combination of one or two usages the CTS keeps.
// The 1D cases are counted.
static void TestSampleShapes(void)
{
    mrhiTextureUsage usages[16];
    uint32_t usageCount = UsageCombinations(usages);
    for (Dimension d = DIMENSION_DEFAULT; d <= DIMENSION_3D; ++d)
    {
        for (mrhiFormat format = RGBA8; format <= LAST_FORMAT; ++format)
        {
            for (uint32_t i = 0; i < 2 * 2 * 2 * usageCount; ++i)
            {
                SampleCase c = {.dimension = d,
                                .format = format,
                                .samples = i % 2 == 0 ? 1 : 4,
                                .layers = i / 2 % 2 + 1,
                                .mips = i / 4 % 2 + 1,
                                .usage = usages[i / 8]};
                if (!RunsSampleCase(&c))
                {
                    continue;
                }
                if (d == DIMENSION_1D)
                {
                    ++s_inexpressible;
                    continue;
                }
                ExpectSampleCase(&c);
            }
        }
    }
}

// createTexture:sample_count,1d_2d_array_3d: four samples of rgba8unorm
// only on a 2D texture of one layer. The 1D case is counted.
static void TestSampledKinds(void)
{
    static const struct
    {
        Dimension dimension;
        mrhiExtent3d size;
    } cases[] = {{DIMENSION_2D, {4, 4, 1}},
                 {DIMENSION_1D, {4, 1, 1}},
                 {DIMENSION_2D, {4, 4, 4}},
                 {DIMENSION_2D, {4, 4, 6}},
                 {DIMENSION_3D, {4, 4, 4}}};
    char name[128];
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        if (cases[i].dimension == DIMENSION_1D)
        {
            ++s_inexpressible;
            continue;
        }
        mrhiExtent3d size = cases[i].size;
        snprintf(name, sizeof(name), "createTexture:sample_count,1d_2d_array_3d %s %ux%ux%u",
                 s_dimensions[cases[i].dimension], size.width, size.height, size.depthOrLayers);
        Expect(name,
               CreateSampled(cases[i].dimension, RGBA8, size, 1, 4,
                             mrhi_textureSampled | mrhi_textureRenderTarget),
               i == 0);
    }
}

// createTexture:texture_usage: one block of every format in every
// dimension, with each usage and pair the CTS keeps. Storage needs a
// format that stores and a render target a color format that renders,
// which are ungranted; a transient texture is refused off 2D as
// invalid. The 1D cases are counted.
static void TestTextureUsages(void)
{
    mrhiTextureUsage usages[16];
    uint32_t usageCount = UsageCombinations(usages);
    char name[160];
    for (Dimension d = DIMENSION_DEFAULT; d <= DIMENSION_3D; ++d)
    {
        for (mrhiFormat format = RGBA8; format <= LAST_FORMAT; ++format)
        {
            for (uint32_t u = 0; u < usageCount && Runs(d, format); ++u)
            {
                if (d == DIMENSION_1D)
                {
                    ++s_inexpressible;
                    continue;
                }
                mrhiTextureUsage usage = usages[u];
                bool granted = ((usage & mrhi_textureStorage) == 0 || Stores(format)) &&
                               ((usage & mrhi_textureRenderTarget) == 0 || Renders(format));
                snprintf(name, sizeof(name),
                         "createTexture:texture_usage %s, format %d, usage 0x%x", s_dimensions[d],
                         (int)format, (unsigned)usage);
                mrhiResult status = CreateTexture(d, format, BlockOf(format), 1, usage);
                if ((usage & mrhi_textureTransient) != 0 && d == DIMENSION_3D)
                {
                    Expect(name, status, false);
                    continue;
                }
                ExpectUngranted(name, status, granted);
            }
        }
    }
}

// createTexture:usage and createTexture:new_usages: an rgba8unorm
// texture of one texel with no usage, each usage, a bit no usage has,
// or a pair of them. A usage is valid when it is not empty, has only
// known bits and is transient only as a transient render target. The
// library's bit past its usages stands for the CTS's bogus one.
static void TestUsageBits(void)
{
    mrhiTextureUsage usages[8] = {0};
    for (uint32_t i = 0; i < 6; ++i)
    {
        usages[i + 1] = s_ctsUsages[i];
    }
    usages[7] = 0x40000000u;
    mrhiTextureUsage transient = mrhi_textureTransient | mrhi_textureRenderTarget;
    char name[96];
    for (uint32_t i = 0; i < 8; ++i)
    {
        for (uint32_t j = i; j < 8; ++j)
        {
            mrhiTextureUsage usage = usages[i] | usages[j];
            bool valid = usage != 0 && usage != 0x40000000u && (usage & 0x40000000u) == 0 &&
                         ((usage & mrhi_textureTransient) == 0 || usage == transient);
            snprintf(name, sizeof(name), "createTexture:usage 0x%x", (unsigned)usage);
            Expect(name, CreateTexture(DIMENSION_DEFAULT, RGBA8, (mrhiExtent3d){1, 1, 1}, 1, usage),
                   valid);
        }
    }
    for (uint32_t i = 0; i < 7; ++i)
    {
        mrhiTextureUsage usage = i < 6 ? s_ctsUsages[i] : transient;
        if (usage == mrhi_textureTransient)
        {
            continue;
        }
        snprintf(name, sizeof(name), "createTexture:new_usages 0x%x", (unsigned)usage);
        Expect(name, CreateTexture(DIMENSION_DEFAULT, RGBA8, (mrhiExtent3d){1, 1, 1}, 1, usage),
               true);
    }
}

// The format a WebGPU format names without its -srgb suffix, from the
// CTS's tables: the library's sRGB formats follow their linear ones.
static mrhiFormat Linear(mrhiFormat format)
{
    switch (format)
    {
    case mrhi_formatRgba8UnormSrgb:
    case mrhi_formatBgra8UnormSrgb:
    case mrhi_formatBc1RgbaUnormSrgb:
    case mrhi_formatBc2RgbaUnormSrgb:
    case mrhi_formatBc3RgbaUnormSrgb:
    case mrhi_formatBc7RgbaUnormSrgb:
    case mrhi_formatEtc2Rgb8UnormSrgb:
    case mrhi_formatEtc2Rgb8a1UnormSrgb:
    case mrhi_formatEtc2Rgba8UnormSrgb:
        return format - 1;
    default:
        return format >= mrhi_formatAstc4x4Unorm && (format - mrhi_formatAstc4x4Unorm) % 2 == 1
                   ? format - 1
                   : format;
    }
}

// A sampled texture of one block, listing the view formats: its
// refusal, or success.
static mrhiResult CreateViewed(mrhiFormat format, mrhiFormat first, mrhiFormat second)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    mrhiExtent3d block = BlockOf(format);
    def.format = format;
    def.width = block.width;
    def.height = block.height;
    def.usage = mrhi_textureSampled;
    def.viewFormats[0] = first;
    def.viewFormats[1] = second;
    mrhiTextureId texture = {0};
    mrhiResult status = mrhiCreateTexture(s_device, &def, &texture);
    if (status == mrhi_success)
    {
        CHECK(mrhiDestroyTexture(s_device, texture) == mrhi_success, "the texture");
    }
    return status;
}

// createTexture:viewFormats: every pair of formats, the view format
// listed alone, before the format, and twice. A view format is valid
// when it is the format or differs from it only in sRGB.
static void TestViewFormatLists(void)
{
    char name[128];
    for (mrhiFormat format = RGBA8; format <= LAST_FORMAT; ++format)
    {
        for (mrhiFormat view = RGBA8; view <= LAST_FORMAT; ++view)
        {
            bool compatible = Linear(format) == Linear(view);
            snprintf(name, sizeof(name), "createTexture:viewFormats format %d, view format %d",
                     (int)format, (int)view);
            Expect(name, CreateViewed(format, view, mrhi_formatNone), compatible);
            Expect(name, CreateViewed(format, view, format), compatible);
            Expect(name, CreateViewed(format, view, view), compatible);
        }
    }
}

// The CTS's makeValueTestVariant for one of a size's three values: the
// limit times mult, plus add, in blocks of the format's width or height
// when the variant says so.
typedef struct Variant
{
    uint32_t mult;
    int32_t add;
    uint32_t inBlock;
} Variant;

#define IN_WIDTH  1u
#define IN_HEIGHT 2u

static uint32_t Varied(uint32_t limit, Variant variant, mrhiExtent3d block)
{
    int32_t unit = variant.inBlock == IN_WIDTH    ? (int32_t)block.width
                   : variant.inBlock == IN_HEIGHT ? (int32_t)block.height
                                                  : 1;
    return (uint32_t)((int64_t)limit * variant.mult + (int64_t)variant.add * unit);
}

// A limit rounded down to whole blocks.
static uint32_t RoundDown(uint32_t limit, uint32_t block)
{
    return limit / block * block;
}

// The uncompressed size variants: each value one under, at and one past
// its limit, the others 1.
static const Variant s_sizeVariants[9][3] = {
    {{1, -1, 0}, {0, 1, 0}, {0, 1, 0}}, {{1, 0, 0}, {0, 1, 0}, {0, 1, 0}},
    {{1, 1, 0}, {0, 1, 0}, {0, 1, 0}},  {{0, 1, 0}, {1, -1, 0}, {0, 1, 0}},
    {{0, 1, 0}, {1, 0, 0}, {0, 1, 0}},  {{0, 1, 0}, {1, 1, 0}, {0, 1, 0}},
    {{0, 1, 0}, {0, 1, 0}, {1, -1, 0}}, {{0, 1, 0}, {0, 1, 0}, {1, 0, 0}},
    {{0, 1, 0}, {0, 1, 0}, {1, 1, 0}},
};

// The compressed 2D size variants: each value a texel or a block under,
// at and past its limit in whole blocks, the others 1 or a block.
static const Variant s_blockVariants[28][3] = {
    {{1, -1, 0}, {0, 1, 0}, {0, 1, 0}},
    {{1, -1, IN_WIDTH}, {0, 1, 0}, {0, 1, 0}},
    {{1, -1, IN_WIDTH}, {0, 1, IN_HEIGHT}, {0, 1, 0}},
    {{1, 0, 0}, {0, 1, 0}, {0, 1, 0}},
    {{1, 0, 0}, {0, 1, IN_HEIGHT}, {0, 1, 0}},
    {{1, 1, 0}, {0, 1, 0}, {0, 1, 0}},
    {{1, 1, IN_WIDTH}, {0, 1, 0}, {0, 1, 0}},
    {{1, 1, IN_WIDTH}, {0, 1, IN_HEIGHT}, {0, 1, 0}},
    {{0, 1, 0}, {1, -1, 0}, {0, 1, 0}},
    {{0, 1, 0}, {1, -1, IN_HEIGHT}, {0, 1, 0}},
    {{0, 1, IN_WIDTH}, {1, -1, IN_HEIGHT}, {0, 1, 0}},
    {{0, 1, 0}, {1, 0, 0}, {0, 1, 0}},
    {{0, 1, IN_WIDTH}, {1, 0, 0}, {0, 1, 0}},
    {{0, 1, 0}, {1, 1, 0}, {0, 1, 0}},
    {{0, 1, 0}, {1, 1, IN_WIDTH}, {0, 1, 0}},
    {{0, 1, IN_WIDTH}, {1, 1, IN_HEIGHT}, {0, 1, 0}},
    {{0, 1, 0}, {0, 1, 0}, {1, -1, 0}},
    {{0, 1, IN_WIDTH}, {0, 1, 0}, {1, -1, 0}},
    {{0, 1, 0}, {0, 1, IN_HEIGHT}, {1, -1, 0}},
    {{0, 1, IN_WIDTH}, {0, 1, IN_HEIGHT}, {1, -1, 0}},
    {{0, 1, 0}, {0, 1, 0}, {1, 0, 0}},
    {{0, 1, IN_WIDTH}, {0, 1, 0}, {1, 0, 0}},
    {{0, 1, 0}, {0, 1, IN_HEIGHT}, {1, 0, 0}},
    {{0, 1, IN_WIDTH}, {0, 1, IN_HEIGHT}, {1, 0, 0}},
    {{0, 1, 0}, {0, 1, 0}, {1, 1, 0}},
    {{0, 1, IN_WIDTH}, {0, 1, 0}, {1, 1, 0}},
    {{0, 1, 0}, {0, 1, IN_HEIGHT}, {1, 1, 0}},
    {{0, 1, IN_WIDTH}, {0, 1, IN_HEIGHT}, {1, 1, 0}},
};

// createTexture:texture_size,default_value_and_smallest_size: one texel
// of every uncompressed format is valid, and of a compressed one only a
// whole block, with the height and layers left at 1 or given. The 1D
// cases are counted.
static void TestSmallestSizes(void)
{
    char name[160];
    for (Dimension d = DIMENSION_DEFAULT; d <= DIMENSION_3D; ++d)
    {
        for (mrhiFormat format = RGBA8; format <= LAST_FORMAT; ++format)
        {
            mrhiExtent3d block = BlockOf(format);
            bool compressed = format >= mrhi_formatBc1RgbaUnorm;
            // The CTS's sizes [1], [width], [1, 1], [width, height],
            // [1, 1, 1] and [width, height, 1], in blocks; the first
            // three of an uncompressed format.
            const mrhiExtent3d sizes[] = {
                {1, 1, 1}, {block.width, 1, 1}, {1, 1, 1}, block, {1, 1, 1}, block,
            };
            for (uint32_t i = 0; i < (compressed ? 6u : 3u) && Runs(d, format); ++i)
            {
                if (d == DIMENSION_1D)
                {
                    ++s_inexpressible;
                    continue;
                }
                mrhiExtent3d size = sizes[i];
                snprintf(name, sizeof(name),
                         "createTexture:texture_size,default_value_and_smallest_size %s, format "
                         "%d, %ux%u",
                         s_dimensions[d], (int)format, size.width, size.height);
                Expect(name, CreateTexture(d, format, size, 1, mrhi_textureSampled),
                       !compressed || i == 3 || i == 5);
            }
        }
    }
}

// A case of texture_size at a limit: success within the device's
// limits, mrhi_errorCapacity past one, and a validation error for a
// size out of whole blocks first.
static void ExpectSize(const char* name, Dimension dimension, mrhiFormat format, mrhiExtent3d size,
                       const mrhiLimits* limits)
{
    mrhiExtent3d block = BlockOf(format);
    bool aligned = size.width % block.width == 0 && size.height % block.height == 0;
    bool within = dimension == DIMENSION_3D ? size.width <= limits->textureDimension3d &&
                                                  size.height <= limits->textureDimension3d &&
                                                  size.depthOrLayers <= limits->textureDimension3d
                                            : size.width <= limits->textureDimension2d &&
                                                  size.height <= limits->textureDimension2d &&
                                                  size.depthOrLayers <= limits->textureArrayLayers;
    mrhiResult status = CreateTexture(dimension, format, size, 1, mrhi_textureSampled);
    if (!aligned)
    {
        Expect(name, status, false);
        return;
    }
    ExpectPastLimit(name, status, within);
}

// createTexture:texture_size,1d_texture, 2d_texture and 3d_texture:
// each of a size's values one under, at and past its limit, in whole
// blocks for a compressed format and a texel or a block off them. The
// 1D sizes are counted, and a compressed 3D texture is not run.
static void TestLimitSizes(void)
{
    mrhiLimits limits = {0};
    CHECK(mrhiDevice_GetLimits(s_device, &limits) == mrhi_success, "the limits");
    // The 1D test's regular formats, three widths, two heights and two
    // layer counts.
    s_inexpressible += (uint32_t)(mrhi_formatDepth32Float - RGBA8) * 3 * 2 * 2;
    char name[192];
    for (mrhiFormat format = RGBA8; format <= LAST_FORMAT; ++format)
    {
        mrhiExtent3d block = BlockOf(format);
        bool compressed = format >= mrhi_formatBc1RgbaUnorm;
        const uint32_t base2d[3] = {RoundDown(limits.textureDimension2d, block.width),
                                    RoundDown(limits.textureDimension2d, block.height),
                                    limits.textureArrayLayers};
        const Variant(*variants)[3] = compressed ? s_blockVariants : s_sizeVariants;
        uint32_t variantCount = compressed ? 28 : 9;
        for (uint32_t d = 0; d < 2; ++d)
        {
            Dimension dimension = d == 0 ? DIMENSION_DEFAULT : DIMENSION_2D;
            for (uint32_t v = 0; v < variantCount; ++v)
            {
                mrhiExtent3d size = {Varied(base2d[0], variants[v][0], block),
                                     Varied(base2d[1], variants[v][1], block),
                                     Varied(base2d[2], variants[v][2], block)};
                snprintf(name, sizeof(name),
                         "createTexture:texture_size,2d_texture %s, format %d, %ux%ux%u",
                         s_dimensions[dimension], (int)format, size.width, size.height,
                         size.depthOrLayers);
                ExpectSize(name, dimension, format, size, &limits);
            }
        }
        for (uint32_t v = 0; v < 9 && Runs(DIMENSION_3D, format); ++v)
        {
            uint32_t most = limits.textureDimension3d;
            mrhiExtent3d size = {Varied(most, s_sizeVariants[v][0], block),
                                 Varied(most, s_sizeVariants[v][1], block),
                                 Varied(most, s_sizeVariants[v][2], block)};
            snprintf(name, sizeof(name),
                     "createTexture:texture_size,3d_texture format %d, %ux%ux%u", (int)format,
                     size.width, size.height, size.depthOrLayers);
            ExpectSize(name, DIMENSION_3D, format, size, &limits);
        }
    }
}

// The CTS's view dimensions, and an absent one.
typedef enum ViewDimension
{
    VIEW_ABSENT,
    VIEW_1D,
    VIEW_2D,
    VIEW_2D_ARRAY,
    VIEW_CUBE,
    VIEW_CUBE_ARRAY,
    VIEW_3D,
} ViewDimension;

static const char* const s_viewDimensions[] = {"absent", "1d",         "2d", "2d-array",
                                               "cube",   "cube-array", "3d"};

// A CTS texture as the library's, made: a 2D one of square layers in
// sixes is a cube array, which takes every view a WebGPU 2D texture
// takes, and any other of several layers a 2D array.
static mrhiTextureId MakeViewed(Dimension dimension, mrhiFormat format, mrhiExtent3d size,
                                uint32_t mips, mrhiTextureUsage usage, mrhiFormat viewFormat)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    bool cubes = size.width == size.height && size.depthOrLayers % 6 == 0;
    def.kind = dimension == DIMENSION_3D ? mrhi_texture3d
               : size.depthOrLayers == 1 ? mrhi_texture2d
               : cubes                   ? mrhi_textureCubeArray
                                         : mrhi_texture2dArray;
    def.format = format;
    def.width = size.width;
    def.height = size.height;
    def.depthOrLayers = size.depthOrLayers;
    def.mipLevels = mips;
    def.usage = usage;
    def.viewFormats[0] = viewFormat;
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(s_device, &def, &texture) == mrhi_success, "the viewed texture");
    return texture;
}

// A view of the texture: its refusal, or success, the view destroyed.
static mrhiResult MakeView(const mrhiViewDef* def)
{
    mrhiViewId view = {0};
    mrhiResult status = mrhiCreateView(s_device, def, &view);
    if (status == mrhi_success)
    {
        CHECK(mrhiDestroyView(s_device, view) == mrhi_success, "the view");
    }
    return status;
}

// createView:format: a texture of every format seen as every format or
// none, with the view format listed or not; a format that differs only
// in sRGB is listed only where it may be. Valid without a view format,
// with the texture's own, or with a listed one.
static void TestViewFormats(void)
{
    char name[160];
    for (mrhiFormat format = RGBA8; format <= LAST_FORMAT; ++format)
    {
        for (mrhiFormat view = mrhi_formatNone; view <= LAST_FORMAT; ++view)
        {
            for (uint32_t listed = 0; listed < 2; ++listed)
            {
                bool compatible = view == mrhi_formatNone || Linear(format) == Linear(view);
                mrhiFormat list = listed != 0 && compatible ? view : mrhi_formatNone;
                mrhiTextureId texture =
                    MakeViewed(DIMENSION_2D, format, BlockOf(format), 1, mrhi_textureSampled, list);
                mrhiViewDef def = mrhiDefaultViewDef();
                def.texture = texture;
                def.format = view;
                snprintf(name, sizeof(name), "createView:format format %d, view format %d, %s",
                         (int)format, (int)view, listed != 0 ? "listed" : "not listed");
                Expect(name, MakeView(&def),
                       view == mrhi_formatNone || view == format || (compatible && listed != 0));
                CHECK(mrhiDestroyTexture(s_device, texture) == mrhi_success, "the texture");
            }
        }
    }
}

// createView:aspect: a texture of every format seen with each aspect:
// all on any, depth only on a depth format and stencil only on a
// stencil one.
static void TestViewAspects(void)
{
    char name[128];
    for (mrhiFormat format = RGBA8; format <= LAST_FORMAT; ++format)
    {
        mrhiTextureId texture =
            MakeViewed(DIMENSION_2D, format, BlockOf(format), 1, mrhi_textureSampled, 0);
        for (mrhiTextureAspect aspect = mrhi_aspectAll; aspect <= mrhi_aspectStencilOnly; ++aspect)
        {
            mrhiViewDef def = mrhiDefaultViewDef();
            def.texture = texture;
            def.aspect = aspect;
            snprintf(name, sizeof(name), "createView:aspect format %d, aspect %d", (int)format,
                     (int)aspect);
            bool depth = format == mrhi_formatDepth32Float || format == DEPTH;
            Expect(name, MakeView(&def),
                   aspect == mrhi_aspectAll || (aspect == mrhi_aspectDepthOnly && depth) ||
                       (aspect == mrhi_aspectStencilOnly && format == DEPTH));
        }
        CHECK(mrhiDestroyTexture(s_device, texture) == mrhi_success, "the texture");
    }
}

// A CTS view of layers and mips: its dimension and its first layer,
// layer count, first mip and mip count, each of them ABSENT or given.
typedef struct ViewRange
{
    ViewDimension dimension;
    int64_t baseLayer;
    int64_t layerCount;
    int64_t baseMip;
    int64_t mipCount;
} ViewRange;

// The view's dimension once resolved, as reifyTextureViewDescriptor
// does: an absent one is the texture's, a 2D array for a 2D texture of
// several layers.
static ViewDimension Resolved(Dimension texture, uint32_t layers, ViewDimension view)
{
    if (view != VIEW_ABSENT)
    {
        return view;
    }
    return texture == DIMENSION_1D   ? VIEW_1D
           : texture == DIMENSION_3D ? VIEW_3D
           : layers > 1              ? VIEW_2D_ARRAY
                                     : VIEW_2D;
}

// The CTS's validateCreateViewLayersLevels, that the view's dimension
// fits the texture's, and that a cube is square: counts resolved as the
// CTS does, at least one mip and layer within the texture's, one layer
// for 2D and 3D, six for a cube and a multiple of six for a cube array.
static bool IsViewValid(Dimension texture, mrhiExtent3d size, uint32_t levels, ViewRange range)
{
    ViewDimension dimension = Resolved(texture, size.depthOrLayers, range.dimension);
    int64_t layers = texture == DIMENSION_3D ? 1 : size.depthOrLayers;
    int64_t baseMip = range.baseMip == ABSENT ? 0 : range.baseMip;
    int64_t mips = range.mipCount == ABSENT ? levels - baseMip : range.mipCount;
    int64_t baseLayer = range.baseLayer == ABSENT ? 0 : range.baseLayer;
    int64_t count = range.layerCount;
    if (count == ABSENT)
    {
        bool arrays = dimension == VIEW_2D_ARRAY || dimension == VIEW_CUBE_ARRAY;
        count = arrays ? size.depthOrLayers - baseLayer : dimension == VIEW_CUBE ? 6 : 1;
    }
    bool fits = texture == DIMENSION_3D ? dimension == VIEW_3D
                                        : dimension != VIEW_3D && dimension != VIEW_1D;
    bool cube = dimension == VIEW_CUBE || dimension == VIEW_CUBE_ARRAY;
    fits = fits && (!cube || size.width == size.height);
    bool shaped = dimension == VIEW_CUBE         ? count == 6
                  : dimension == VIEW_CUBE_ARRAY ? count % 6 == 0
                  : dimension == VIEW_2D_ARRAY   ? true
                                                 : count == 1;
    return fits && shaped && mips > 0 && baseMip < levels && baseMip + mips <= levels &&
           count > 0 && baseLayer < layers && baseLayer + count <= layers;
}

// The case as a library view of the texture: its resolved kind, a
// first layer or mip of 0 where absent, and an absent count as
// MRHI_REMAINING, or the count the CTS resolves for 2D, 3D and a cube.
static mrhiResult MakeRangeView(mrhiTextureId texture, Dimension dimension, uint32_t layers,
                                ViewRange range)
{
    static const mrhiTextureKind kinds[] = {
        mrhi_texture2d,   mrhi_texture2d,        mrhi_texture2d, mrhi_texture2dArray,
        mrhi_textureCube, mrhi_textureCubeArray, mrhi_texture3d,
    };
    ViewDimension resolved = Resolved(dimension, layers, range.dimension);
    mrhiViewDef def = mrhiDefaultViewDef();
    def.texture = texture;
    def.kind = kinds[resolved];
    def.baseLayer = range.baseLayer == ABSENT ? 0 : (uint32_t)range.baseLayer;
    def.baseMip = range.baseMip == ABSENT ? 0 : (uint32_t)range.baseMip;
    def.mipCount = range.mipCount == ABSENT ? MRHI_REMAINING : (uint32_t)range.mipCount;
    bool arrays = resolved == VIEW_2D_ARRAY || resolved == VIEW_CUBE_ARRAY;
    def.layerCount = range.layerCount != ABSENT ? (uint32_t)range.layerCount
                     : arrays                   ? MRHI_REMAINING
                     : resolved == VIEW_CUBE    ? 6
                                                : 1;
    return MakeView(&def);
}

// The view dimensions the CTS gives a texture dimension, an absent one
// first: 1D only for 1D and 3D only for 3D.
static uint32_t ViewDimensionsOf(Dimension dimension, ViewDimension out[5])
{
    out[0] = VIEW_ABSENT;
    if (dimension != DIMENSION_2D)
    {
        out[1] = dimension == DIMENSION_1D ? VIEW_1D : VIEW_3D;
        return 2;
    }
    out[1] = VIEW_2D;
    out[2] = VIEW_2D_ARRAY;
    out[3] = VIEW_CUBE;
    out[4] = VIEW_CUBE_ARRAY;
    return 5;
}

// The CTS's set of first values: absent, 0, 1, 5, 6, 7 and around the
// total, each once.
static uint32_t Firsts(int64_t total, int64_t out[9])
{
    const int64_t candidates[] = {ABSENT, 0, 1, 5, 6, 7, total - 1, total, total + 1};
    uint32_t count = 0;
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
    {
        bool seen = false;
        for (uint32_t j = 0; j < count; ++j)
        {
            seen = seen || out[j] == candidates[i];
        }
        if (!seen)
        {
            out[count++] = candidates[i];
        }
    }
    return count;
}

// The CTS's counts after a first value: absent, and each count ending
// at one of the first values but absent, from the first on.
static uint32_t Counts(int64_t total, int64_t first, int64_t out[10])
{
    int64_t lasts[9];
    uint32_t lastCount = Firsts(total, lasts);
    int64_t base = first == ABSENT ? 0 : first;
    uint32_t count = 0;
    out[count++] = ABSENT;
    for (uint32_t i = 1; i < lastCount; ++i)
    {
        if (base <= lasts[i])
        {
            out[count++] = lasts[i] - base;
        }
    }
    return count;
}

// One range case: the view of a texture made for it, against the
// CTS's outcome, or counted when the texture or view is 1D.
static void ExpectRange(const char* test, Dimension dimension, mrhiExtent3d size, uint32_t levels,
                        ViewRange range)
{
    if (dimension == DIMENSION_1D)
    {
        ++s_inexpressible;
        return;
    }
    char name[192];
    snprintf(name, sizeof(name),
             "createView:%s %s %ux%ux%u of %u mips, view %s, layers %lld+%lld, mips %lld+%lld",
             test, s_dimensions[dimension], size.width, size.height, size.depthOrLayers, levels,
             s_viewDimensions[range.dimension], (long long)range.baseLayer,
             (long long)range.layerCount, (long long)range.baseMip, (long long)range.mipCount);
    mrhiTextureId texture =
        MakeViewed(dimension, RGBA8, size, levels, mrhi_textureSampled, mrhi_formatNone);
    Expect(name, MakeRangeView(texture, dimension, size.depthOrLayers, range),
           IsViewValid(dimension, size, levels, range));
    CHECK(mrhiDestroyTexture(s_device, texture) == mrhi_success, "the texture");
}

// createView:array_layers: an rgba8unorm texture 32 texels wide in each
// dimension, 2D of 1, 6 or 18 layers, of 1 or 6 mips (1D only 1), seen
// with each view dimension it takes from each first layer and count
// the CTS tries.
static void TestViewLayers(void)
{
    for (Dimension d = DIMENSION_1D; d <= DIMENSION_3D; ++d)
    {
        ViewDimension views[5];
        uint32_t viewCount = ViewDimensionsOf(d, views);
        static const uint32_t layerSets[] = {1, 6, 18};
        for (uint32_t l = 0; l < (d == DIMENSION_2D ? 3u : 1u); ++l)
        {
            mrhiExtent3d size = {32, d == DIMENSION_1D ? 1u : 32u,
                                 d == DIMENSION_3D ? 32u : layerSets[l]};
            for (uint32_t levels = 1; levels <= 6; levels += d == DIMENSION_1D ? 6 : 5)
            {
                int64_t firsts[9];
                uint32_t firstCount = Firsts(layerSets[l], firsts);
                for (uint32_t v = 0; v < viewCount; ++v)
                {
                    for (uint32_t f = 0; f < firstCount; ++f)
                    {
                        int64_t counts[10];
                        uint32_t countCount = Counts(layerSets[l], firsts[f], counts);
                        for (uint32_t c = 0; c < countCount; ++c)
                        {
                            ViewRange range = {views[v], firsts[f], counts[c], ABSENT, ABSENT};
                            ExpectRange("array_layers", d, size, levels, range);
                        }
                    }
                }
            }
        }
    }
}

// createView:mip_levels: an rgba8unorm texture of 1, 4 or 6 mips (1D
// only 1), 32 texels in 1D, 32x32x32 in 3D and 32x32 of 18 layers in
// 2D, seen with each view dimension it takes from each first mip and
// count the CTS tries.
static void TestViewMips(void)
{
    static const uint32_t levelSets[] = {1, 4, 6};
    for (Dimension d = DIMENSION_1D; d <= DIMENSION_3D; ++d)
    {
        ViewDimension views[5];
        uint32_t viewCount = ViewDimensionsOf(d, views);
        mrhiExtent3d size = d == DIMENSION_1D   ? (mrhiExtent3d){32, 1, 1}
                            : d == DIMENSION_3D ? (mrhiExtent3d){32, 32, 32}
                                                : (mrhiExtent3d){32, 32, 18};
        for (uint32_t l = 0; l < (d == DIMENSION_1D ? 1u : 3u); ++l)
        {
            int64_t firsts[9];
            uint32_t firstCount = Firsts(levelSets[l], firsts);
            for (uint32_t v = 0; v < viewCount; ++v)
            {
                for (uint32_t f = 0; f < firstCount; ++f)
                {
                    int64_t counts[10];
                    uint32_t countCount = Counts(levelSets[l], firsts[f], counts);
                    for (uint32_t c = 0; c < countCount; ++c)
                    {
                        ViewRange range = {views[v], ABSENT, ABSENT, firsts[f], counts[c]};
                        ExpectRange("mip_levels", d, size, levelSets[l], range);
                    }
                }
            }
        }
    }
}

// createView:dimension and createView:cube_faces_square: a 2D texture
// of 4x4 and 6 layers or a 3D one of 4x4x6 seen with each view
// dimension, which must be of the texture's dimension; and textures of
// 6 layers, square or not, seen as 2D, a cube or a cube array, which
// must be square. The 1D textures and views are counted.
static void TestViewDimensionCases(void)
{
    for (Dimension d = DIMENSION_1D; d <= DIMENSION_3D; ++d)
    {
        for (ViewDimension v = VIEW_ABSENT; v <= VIEW_3D; ++v)
        {
            if (v == VIEW_1D)
            {
                ++s_inexpressible;
                continue;
            }
            mrhiExtent3d size = {4, d == DIMENSION_1D ? 1u : 4u, d == DIMENSION_1D ? 1u : 6u};
            ExpectRange("dimension", d, size, 1, (ViewRange){v, ABSENT, ABSENT, ABSENT, ABSENT});
        }
    }
    static const mrhiExtent3d sizes[] = {{4, 4, 6}, {5, 5, 6}, {4, 5, 6}, {4, 8, 6}, {8, 4, 6}};
    static const ViewDimension views[] = {VIEW_2D, VIEW_CUBE, VIEW_CUBE_ARRAY};
    for (size_t v = 0; v < 3; ++v)
    {
        for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
        {
            bool square = sizes[i].width == sizes[i].height;
            ViewRange range = {views[v], ABSENT, ABSENT, ABSENT, ABSENT};
            CHECK(IsViewValid(DIMENSION_2D, sizes[i], 1, range) == (v == 0 || square),
                  "createView:cube_faces_square's outcome");
            ExpectRange("cube_faces_square", DIMENSION_2D, sizes[i], 1, range);
        }
    }
}

// createView:texture_state: a view of a valid texture is valid, and of
// an invalid one refused. The library makes no invalid texture: a
// refused def gives the zero id, which is stale. A destroyed texture's
// id is stale too, where WebGPU still views a destroyed texture, so
// that case differs.
static void TestViewStates(void)
{
    mrhiTextureId texture =
        MakeViewed(DIMENSION_2D, RGBA8, (mrhiExtent3d){1, 1, 1}, 1, mrhi_textureSampled, 0);
    mrhiViewDef def = mrhiDefaultViewDef();
    def.texture = texture;
    Expect("createView:texture_state valid", MakeView(&def), true);
    CHECK(mrhiDestroyTexture(s_device, texture) == mrhi_success, "the texture");
    CHECK(MakeView(&def) == mrhi_errorStale, "createView:texture_state destroyed, stale");
    s_differs += 1;
    mrhiTextureDef invalid = mrhiDefaultTextureDef();
    invalid.format = RGBA8;
    mrhiTextureId none = {0};
    CHECK(mrhiCreateTexture(s_device, &invalid, &none) == mrhi_errorInvalid && none.index1 == 0,
          "an invalid texture");
    def.texture = none;
    CHECK(MakeView(&def) == mrhi_errorStale, "createView:texture_state invalid, stale");
}

// Whether a format takes a single usage on a device without optional
// features, which the CTS's skips leave: storage, rendering, and the
// others any format takes.
static bool Takes(mrhiFormat format, mrhiTextureUsage usage)
{
    return usage == mrhi_textureStorage        ? Stores(format)
           : usage == mrhi_textureRenderTarget ? Renders(format)
                                               : true;
}

// createView:texture_view_usage: a texture of every format with each
// usage it takes, seen with each usage: valid when the view's usage is
// the texture's. Transient usages are left out, as the CTS does.
static void TestViewUsageSubsets(void)
{
    char name[128];
    for (mrhiFormat format = RGBA8; format <= LAST_FORMAT; ++format)
    {
        for (uint32_t t = 0; t < 5; ++t)
        {
            mrhiTextureUsage usage = s_ctsUsages[t];
            if (!Takes(format, usage))
            {
                continue;
            }
            mrhiTextureId texture = MakeViewed(DIMENSION_2D, format, BlockOf(format), 1, usage, 0);
            for (uint32_t v = 0; v < 5; ++v)
            {
                mrhiViewDef def = mrhiDefaultViewDef();
                def.texture = texture;
                def.usage = s_ctsUsages[v];
                snprintf(name, sizeof(name),
                         "createView:texture_view_usage format %d, usage 0x%x, view usage 0x%x",
                         (int)format, (unsigned)usage, (unsigned)def.usage);
                Expect(name, MakeView(&def), v == t);
            }
            CHECK(mrhiDestroyTexture(s_device, texture) == mrhi_success, "the texture");
        }
    }
}

// createView:texture_view_usage_with_view_format: a texture of every
// format with each usage it takes, listing a view format that differs
// at most in sRGB, seen as that format with the usage given and
// inherited. Valid when the view format takes the usage too, else
// ungranted: rgba8unorm-srgb stores nothing.
static void TestViewUsageFormats(void)
{
    char name[160];
    for (mrhiFormat format = RGBA8; format <= LAST_FORMAT; ++format)
    {
        for (mrhiFormat view = RGBA8; view <= LAST_FORMAT; ++view)
        {
            for (uint32_t u = 0; u < 5 && Linear(format) == Linear(view); ++u)
            {
                mrhiTextureUsage usage = s_ctsUsages[u];
                if (!Takes(format, usage))
                {
                    continue;
                }
                mrhiTextureId texture =
                    MakeViewed(DIMENSION_2D, format, BlockOf(format), 1, usage, view);
                for (uint32_t inherited = 0; inherited < 2; ++inherited)
                {
                    mrhiViewDef def = mrhiDefaultViewDef();
                    def.texture = texture;
                    def.format = view;
                    def.usage = inherited != 0 ? 0 : usage;
                    snprintf(name, sizeof(name),
                             "createView:texture_view_usage_with_view_format format %d, view "
                             "format %d, usage 0x%x%s",
                             (int)format, (int)view, (unsigned)usage,
                             inherited != 0 ? ", inherited" : "");
                    ExpectUngranted(name, MakeView(&def), Takes(view, usage));
                }
                CHECK(mrhiDestroyTexture(s_device, texture) == mrhi_success, "the texture");
            }
        }
    }
}

// A sampler of the def: its refusal, or success, the sampler destroyed.
static mrhiResult MakeSampler(const mrhiSamplerDef* def)
{
    mrhiSamplerId sampler = {0};
    mrhiResult status = mrhiCreateSampler(s_device, def, &sampler);
    if (status == mrhi_success)
    {
        CHECK(mrhiDestroySampler(s_device, sampler) == mrhi_success, "the sampler");
    }
    return status;
}

// createSampler:lodMinAndMaxClamp and createSampler:maxAnisotropy: each
// pair of clamps, valid when neither is below 0 nor the first above the
// second; and anisotropy from -1 to 1024 with linear filters, then 1
// with each filter nearest in turn, valid from 1 and, above 1, with
// every filter linear. WebIDL's [Clamp] makes the CTS's -1 a 0, and its
// absent value is 1.
static void TestSamplers(void)
{
    static const float clamps[] = {-4e-30f, -1.0f, 0.0f, 0.5f, 1.0f, 10.0f, 4e30f};
    char name[128];
    for (size_t i = 0; i < 7; ++i)
    {
        for (size_t j = 0; j < 7; ++j)
        {
            mrhiSamplerDef def = mrhiDefaultSamplerDef();
            def.lodMin = clamps[i];
            def.lodMax = clamps[j];
            snprintf(name, sizeof(name), "createSampler:lodMinAndMaxClamp %g, %g",
                     (double)clamps[i], (double)clamps[j]);
            Expect(name, MakeSampler(&def), clamps[i] >= 0.0f && clamps[j] >= clamps[i]);
        }
    }
    static const uint16_t anisotropies[] = {0, 1, 0, 1, 2, 4, 7, 16, 32, 33, 1024};
    for (size_t i = 0; i < sizeof(anisotropies) / sizeof(anisotropies[0]) + 3; ++i)
    {
        size_t nearest = i < 11 ? 3 : i - 11;
        mrhiSamplerDef def = mrhiDefaultSamplerDef();
        def.minFilter = nearest == 0 ? mrhi_filterNearest : mrhi_filterLinear;
        def.magFilter = nearest == 1 ? mrhi_filterNearest : mrhi_filterLinear;
        def.mipFilter = nearest == 2 ? mrhi_filterNearest : mrhi_filterLinear;
        def.maxAnisotropy = i < 11 ? anisotropies[i] : 1;
        snprintf(name, sizeof(name), "createSampler:maxAnisotropy %u, nearest filter %zu",
                 (unsigned)def.maxAnisotropy, nearest);
        Expect(name, MakeSampler(&def), def.maxAnisotropy >= 1);
    }
}

// A buffer of the size and usages: its refusal, or success, the buffer
// destroyed.
static mrhiResult TryBuffer(uint64_t size, mrhiBufferUsage usage)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = usage;
    mrhiBufferId buffer = {0};
    mrhiResult status = mrhiCreateBuffer(s_device, &def, &buffer);
    if (status == mrhi_success)
    {
        CHECK(mrhiDestroyBuffer(s_device, buffer) == mrhi_success, "the buffer");
    }
    return status;
}

// The CTS's buffer usages, in its order, as the library's: MAP_READ and
// MAP_WRITE have none, as the library maps no buffer.
static const mrhiBufferUsage s_ctsBufferUsages[] = {
    0,
    0,
    mrhi_bufferCopySource,
    mrhi_bufferCopyDestination,
    mrhi_bufferIndex,
    mrhi_bufferVertex,
    mrhi_bufferUniform,
    mrhi_bufferStorage,
    mrhi_bufferIndirect,
    mrhi_bufferQueryResolve,
};

// createBuffer: size, limit, usage, new_usages and
// createBuffer_invalid_and_oom. A buffer takes any size up to the
// device's limit, past which it is mrhi_errorCapacity, and any usages
// but none or an unknown bit. The library maps no buffer, so a case
// mapped at creation or with a map usage is counted.
static void TestBuffers(void)
{
    char name[96];
    for (uint64_t size = 0; size <= 8; size += 2)
    {
        snprintf(name, sizeof(name), "createBuffer:size %llu", (unsigned long long)size);
        Expect(name, TryBuffer(size, mrhi_bufferCopySource), true);
        ++s_inexpressible;
    }
    mrhiLimits limits = {0};
    CHECK(mrhiDevice_GetLimits(s_device, &limits) == mrhi_success, "the limits");
    for (uint64_t size = limits.bufferBytes - 1; size <= limits.bufferBytes + 1; ++size)
    {
        snprintf(name, sizeof(name), "createBuffer:limit %llu", (unsigned long long)size);
        ExpectPastLimit(name, TryBuffer(size, mrhi_bufferCopySource), size <= limits.bufferBytes);
    }
    // The CTS's usages with none first and its bogus bit last.
    mrhiBufferUsage usages[12] = {0};
    for (uint32_t i = 0; i < 10; ++i)
    {
        usages[i + 1] = s_ctsBufferUsages[i];
    }
    usages[11] = 0x40000000u;
    for (uint32_t i = 0; i < 12; ++i)
    {
        for (uint32_t j = i; j < 12; ++j)
        {
            // Mapped at creation, and with MAP_READ or MAP_WRITE, counted.
            bool maps = i == 1 || i == 2 || j == 1 || j == 2;
            s_inexpressible += maps ? 2 : 1;
            if (maps)
            {
                continue;
            }
            mrhiBufferUsage usage = usages[i] | usages[j];
            snprintf(name, sizeof(name), "createBuffer:usage 0x%x", (unsigned)usage);
            Expect(name, TryBuffer(8, usage), usage != 0 && (usage & 0x40000000u) == 0);
        }
    }
    for (uint32_t i = 0; i < 10; ++i)
    {
        snprintf(name, sizeof(name), "createBuffer:new_usages 0x%x",
                 (unsigned)s_ctsBufferUsages[i]);
        if (s_ctsBufferUsages[i] == 0)
        {
            ++s_inexpressible;
            continue;
        }
        Expect(name, TryBuffer(16, s_ctsBufferUsages[i]), true);
    }
    Expect("createBuffer:createBuffer_invalid_and_oom uniform", TryBuffer(16, mrhi_bufferUniform),
           true);
    Expect("createBuffer:createBuffer_invalid_and_oom storage", TryBuffer(16, mrhi_bufferStorage),
           true);
    // Its six cases with a map usage.
    s_inexpressible += 6;
}

// The color renderable formats, with WebGPU's render target pixel byte
// cost and component alignment of each.
static const struct
{
    mrhiFormat format;
    uint32_t bytes;
    uint32_t alignment;
} s_renderable[] = {
    {mrhi_formatR8Unorm, 1, 1},      {mrhi_formatRg8Unorm, 2, 1},
    {mrhi_formatRgba8Unorm, 8, 1},   {mrhi_formatRgba8UnormSrgb, 8, 1},
    {mrhi_formatBgra8Unorm, 8, 1},   {mrhi_formatBgra8UnormSrgb, 8, 1},
    {mrhi_formatR16Float, 2, 2},     {mrhi_formatRg16Float, 4, 2},
    {mrhi_formatRgba16Float, 8, 2},  {mrhi_formatR32Float, 4, 4},
    {mrhi_formatRg32Float, 8, 4},    {mrhi_formatRgba32Float, 16, 4},
    {mrhi_formatR32Uint, 4, 4},      {mrhi_formatR32Sint, 4, 4},
    {mrhi_formatRgb10a2Unorm, 8, 4},
};

// The bytes a sample of targets of the formats takes, as WebGPU counts
// them; the formats are indices into s_renderable.
static uint32_t BytesPerSample(const uint32_t* formats, uint32_t count)
{
    uint32_t total = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t alignment = s_renderable[formats[i]].alignment;
        total = (total + alignment - 1) / alignment * alignment + s_renderable[formats[i]].bytes;
    }
    return total;
}

// color_attachments,limits,maxColorAttachmentBytesPerSample: aligned,
// every renderable format from one target to the most; unaligned, two
// orders of one set of formats, whose alignment puts the first past 32
// bytes and leaves the second within them.
static void TestColorBytes(void)
{
    char name[160];
    for (uint32_t f = 0; f < sizeof(s_renderable) / sizeof(s_renderable[0]); ++f)
    {
        Attachment same[MRHI_MAX_COLOR_TARGETS];
        uint32_t indices[MRHI_MAX_COLOR_TARGETS];
        for (uint32_t i = 0; i < MRHI_MAX_COLOR_TARGETS; ++i)
        {
            same[i] = (Attachment){.format = s_renderable[f].format};
            indices[i] = f;
        }
        for (uint32_t count = 1; count <= MRHI_MAX_COLOR_TARGETS; ++count)
        {
            snprintf(name, sizeof(name),
                     "render_pass_descriptor:color_attachments,limits,"
                     "maxColorAttachmentBytesPerSample,aligned format %d, %u",
                     (int)s_renderable[f].format, (unsigned)count);
            ExpectPastLimit(name, RenderPass(same, nullptr, count, (Attachment){0}),
                            BytesPerSample(indices, count) <= 32);
        }
    }
    // r8unorm, r32float, rgba8unorm, rgba32float, then the two r8unorm last.
    static const uint32_t orders[2][5] = {{0, 9, 2, 11, 0}, {9, 2, 11, 0, 0}};
    for (uint32_t o = 0; o < 2; ++o)
    {
        Attachment formats[5];
        for (uint32_t i = 0; i < 5; ++i)
        {
            formats[i] = (Attachment){.format = s_renderable[orders[o][i]].format};
        }
        snprintf(name, sizeof(name),
                 "render_pass_descriptor:color_attachments,limits,"
                 "maxColorAttachmentBytesPerSample,unaligned %s",
                 o == 0 ? "r8unorm first" : "r8unorm last");
        ExpectPastLimit(name, RenderPass(formats, nullptr, 5, (Attachment){0}),
                        BytesPerSample(orders[o], 5) <= 32);
    }
}

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
    // color_attachments,empty: undefined attachments need a defined one
    // or depth. Its case of none at all without depth is a compute pass.
    const Attachment undefinedOr[MRHI_MAX_COLOR_TARGETS] = {0};
    static const uint32_t counts[] = {1, 2, MRHI_MAX_COLOR_TARGETS};
    for (uint32_t withDepth = 0; withDepth < 2; ++withDepth)
    {
        for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i)
        {
            snprintf(name, sizeof(name),
                     "render_pass_descriptor:color_attachments,empty %u undefined, %s",
                     (unsigned)counts[i], withDepth ? "depth" : "no depth");
            Expect(name, RenderPass(undefinedOr, nullptr, counts[i], withDepth ? depth : s_none),
                   withDepth == 1);
        }
        snprintf(name, sizeof(name), "render_pass_descriptor:color_attachments,empty one, %s",
                 withDepth ? "depth" : "no depth");
        Expect(name, RenderPass(&color, nullptr, 1, withDepth ? depth : s_none), true);
    }
    // color_attachments,limits,maxColorAttachments.
    const Attachment many[MRHI_MAX_COLOR_TARGETS + 1] = {r8, r8, r8, r8, r8, r8, r8, r8, r8};
    Expect("render_pass_descriptor:color_attachments,limits,maxColorAttachments 8",
           RenderPass(many, nullptr, MRHI_MAX_COLOR_TARGETS, s_none), true);
    Expect("render_pass_descriptor:color_attachments,limits,maxColorAttachments 9",
           RenderPass(many, nullptr, MRHI_MAX_COLOR_TARGETS + 1, s_none), false);
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
    // resolveTarget,format_supports_resolve: each color renderable format
    // the device multisamples, resolved only if WebGPU resolves it.
    static const struct
    {
        mrhiFormat format;
        bool resolves;
    } multisampled[] = {
        {mrhi_formatR8Unorm, true},
        {mrhi_formatRg8Unorm, true},
        {RGBA8, true},
        {mrhi_formatRgba8UnormSrgb, true},
        {mrhi_formatBgra8Unorm, true},
        {mrhi_formatBgra8UnormSrgb, true},
        {mrhi_formatR16Float, true},
        {mrhi_formatRg16Float, true},
        {mrhi_formatRgba16Float, true},
        {mrhi_formatRgb10a2Unorm, true},
        {mrhi_formatR32Float, false},
    };
    for (size_t i = 0; i < sizeof(multisampled) / sizeof(multisampled[0]); ++i)
    {
        snprintf(name, sizeof(name),
                 "render_pass_descriptor:resolveTarget,format_supports_resolve %d",
                 (int)multisampled[i].format);
        const Attachment samples = {.format = multisampled[i].format, .samples = 4};
        const Attachment into = {.format = multisampled[i].format};
        Expect(name, RenderPass(&samples, &into, 1, s_none), multisampled[i].resolves);
    }
    // depth_stencil_attachment,sample_counts_mismatch.
    Expect("render_pass_descriptor:depth_stencil_attachment,sample_counts_mismatch 4 and 1",
           RenderPass(&multi, nullptr, 1, depth), false);
    Expect("render_pass_descriptor:depth_stencil_attachment,sample_counts_mismatch 1 and 4",
           RenderPass(&color, nullptr, 1, multiDepth), false);
    // depth_stencil_attachment,depth_clear_value: a value from 0 to 1 when
    // the depth is cleared, any when it is loaded. The CTS's value left
    // undefined is NaN; its pass of no depth load has no counterpart, as a
    // depth target has depth.
    const float values[] = {NAN, -1.0f, 0.0f, 0.5f, 1.0f, 1.5f};
    for (uint32_t load = 0; load < 2; ++load)
    {
        for (uint32_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        {
            snprintf(name, sizeof(name),
                     "render_pass_descriptor:depth_stencil_attachment,depth_clear_value %s %g",
                     load == 0 ? "load" : "clear", (double)values[i]);
            Attachment cleared = {.format = DEPTH, .keepDepth = load == 0, .clearDepth = values[i]};
            bool inRange = values[i] >= 0.0f && values[i] <= 1.0f;
            Expect(name, RenderPass(&color, nullptr, 1, cleared), load == 0 || inRange);
        }
    }
}

static mrhiQuerySetId MakeQuerySet(mrhiQueryKind kind, uint32_t count)
{
    mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
    def.kind = kind;
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
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success &&
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
        CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success &&
                  mrhiPass_Begin(s_device, pass) == mrhi_success,
              "the pass begun");
        status = mrhiPass_ResolveQueries(s_device, pass, set, first, count, destination, offset);
    }
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success &&
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
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success &&
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
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success, "compiled");
    mrhiResult status = mrhi_success;
    for (uint32_t i = 0; i < passes && status == mrhi_success; ++i)
    {
        CHECK(mrhiPass_Begin(s_device, ids[i]) == mrhi_success, "the pass begun");
        for (size_t c = 0; c < callCount && status == mrhi_success; ++c)
        {
            status = calls[c] == END ? mrhiPass_EndOcclusionQuery(s_device, ids[i])
                                     : mrhiPass_BeginOcclusionQuery(s_device, ids[i], calls[c]);
        }
        status = status == mrhi_success ? mrhiPass_End(s_device, ids[i]) : status;
    }
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success &&
              mrhiDestroyQuerySet(s_device, set) == mrhi_success,
          "dropped");
    return status;
}

// createQuerySet: count, of the CTS's kQueryTypes, occlusion and
// timestamp, at 0, kMaxQueryCount (4096) and one past it.
static void TestQuerySets(void)
{
    char name[64];
    static const mrhiQueryKind kinds[] = {mrhi_queryOcclusion, mrhi_queryTimestamp};
    static const uint32_t counts[] = {0, 4096, 4097};
    for (size_t k = 0; k < 2; ++k)
    {
        for (size_t c = 0; c < 3; ++c)
        {
            mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
            def.kind = kinds[k];
            def.count = counts[c];
            mrhiQuerySetId set = {0};
            mrhiResult status = mrhiCreateQuerySet(s_device, &def, &set);
            snprintf(name, sizeof(name), "createQuerySet:count %s %u",
                     k == 0 ? "occlusion" : "timestamp", counts[c]);
            Expect(name, status, counts[c] <= 4096);
            CHECK(status != mrhi_success || mrhiDestroyQuerySet(s_device, set) == mrhi_success,
                  "destroyed");
        }
    }
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
    // Vulkan needs.
    static const uint32_t twice[] = {0, END, 0, END};
    Expect("begin_end:occlusion_query,disjoint_queries_with_same_query_index same pass",
           Occlusion(1, twice, 4, 1), false);
    s_differs += 1;
    Expect("begin_end:occlusion_query,disjoint_queries_with_same_query_index other pass",
           Occlusion(1, twice, 2, 2), false);
}

// A destroy the CTS expects to raise no error, of a live object, one
// destroyed already, or one a refused def left. The library's ids are
// generational: destroying an ended one, or the zero id a refused def
// gives, is mrhi_errorStale, which changes nothing and counts no misuse,
// as WebGPU's destroy of a destroyed or invalid object raises nothing.
static void ExpectDestroy(const char* test, mrhiResult result, bool ended, uint64_t misuse)
{
    if (result != (ended ? mrhi_errorStale : mrhi_success) ||
        mrhiDevice_GetMisuse(s_device) != misuse)
    {
        printf("FAIL: %s: %d, the CTS expects no error\n", test, (int)result);
        ++s_failures;
    }
}

// Reports a case the CTS refuses for a destroyed object. Its id has
// ended, so the library refuses it as mrhi_errorStale.
static void ExpectStale(const char* test, mrhiResult result)
{
    if (result != mrhi_errorStale)
    {
        printf("FAIL: %s: %d, the CTS expects a validation error\n", test, (int)result);
        ++s_failures;
    }
}

// buffer,destroy: all_usages, error_buffer, twice and while_mapped. The
// library maps no buffer, so a case with a map usage, mapped at
// creation or mapped is counted; the CTS's error buffer is one, as a
// map usage makes it invalid.
static void TestBufferDestroys(void)
{
    char name[64];
    for (uint32_t i = 0; i < 10; ++i)
    {
        if (s_ctsBufferUsages[i] == 0)
        {
            ++s_inexpressible;
            continue;
        }
        mrhiBufferId buffer = MakeBuffer(4, s_ctsBufferUsages[i]);
        snprintf(name, sizeof(name), "buffer,destroy:all_usages 0x%x",
                 (unsigned)s_ctsBufferUsages[i]);
        ExpectDestroy(name, mrhiDestroyBuffer(s_device, buffer), false,
                      mrhiDevice_GetMisuse(s_device));
    }
    s_inexpressible += 1;
    // twice: not mapped at creation, of COPY_SRC; the rest map.
    mrhiBufferId buffer = MakeBuffer(4, mrhi_bufferCopySource);
    uint64_t misuse = mrhiDevice_GetMisuse(s_device);
    ExpectDestroy("buffer,destroy:twice first", mrhiDestroyBuffer(s_device, buffer), false, misuse);
    ExpectDestroy("buffer,destroy:twice second", mrhiDestroyBuffer(s_device, buffer), true, misuse);
    s_inexpressible += 5 + 14;
}

// texture,destroy: base, twice and invalid_texture, whose width is one
// past the device's maxTextureDimension2D.
static void TestTextureDestroys(void)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = RGBA8;
    def.width = 16;
    def.height = 16;
    def.usage = mrhi_textureSampled;
    for (uint32_t times = 1; times <= 2; ++times)
    {
        mrhiTextureId texture = {0};
        CHECK(mrhiCreateTexture(s_device, &def, &texture) == mrhi_success, "a sampled texture");
        uint64_t misuse = mrhiDevice_GetMisuse(s_device);
        for (uint32_t i = 0; i < times; ++i)
        {
            ExpectDestroy(times == 1 ? "texture,destroy:base" : "texture,destroy:twice",
                          mrhiDestroyTexture(s_device, texture), i == 1, misuse);
        }
    }
    mrhiLimits limits = {0};
    CHECK(mrhiDevice_GetLimits(s_device, &limits) == mrhi_success, "the limits");
    def.width = limits.textureDimension2d + 1;
    def.height = 1;
    mrhiTextureId invalid = {0};
    CHECK(mrhiCreateTexture(s_device, &def, &invalid) != mrhi_success && invalid.index1 == 0,
          "an invalid texture");
    ExpectDestroy("texture,destroy:invalid_texture", mrhiDestroyTexture(s_device, invalid), true,
                  mrhiDevice_GetMisuse(s_device));
}

// When a destroy case destroys its texture or query set: never, before
// the frame names it, or after its pass is added and before the submit.
typedef enum Destroyed
{
    KEPT,
    BEFORE,
    AFTER,
} Destroyed;

// texture,destroy:submit_a_destroyed_texture_as_attachment: a frame of
// a render pass clearing a color target and a depth target, its stencil
// too when it has one, each destroyed as given; the first refusal, from the imports to the
// submit, or success once the frame has finished.
static mrhiResult SubmitTargets(mrhiFormat depthFormat, Destroyed color, Destroyed depth)
{
    const mrhiTextureUsage usage = mrhi_textureRenderTarget | mrhi_textureCopyDestination;
    const Attachment defs[2] = {{.format = RGBA8, .usage = usage},
                                {.format = depthFormat, .usage = usage}};
    const Destroyed states[2] = {color, depth};
    mrhiTextureId textures[2] = {MakeAttachment(&defs[0]), MakeAttachment(&defs[1])};
    for (size_t i = 0; i < 2; ++i)
    {
        CHECK(states[i] != BEFORE || mrhiDestroyTexture(s_device, textures[i]) == mrhi_success,
              "destroyed before");
    }
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success, "a frame");
    mrhiResourceId targets[2] = {0};
    mrhiResult status = mrhi_success;
    for (size_t i = 0; i < 2 && status == mrhi_success; ++i)
    {
        status = mrhiImportTexture(s_device, textures[i], &targets[i]);
    }
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.colorTargets[0] = (mrhiColorTarget){
        .resource = targets[0],
        .load = mrhi_loadClear,
        .store = mrhi_storeKeep,
    };
    def.colorTargetCount = 1;
    def.depthTarget.resource = targets[1];
    def.depthTarget.depthLoad = mrhi_loadClear;
    def.depthTarget.depthStore = mrhi_storeDiscard;
    if (depthFormat == DEPTH)
    {
        def.depthTarget.stencilLoad = mrhi_loadClear;
        def.depthTarget.stencilStore = mrhi_storeDiscard;
    }
    mrhiPassId pass = {0};
    status = status == mrhi_success ? mrhiAddPass(s_device, &def, &pass) : status;
    for (size_t i = 0; i < 2; ++i)
    {
        CHECK(states[i] != AFTER || mrhiDestroyTexture(s_device, textures[i]) == mrhi_success,
              "destroyed after");
    }
    if (status != mrhi_success)
    {
        CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success, "dropped");
    }
    else
    {
        mrhiRequestId token = {0};
        status = mrhiDevice_CompileFrame(s_device);
        CHECK(status != mrhi_success ||
                  (mrhiPass_Begin(s_device, pass) == mrhi_success &&
                   mrhiPass_End(s_device, pass) == mrhi_success &&
                   mrhiDevice_SubmitFrame(s_device, &token) == mrhi_success && Finished(token)),
              "submitted and finished");
    }
    for (size_t i = 0; i < 2; ++i)
    {
        CHECK(states[i] != KEPT || mrhiDestroyTexture(s_device, textures[i]) == mrhi_success,
              "destroyed");
    }
    return status;
}

// The CTS's aspects become the library's formats: depth24plus-stencil8
// its depth-stencil format, and depth32float its own, without stencil
// operations. The library has no stencil-only format, so stencil8's
// cases are counted. A target destroyed before the frame
// imports it is refused as stale. One destroyed after its pass is added
// is still what the frame runs on, as the library keeps what a frame
// imported until that frame has finished, where WebGPU fails the submit:
// those cases differ.
static void TestDestroyedTargets(void)
{
    static const char* const aspects[] = {"all", "depth-only", "stencil-only"};
    static const mrhiFormat formats[] = {DEPTH, mrhi_formatDepth32Float, mrhi_formatNone};
    static const char* const states[] = {"valid", "destroyedBeforeEncode", "destroyedAfterEncode"};
    char name[160];
    for (size_t a = 0; a < 3; ++a)
    {
        for (uint32_t color = KEPT; color <= AFTER; ++color)
        {
            for (uint32_t depth = KEPT; depth <= AFTER; ++depth)
            {
                if (formats[a] == mrhi_formatNone)
                {
                    ++s_inexpressible;
                    continue;
                }
                snprintf(name, sizeof(name),
                         "texture,destroy:submit_a_destroyed_texture_as_attachment %s,%s,%s",
                         aspects[a], states[color], states[depth]);
                mrhiResult status = SubmitTargets(formats[a], color, depth);
                if (color == BEFORE || depth == BEFORE)
                {
                    ExpectStale(name, status);
                    continue;
                }
                Expect(name, status, true);
                s_differs += color == AFTER || depth == AFTER ? 1 : 0;
            }
        }
    }
}

// How a queue,destroyed,query_set case uses its set.
typedef enum QueryUse
{
    OCCLUSION_BEGUN,
    OCCLUSION_UNUSED,
    TIMESTAMP_COMPUTE,
    TIMESTAMP_RENDER,
    RESOLVED,
} QueryUse;

// A queue,destroyed,query_set case: a set of the CTS's two queries,
// destroyed before the frame or not, used by one pass; the first
// refusal, from the pass to the submit, or success once the frame has
// finished. A compute pass is one of the graphics class without targets.
static mrhiResult UseQuerySet(QueryUse use, bool destroyed)
{
    bool timestamps = use == TIMESTAMP_COMPUTE || use == TIMESTAMP_RENDER;
    mrhiQuerySetId set = MakeQuerySet(timestamps ? mrhi_queryTimestamp : mrhi_queryOcclusion, 2);
    CHECK(!destroyed || mrhiDestroyQuerySet(s_device, set) == mrhi_success, "destroyed");
    mrhiBufferId buffer = MakeBuffer(8, mrhi_bufferQueryResolve);
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId target = {0};
    mrhiResourceId results = {0};
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success &&
              mrhiImportTexture(s_device, s_target, &target) == mrhi_success &&
              mrhiImportBuffer(s_device, buffer, &results) == mrhi_success,
          "a frame of the target and the buffer");
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    if (use != TIMESTAMP_COMPUTE && use != RESOLVED)
    {
        def.colorTargets[0] = (mrhiColorTarget){.resource = target, .load = mrhi_loadKeep};
        def.colorTargetCount = 1;
    }
    if (timestamps)
    {
        def.timestampQuerySet = set;
        def.timestampBegin = 0;
    }
    else if (use != RESOLVED)
    {
        def.occlusionQuerySet = set;
    }
    const mrhiAccess access = {.resource = results, .kind = mrhi_accessQueryResolve};
    def.accesses = use == RESOLVED ? &access : nullptr;
    def.accessCount = use == RESOLVED ? 1 : 0;
    mrhiPassId pass = {0};
    mrhiResult status = mrhiAddPass(s_device, &def, &pass);
    if (status == mrhi_success)
    {
        CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success &&
                  mrhiPass_Begin(s_device, pass) == mrhi_success,
              "the pass begun");
        if (use == OCCLUSION_BEGUN)
        {
            status = mrhiPass_BeginOcclusionQuery(s_device, pass, 0);
            status = status == mrhi_success ? mrhiPass_EndOcclusionQuery(s_device, pass) : status;
        }
        if (use == RESOLVED)
        {
            status = mrhiPass_ResolveQueries(s_device, pass, set, 0, 1, results, 0);
        }
    }
    mrhiRequestId token = {0};
    CHECK(status == mrhi_success
              ? mrhiPass_End(s_device, pass) == mrhi_success &&
                    mrhiDevice_SubmitFrame(s_device, &token) == mrhi_success && Finished(token)
              : mrhiDevice_DropFrame(s_device) == mrhi_success,
          "submitted and finished, or dropped");
    CHECK((destroyed || mrhiDestroyQuerySet(s_device, set) == mrhi_success) &&
              mrhiDestroyBuffer(s_device, buffer) == mrhi_success,
          "the set and buffer destroyed");
    return status;
}

// query_set,destroy: twice and invalid_queryset, of kMaxQueryCount + 1
// queries; and queue,destroyed,query_set: beginOcclusionQuery,
// unusedOcclusionQuery, timestamps, of a compute and a render pass, and
// resolveQuerySet, each of a valid and a destroyed set.
static void TestQuerySetDestroys(void)
{
    mrhiQuerySetId set = MakeQuerySet(mrhi_queryOcclusion, 1);
    uint64_t misuse = mrhiDevice_GetMisuse(s_device);
    ExpectDestroy("query_set,destroy:twice first", mrhiDestroyQuerySet(s_device, set), false,
                  misuse);
    ExpectDestroy("query_set,destroy:twice second", mrhiDestroyQuerySet(s_device, set), true,
                  misuse);
    mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
    def.count = 4097;
    mrhiQuerySetId invalid = {0};
    CHECK(mrhiCreateQuerySet(s_device, &def, &invalid) == mrhi_errorLimit && invalid.index1 == 0,
          "an invalid query set");
    ExpectDestroy("query_set,destroy:invalid_queryset", mrhiDestroyQuerySet(s_device, invalid),
                  true, mrhiDevice_GetMisuse(s_device));
    static const char* const uses[] = {"beginOcclusionQuery", "unusedOcclusionQuery",
                                       "timestamps compute", "timestamps render",
                                       "resolveQuerySet"};
    char name[96];
    for (uint32_t use = OCCLUSION_BEGUN; use <= RESOLVED; ++use)
    {
        snprintf(name, sizeof(name), "queue,destroyed,query_set:%s valid", uses[use]);
        Expect(name, UseQuerySet(use, false), true);
        snprintf(name, sizeof(name), "queue,destroyed,query_set:%s destroyed", uses[use]);
        ExpectStale(name, UseQuerySet(use, true));
    }
}

// A queue,writeBuffer case: bytes written to a buffer in a transfer pass
// declaring it with the copy destination access; the first refusal, from
// the import to the write, or success.
static mrhiResult WriteBytes(mrhiBufferId buffer, uint64_t offset, const uint8_t* bytes,
                             uint64_t size)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success, "a frame");
    mrhiResourceId destination = {0};
    mrhiResult status = mrhiImportBuffer(s_device, buffer, &destination);
    const mrhiAccess access = Whole(destination, mrhi_accessCopyDestination);
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.neverCull = true;
    def.accesses = &access;
    def.accessCount = 1;
    mrhiPassId pass = {0};
    status = status == mrhi_success ? mrhiAddPass(s_device, &def, &pass) : status;
    if (status == mrhi_success)
    {
        CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success &&
                  mrhiPass_Begin(s_device, pass) == mrhi_success,
              "the pass begun");
        status = mrhiPass_WriteBuffer(s_device, pass, destination, offset, bytes, size);
    }
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success, "dropped");
    return status;
}

// A writeBuffer:ranges call: the array's elements, the buffer offset in
// bytes, past the buffer's end when fromEnd, then the data offset and
// size in elements, the size ALL for the rest of the array; and the
// CTS's outcome. A size past the array throws in JavaScript, where C
// passes a pointer and a size, so those calls are counted. A size not a
// multiple of 4 throws too, which the library refuses.
#define ALL UINT32_MAX

typedef enum WriteOutcome
{
    WRITTEN,
    REFUSED,
    PAST_ARRAY,
} WriteOutcome;

typedef struct WriteRange
{
    uint32_t elements;
    uint64_t offset;
    bool fromEnd;
    uint32_t dataOffset;
    uint32_t size;
    WriteOutcome outcome;
    // Run only for elements of fewer than 4 bytes.
    bool small;
} WriteRange;

static const WriteRange s_writeRanges[] = {
    {15, 0, false, 0, ALL, REFUSED, true},    {15, 0, false, 3, ALL, WRITTEN, true},
    {16, 0, false, 3, ALL, REFUSED, true},    {8, 0, false, 0, 7, REFUSED, true},
    {8, 0, false, 0, ALL, WRITTEN, false},    {16, 0, false, 0, ALL, WRITTEN, false},
    {32, 0, false, 0, ALL, REFUSED, false},   {8, 8, false, 0, ALL, WRITTEN, false},
    {16, 8, false, 0, ALL, REFUSED, false},   {8, 3, false, 0, ALL, REFUSED, false},
    {8, 0, false, 4, ALL, WRITTEN, false},    {16, 0, false, 4, ALL, WRITTEN, false},
    {32, 0, false, 4, ALL, REFUSED, false},   {32, 0, false, 16, ALL, WRITTEN, false},
    {8, 0, false, 4, 4, WRITTEN, false},      {8, 0, false, 0, 16, PAST_ARRAY, false},
    {8, 0, false, 4, 8, PAST_ARRAY, false},   {8, 0, false, 3, 4, WRITTEN, false},
    {8, 0, true, 0, 0, WRITTEN, false},       {8, 4, true, 0, 0, REFUSED, false},
    {8, 0, false, 8, 0, WRITTEN, false},      {8, 0, false, 9, 0, PAST_ARRAY, false},
    {8, 0, false, 9, ALL, PAST_ARRAY, false}, {8, 0, false, 0, 8, WRITTEN, false},
    {8, 0, false, 0, 12, PAST_ARRAY, false},
};

// queue,writeBuffer: ranges, of an ArrayBuffer and of each typed array
// but Float16Array, which the CTS skips, into a buffer of 16 elements;
// usages; buffer_state, whose invalid buffer has a map usage and is
// counted; and buffer,device_mismatch, whose mismatched case is counted,
// as the library's ids name no device.
static void TestWriteBuffers(void)
{
    static const struct
    {
        const char* name;
        uint32_t bytes;
    } arrays[] = {
        {"ArrayBuffer", 1},  {"Uint8Array", 1},   {"Uint8ClampedArray", 1}, {"Uint16Array", 2},
        {"Uint32Array", 4},  {"Int8Array", 1},    {"Int16Array", 2},        {"Int32Array", 4},
        {"Float32Array", 4}, {"Float64Array", 8}, {"BigInt64Array", 8},     {"BigUint64Array", 8},
    };
    static const uint8_t data[32 * 8] = {0};
    char name[128];
    for (size_t a = 0; a < sizeof(arrays) / sizeof(arrays[0]); ++a)
    {
        uint32_t element = arrays[a].bytes;
        mrhiBufferId buffer = MakeBuffer(16 * element, COPY_DST);
        for (size_t i = 0; i < sizeof(s_writeRanges) / sizeof(s_writeRanges[0]); ++i)
        {
            const WriteRange* r = &s_writeRanges[i];
            if (r->small && element >= 4)
            {
                continue;
            }
            if (r->outcome == PAST_ARRAY)
            {
                ++s_inexpressible;
                continue;
            }
            uint32_t size = r->size == ALL ? r->elements - r->dataOffset : r->size;
            uint64_t offset = r->offset + (r->fromEnd ? 16 * element : 0);
            snprintf(name, sizeof(name), "writeBuffer:ranges %s %u at %llu, %u from %u",
                     arrays[a].name, r->elements, (unsigned long long)offset, size, r->dataOffset);
            Expect(name,
                   WriteBytes(buffer, offset, data + (size_t)r->dataOffset * element,
                              (uint64_t)size * element),
                   r->outcome == WRITTEN);
        }
        CHECK(mrhiDestroyBuffer(s_device, buffer) == mrhi_success, "the buffer");
    }
    static const struct
    {
        mrhiBufferUsage usage;
        bool written;
    } usages[] = {
        {COPY_DST, true},
        {mrhi_bufferStorage, false},
        {mrhi_bufferStorage | COPY_SRC, false},
        {mrhi_bufferStorage | COPY_DST, true},
    };
    for (size_t i = 0; i < 4; ++i)
    {
        mrhiBufferId buffer = MakeBuffer(16, usages[i].usage);
        snprintf(name, sizeof(name), "writeBuffer:usages 0x%x", (unsigned)usages[i].usage);
        Expect(name, WriteBytes(buffer, 0, data, 16), usages[i].written);
        CHECK(mrhiDestroyBuffer(s_device, buffer) == mrhi_success, "the buffer");
    }
    mrhiBufferId buffer = MakeBuffer(16, COPY_DST);
    Expect("writeBuffer:buffer_state valid", WriteBytes(buffer, 0, data, 16), true);
    Expect("writeBuffer:buffer,device_mismatch same", WriteBytes(buffer, 0, data, 16), true);
    CHECK(mrhiDestroyBuffer(s_device, buffer) == mrhi_success, "the buffer");
    ExpectStale("writeBuffer:buffer_state destroyed", WriteBytes(buffer, 0, data, 16));
    s_inexpressible += 2;
}

// What a queue,destroyed case encodes with its objects, in one pass.
typedef enum DestroyedUse
{
    WRITTEN_BUFFER,
    WRITTEN_TEXTURE,
    COPIED_BUFFERS,
    COPIED_TO_TEXTURE,
    COPIED_TO_BUFFER,
    COPIED_TEXTURES,
    BOUND_COMPUTE,
    BOUND_RENDER,
    VERTEX_BUFFER,
    INDEX_BUFFER,
    RESOLVED_QUERY,
    ATTACHED,
} DestroyedUse;

// An object of a queue,destroyed case: a buffer, or a 1x1 texture of
// the format and samples, of the usage, which the case's pass declares
// with the access, or as a target for beginRenderPass.
typedef struct UsedObject
{
    bool texture;
    uint32_t usage;
    mrhiAccessKind kind;
    mrhiFormat format;
    uint32_t samples;
} UsedObject;

// The objects the cases use.
typedef enum Used
{
    UNUSED,
    BUFFER_FROM,
    BUFFER_TO,
    TEXTURE_FROM,
    TEXTURE_TO,
    UNIFORM,
    VERTICES,
    INDICES,
    RESULTS,
    SAMPLED,
    STORED,
    COLOR_TARGET,
    RESOLVE_TARGET,
    DEPTH_TARGET,
} Used;

#define BINDABLE (mrhi_textureSampled | mrhi_textureStorage)
#define TARGET   mrhi_textureRenderTarget

static const UsedObject s_used[] = {
    [BUFFER_FROM] = {false, COPY_SRC, mrhi_accessCopySource, mrhi_formatNone, 0},
    [BUFFER_TO] = {false, COPY_DST, mrhi_accessCopyDestination, mrhi_formatNone, 0},
    [TEXTURE_FROM] = {true, TEX_SRC, mrhi_accessCopySource, RGBA8, 1},
    [TEXTURE_TO] = {true, TEX_DST, mrhi_accessCopyDestination, RGBA8, 1},
    [UNIFORM] = {false, mrhi_bufferUniform, mrhi_accessUniform, mrhi_formatNone, 0},
    [VERTICES] = {false, mrhi_bufferVertex, mrhi_accessVertex, mrhi_formatNone, 0},
    [INDICES] = {false, mrhi_bufferIndex, mrhi_accessIndex, mrhi_formatNone, 0},
    [RESULTS] = {false, mrhi_bufferQueryResolve, mrhi_accessQueryResolve, mrhi_formatNone, 0},
    [SAMPLED] = {true, BINDABLE, mrhi_accessSampled, RGBA8, 1},
    [STORED] = {true, BINDABLE, mrhi_accessStorageWrite, RGBA8, 1},
    [COLOR_TARGET] = {true, TARGET, mrhi_accessSampled, RGBA8, 4},
    [RESOLVE_TARGET] = {true, TARGET, mrhi_accessSampled, RGBA8, 1},
    [DEPTH_TARGET] = {true, TARGET, mrhi_accessSampled, mrhi_formatDepth32Float, 4},
};

// A queue,destroyed case: its test, the values of its other parameters,
// its use, the objects its destroyed value of true names, and its
// objects.
typedef struct DestroyedCase
{
    const char* test;
    const char* rest;
    DestroyedUse use;
    uint32_t destroyed;
    Used objects[3];
} DestroyedCase;

static const DestroyedCase s_destroyedCases[] = {
    {"buffer:writeBuffer", "", WRITTEN_BUFFER, 1, {BUFFER_TO}},
    {"buffer:copyBufferToBuffer", "", COPIED_BUFFERS, 0, {BUFFER_FROM, BUFFER_TO}},
    {"buffer:copyBufferToTexture", "", COPIED_TO_TEXTURE, 1, {BUFFER_FROM, TEXTURE_TO}},
    {"buffer:copyTextureToBuffer", "", COPIED_TO_BUFFER, 2, {TEXTURE_FROM, BUFFER_TO}},
    {"buffer:setBindGroup", ",compute pass", BOUND_COMPUTE, 1, {UNIFORM}},
    {"buffer:setBindGroup", ",render pass", BOUND_RENDER, 1, {UNIFORM}},
    {"buffer:setVertexBuffer", ",render pass", VERTEX_BUFFER, 1, {VERTICES}},
    {"buffer:setIndexBuffer", ",render pass", INDEX_BUFFER, 1, {INDICES}},
    {"buffer:resolveQuerySet", "", RESOLVED_QUERY, 1, {RESULTS}},
    {"texture:writeTexture", "", WRITTEN_TEXTURE, 1, {TEXTURE_TO}},
    {"texture:copyTextureToTexture", "", COPIED_TEXTURES, 0, {TEXTURE_FROM, TEXTURE_TO}},
    {"texture:copyBufferToTexture", "", COPIED_TO_TEXTURE, 2, {BUFFER_FROM, TEXTURE_TO}},
    {"texture:copyTextureToBuffer", "", COPIED_TO_BUFFER, 1, {TEXTURE_FROM, BUFFER_TO}},
    {"texture:setBindGroup", ",compute pass,texture", BOUND_COMPUTE, 1, {SAMPLED}},
    {"texture:setBindGroup", ",compute pass,storageTexture", BOUND_COMPUTE, 1, {STORED}},
    {"texture:setBindGroup", ",render pass,texture", BOUND_RENDER, 1, {SAMPLED}},
    {"texture:setBindGroup", ",render pass,storageTexture", BOUND_RENDER, 1, {STORED}},
    {"texture:beginRenderPass", "", ATTACHED, 0, {COLOR_TARGET, RESOLVE_TARGET, DEPTH_TARGET}},
};

// The case's objects.
static uint32_t UsedCount(const DestroyedCase* c)
{
    uint32_t count = 0;
    while (count < 3 && c->objects[count] != UNUSED)
    {
        ++count;
    }
    return count;
}

// The CTS's values of what a case destroys, each with the objects it
// names: none or src, dst or both of a copy's two, none or one of
// beginRenderPass's attachments, or false and true.
static uint32_t DestroyedValues(const DestroyedCase* c, const char* const** valuesOut,
                                uint32_t masksOut[4])
{
    static const char* const bools[] = {"false", "true"};
    static const char* const sides[] = {"none", "src", "dst", "both"};
    static const char* const targets[] = {"none", "colorAttachment", "resolveAttachment",
                                          "depthStencilAttachment"};
    if (c->use == COPIED_BUFFERS || c->use == COPIED_TEXTURES || c->use == ATTACHED)
    {
        bool copy = c->use != ATTACHED;
        *valuesOut = copy ? sides : targets;
        for (uint32_t v = 0; v < 4; ++v)
        {
            masksOut[v] = copy ? v : (v == 0 ? 0 : 1u << (v - 1));
        }
        return 4;
    }
    *valuesOut = bools;
    masksOut[0] = 0;
    masksOut[1] = c->destroyed;
    return 2;
}

// Destroys the case's objects the mask names.
static void DestroyUsed(const DestroyedCase* c, uint32_t mask, const mrhiBufferId* buffers,
                        const mrhiTextureId* textures)
{
    for (uint32_t i = 0; i < UsedCount(c); ++i)
    {
        CHECK((mask & (1u << i)) == 0 ||
                  (s_used[c->objects[i]].texture
                       ? mrhiDestroyTexture(s_device, textures[i])
                       : mrhiDestroyBuffer(s_device, buffers[i])) == mrhi_success,
              "destroyed");
    }
}

// The case's pass: a transfer pass for a write or a copy, the
// attachments for beginRenderPass, a pass of the CTS's target for a
// render pass, and one of the graphics class without targets for a
// compute pass or a resolve; each object declared with its access.
static mrhiPassDef UsePass(const DestroyedCase* c, const mrhiResourceId* resources,
                           mrhiResourceId target, mrhiAccess* accesses)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    if (c->use == ATTACHED)
    {
        def.colorTargets[0] = (mrhiColorTarget){
            .resource = resources[0],
            .resolve = resources[1],
            .load = mrhi_loadClear,
            .store = mrhi_storeKeep,
        };
        def.colorTargetCount = 1;
        def.depthTarget.resource = resources[2];
        def.depthTarget.depthLoad = mrhi_loadClear;
        def.depthTarget.depthStore = mrhi_storeKeep;
        def.depthTarget.clearDepth = 0.0f;
        return def;
    }
    for (uint32_t i = 0; i < UsedCount(c); ++i)
    {
        accesses[i] = Whole(resources[i], s_used[c->objects[i]].kind);
    }
    def.accesses = accesses;
    def.accessCount = UsedCount(c);
    def.passClass = c->use <= COPIED_TEXTURES ? mrhi_passTransfer : mrhi_passGraphics;
    if (target.index1 != 0)
    {
        def.colorTargets[0] = (mrhiColorTarget){.resource = target, .load = mrhi_loadClear};
        def.colorTargetCount = 1;
    }
    return def;
}

// Records the case's command in its begun pass: nothing for a binding
// or the attachments, which the pass declares; its refusal, or success.
static mrhiResult RecordUse(const DestroyedCase* c, mrhiPassId pass, const mrhiResourceId* r,
                            mrhiQuerySetId set)
{
    static const uint8_t bytes[4] = {0};
    const mrhiExtent3d one = {1, 1, 1};
    const mrhiTextureCopy first = {.resource = r[0]};
    const mrhiTextureCopy second = {.resource = r[1]};
    const mrhiBufferCopy from = {.resource = r[0]};
    const mrhiBufferCopy to = {.resource = r[1]};
    const mrhiTexelLayout layout = {.bytesPerRow = 4};
    bool drawn = c->use == VERTEX_BUFFER || c->use == INDEX_BUFFER;
    CHECK(!drawn || mrhiPass_SetGraphicsPipeline(s_device, pass, s_graphics) == mrhi_success,
          "the pipeline");
    switch (c->use)
    {
    case WRITTEN_BUFFER:
        return mrhiPass_WriteBuffer(s_device, pass, r[0], 0, bytes, sizeof(bytes));
    case WRITTEN_TEXTURE:
        return mrhiPass_WriteTexture(s_device, pass, &first, bytes, sizeof(bytes), &layout, &one);
    case COPIED_BUFFERS:
        return mrhiPass_CopyBuffer(s_device, pass, r[0], 0, r[1], 0, 4);
    case COPIED_TO_TEXTURE:
        return mrhiPass_CopyBufferToTexture(s_device, pass, &from, &second, &one);
    case COPIED_TO_BUFFER:
        return mrhiPass_CopyTextureToBuffer(s_device, pass, &first, &to, &one);
    case COPIED_TEXTURES:
        return mrhiPass_CopyTexture(s_device, pass, &first, &second, &one);
    case VERTEX_BUFFER:
        return mrhiPass_SetVertexBuffer(s_device, pass, 0, r[0], 0, MRHI_WHOLE_SIZE);
    case INDEX_BUFFER:
        return mrhiPass_SetIndexBuffer(s_device, pass, r[0], mrhi_indexUint16, 0, MRHI_WHOLE_SIZE);
    case RESOLVED_QUERY:
        return mrhiPass_ResolveQueries(s_device, pass, set, 0, 1, r[0], 0);
    default:
        return mrhi_success;
    }
}

// A queue,destroyed case in a frame, the objects of the mask destroyed
// before the frame for a write, whose CTS case destroys before the
// call, and once the pass is recorded and before the submit for the
// rest, whose CTS case destroys after encoding; the first refusal, from
// the imports to the submit, or success once the frame has finished. A buffer has the
// CTS's 4 bytes, 8 for a resolve of its one query.
static mrhiResult UseDestroyed(const DestroyedCase* c, uint32_t mask)
{
    bool before = c->use == WRITTEN_BUFFER || c->use == WRITTEN_TEXTURE;
    mrhiBufferId buffers[3] = {0};
    mrhiTextureId textures[3] = {0};
    for (uint32_t i = 0; i < UsedCount(c); ++i)
    {
        const UsedObject* o = &s_used[c->objects[i]];
        if (o->texture)
        {
            textures[i] = MakeAttachment(&(Attachment){
                .format = o->format, .size = 1, .samples = o->samples, .usage = o->usage});
        }
        else
        {
            buffers[i] = MakeBuffer(c->use == RESOLVED_QUERY ? 8 : 4, o->usage);
        }
    }
    mrhiQuerySetId set = {0};
    set = c->use == RESOLVED_QUERY ? MakeQuerySet(mrhi_queryOcclusion, 1) : set;
    DestroyUsed(c, before ? mask : 0, buffers, textures);
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success, "a frame");
    mrhiResourceId resources[3] = {0};
    mrhiResult status = mrhi_success;
    for (uint32_t i = 0; i < UsedCount(c) && status == mrhi_success; ++i)
    {
        status = s_used[c->objects[i]].texture
                     ? mrhiImportTexture(s_device, textures[i], &resources[i])
                     : mrhiImportBuffer(s_device, buffers[i], &resources[i]);
    }
    mrhiResourceId target = {0};
    bool render = c->use == BOUND_RENDER || c->use == VERTEX_BUFFER || c->use == INDEX_BUFFER;
    CHECK(!render || mrhiImportTexture(s_device, s_target, &target) == mrhi_success, "the target");
    mrhiAccess accesses[3] = {0};
    mrhiPassDef def = UsePass(c, resources, target, accesses);
    mrhiPassId pass = {0};
    status = status == mrhi_success ? mrhiAddPass(s_device, &def, &pass) : status;
    status = status == mrhi_success ? mrhiDevice_CompileFrame(s_device) : status;
    if (status == mrhi_success)
    {
        CHECK(mrhiPass_Begin(s_device, pass) == mrhi_success, "the pass begun");
        status = RecordUse(c, pass, resources, set);
        CHECK(status != mrhi_success || mrhiPass_End(s_device, pass) == mrhi_success,
              "the pass ended");
    }
    DestroyUsed(c, before ? 0 : mask, buffers, textures);
    mrhiRequestId token = {0};
    if (status == mrhi_success)
    {
        status = mrhiDevice_SubmitFrame(s_device, &token);
        CHECK(status != mrhi_success || Finished(token), "finished");
    }
    else
    {
        CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success, "dropped");
    }
    DestroyUsed(c, ~mask & ((1u << UsedCount(c)) - 1), buffers, textures);
    CHECK(c->use != RESOLVED_QUERY || mrhiDestroyQuerySet(s_device, set) == mrhi_success,
          "the set");
    return status;
}

// queue,destroyed,buffer and queue,destroyed,texture. A write's object,
// destroyed before the call, is refused as stale, as WebGPU refuses it.
// The rest destroy after encoding, which is once the pass is recorded
// here: the frame still runs on what it imported, where WebGPU fails
// the submit, so those cases differ. A setBindGroup case declares its
// object with the binding's access, which is what a frame keeps; its
// table is not set, as a table is set only for a pipeline that reads
// it. The render bundle cases are counted, as the library has none.
static void TestQueueDestroys(void)
{
    char name[160];
    for (size_t i = 0; i < sizeof(s_destroyedCases) / sizeof(s_destroyedCases[0]); ++i)
    {
        const DestroyedCase* c = &s_destroyedCases[i];
        const char* const* values = nullptr;
        uint32_t masks[4] = {0};
        uint32_t count = DestroyedValues(c, &values, masks);
        for (uint32_t v = 0; v < count; ++v)
        {
            snprintf(name, sizeof(name), "queue,destroyed,%s %s%s", c->test, values[v], c->rest);
            mrhiResult status = UseDestroyed(c, masks[v]);
            if (masks[v] != 0 && (c->use == WRITTEN_BUFFER || c->use == WRITTEN_TEXTURE))
            {
                ExpectStale(name, status);
                continue;
            }
            Expect(name, status, true);
            s_differs += masks[v] != 0 ? 1 : 0;
        }
    }
    s_inexpressible += 2 + 2 + 2 + 4;
}

// WebGPU's bytes per texel block of a format, as the CTS's tables give
// them; 0 for the depth-stencil format, which the CTS's sized formats
// leave out.
static uint32_t BytesOf(mrhiFormat format)
{
    static const uint8_t bytes[LAST_FORMAT + 1] = {
        [mrhi_formatR8Unorm] = 1,         [mrhi_formatRg8Unorm] = 2,
        [mrhi_formatR16Float] = 2,        [mrhi_formatRg16Float] = 4,
        [mrhi_formatRgba16Float] = 8,     [mrhi_formatR32Float] = 4,
        [mrhi_formatRg32Float] = 8,       [mrhi_formatRgba32Float] = 16,
        [mrhi_formatR32Uint] = 4,         [mrhi_formatR32Sint] = 4,
        [mrhi_formatRgb10a2Unorm] = 4,    [mrhi_formatRg11b10Ufloat] = 4,
        [mrhi_formatDepth32Float] = 4,    [mrhi_formatDepthStencil] = 0,
        [mrhi_formatBc1RgbaUnorm] = 8,    [mrhi_formatBc1RgbaUnormSrgb] = 8,
        [mrhi_formatBc4RUnorm] = 8,       [mrhi_formatBc4RSnorm] = 8,
        [mrhi_formatEtc2Rgb8Unorm] = 8,   [mrhi_formatEtc2Rgb8UnormSrgb] = 8,
        [mrhi_formatEtc2Rgb8a1Unorm] = 8, [mrhi_formatEtc2Rgb8a1UnormSrgb] = 8,
        [mrhi_formatEacR11Unorm] = 8,     [mrhi_formatEacR11Snorm] = 8,
    };
    if (format <= mrhi_formatBgra8UnormSrgb)
    {
        return 4;
    }
    if (format >= mrhi_formatBc1RgbaUnorm && bytes[format] == 0)
    {
        return 16;
    }
    return bytes[format];
}

// api,validation,image_copy,buffer_related: buffer_state, usage and
// buffer,device_mismatch copy nothing between a buffer and a 2x2 rgba8
// texture. The invalid buffer has a map usage and the mismatched one
// another device, which the library's ids do not name: those cases are
// counted. A destroyed buffer is refused as stale at its import, where
// WebGPU fails the submit.
static void TestCopyBuffers(void)
{
    char name[128];
    static const mrhiBufferUsage usages[] = {COPY_SRC | mrhi_bufferUniform,
                                             COPY_DST | mrhi_bufferUniform, COPY_SRC | COPY_DST};
    for (Method m = BUFFER_TO_TEXTURE; m <= TEXTURE_TO_BUFFER; ++m)
    {
        TransferCase c = {
            .method = m,
            .block = &s_blocks[2],
            .texture = CopyTexture(&s_blocks[2], mrhi_texture2d, (mrhiExtent3d){2, 2, 1}),
            .dataSize = 16,
        };
        snprintf(name, sizeof(name), "buffer_related:buffer_state %s valid", s_methods[m]);
        Expect(name, RunTransfer(&c), true);
        snprintf(name, sizeof(name), "buffer_related:buffer,device_mismatch %s false",
                 s_methods[m]);
        Expect(name, RunTransfer(&c), true);
        for (size_t u = 0; u < 3; ++u)
        {
            c.bufferUsage = usages[u];
            snprintf(name, sizeof(name), "buffer_related:usage %s %#x", s_methods[m],
                     (unsigned)usages[u]);
            Expect(name, RunTransfer(&c),
                   (usages[u] & (m == BUFFER_TO_TEXTURE ? COPY_SRC : COPY_DST)) != 0);
        }
        c.bufferUsage = 0;
        c.destroyed = true;
        snprintf(name, sizeof(name), "buffer_related:buffer_state %s destroyed", s_methods[m]);
        ExpectStale(name, RunTransfer(&c));
        s_inexpressible += 2;
    }
}

// buffer_related:bytes_per_row_alignment, over the library's sized
// formats: one block across and up to three rows of blocks, with a
// bytesPerRow absent or at least a block's bytes. A copy with a buffer
// needs it a multiple of 256 unless absent from a copy of one row; a
// write does not. depth32float copies only to a buffer, whole. The 1D
// cases are counted, and those the CTS filters or skips are not run.
static void TestBytesPerRowAlignments(void)
{
    char name[160];
    static const int64_t values[] = {ABSENT, 0, 1, 255, 256, 257, 512};
    for (Method m = WRITE_TEXTURE; m <= TEXTURE_TO_BUFFER; ++m)
    {
        for (mrhiFormat format = RGBA8; format <= LAST_FORMAT; ++format)
        {
            bool depth = format == mrhi_formatDepth32Float;
            uint32_t bytes = BytesOf(format);
            if (bytes == 0 || (depth && m != TEXTURE_TO_BUFFER))
            {
                continue;
            }
            mrhiExtent3d block = BlockOf(format);
            const Block facts = {format, bytes, block.width, block.height, block.width > 1};
            for (Dimension d = DIMENSION_1D; d <= DIMENSION_3D; ++d)
            {
                if (!Runs(d, format))
                {
                    continue;
                }
                for (size_t v = 0; v < sizeof(values) / sizeof(values[0]); ++v)
                {
                    for (uint32_t rows = 0; rows <= 3; ++rows)
                    {
                        int64_t perRow = values[v];
                        uint32_t textureRows = rows > 0 ? rows : 1;
                        if ((d == DIMENSION_1D && rows > 1) || (depth && rows != textureRows) ||
                            (perRow == ABSENT ? rows > 1 : perRow < bytes))
                        {
                            continue;
                        }
                        if (d == DIMENSION_1D)
                        {
                            ++s_inexpressible;
                            continue;
                        }
                        TransferCase c = {
                            .method = m,
                            .block = &facts,
                            .texture = CopyTexture(
                                &facts, d == DIMENSION_3D ? mrhi_texture3d : mrhi_texture2d,
                                (mrhiExtent3d){block.width, textureRows * block.height, 1}),
                            .bytesPerRow = Value(perRow),
                            .dataSize = 512 * 8 * 16,
                            .size = {block.width, rows * block.height, 1},
                        };
                        bool success = m == WRITE_TEXTURE || (perRow == ABSENT && rows <= 1) ||
                                       (perRow > 0 && perRow % 256 == 0);
                        // The widest %lld, 20 characters, and the NUL.
                        char perRowName[21] = "undefined";
                        if (perRow != ABSENT)
                        {
                            snprintf(perRowName, sizeof(perRowName), "%lld", (long long)perRow);
                        }
                        snprintf(name, sizeof(name),
                                 "buffer_related:bytes_per_row_alignment %s format %d, %s, "
                                 "bytesPerRow=%s, copyHeightInBlocks=%u",
                                 s_methods[m], (int)format, s_dimensions[d], perRowName, rows);
                        Expect(name, RunTransfer(&c), success);
                    }
                }
            }
        }
    }
}

// The CTS's depth-stencil formats, the library's counterpart or none,
// and the aspects a copy with a buffer takes as bits of all (1),
// depth-only (2) and stencil-only (4), from a buffer, which a write
// follows, and to one; and the bytes of its depth and its stencil.
typedef struct DepthCopy
{
    const char* name;
    mrhiFormat format;
    uint8_t fromBuffer;
    uint8_t toBuffer;
    uint8_t bytes[2];
} DepthCopy;

static const DepthCopy s_depthCopies[] = {
    {"stencil8", mrhi_formatNone, 1 | 4, 1 | 4, {0, 1}},
    {"depth16unorm", mrhi_formatNone, 1 | 2, 1 | 2, {2, 0}},
    {"depth24plus", mrhi_formatNone, 0, 0, {0, 0}},
    {"depth24plus-stencil8", DEPTH, 4, 4, {0, 1}},
    {"depth32float", mrhi_formatDepth32Float, 0, 1 | 2, {4, 0}},
    {"depth32float-stencil8", mrhi_formatNone, 4, 2 | 4, {4, 1}},
};

static const char* const s_aspects[] = {"all", "depth-only", "stencil-only"};

// Whether a method copies an aspect of a depth-stencil format.
static bool CopiesAspect(const DepthCopy* d, Method method, mrhiTextureAspect aspect)
{
    uint8_t aspects = method == TEXTURE_TO_BUFFER ? d->toBuffer : d->fromBuffer;
    return ((aspects >> aspect) & 1) != 0;
}

// A transfer case of an aspect of a depth-stencil texture of a size.
static TransferCase DepthCase(Method method, mrhiFormat format, mrhiTextureAspect aspect,
                              mrhiExtent3d size)
{
    const Block block = {format, 4, 1, 1, false};
    return (TransferCase){
        .method = method,
        .texture = CopyTexture(&block, KindOf(false, size.depthOrLayers), size),
        .at = {.aspect = aspect},
        .size = size,
    };
}

// The CTS's bytesPerRow of an aspect's rows of a width, and the bytes a
// copy of its rows and layers needs.
static uint32_t AspectRow(Method method, uint32_t bytes, uint32_t width)
{
    uint32_t alignment = method == WRITE_TEXTURE ? 1 : 256;
    return (bytes * width + alignment - 1) / alignment * alignment;
}

static uint64_t AspectBytes(Method method, uint32_t bytes, mrhiExtent3d size)
{
    return (uint64_t)AspectRow(method, bytes, size.width) * (size.height * size.depthOrLayers - 1) +
           (bytes * size.width + 3) / 4 * 4;
}

// buffer_texture_copies:depth_stencil_format,copy_usage_and_aspect,
// copy_buffer_size and copy_buffer_offset, over the CTS's depth-stencil
// formats: each aspect a 1x1 texture copies, the bytes a copy of an
// aspect needs, enough and 4 fewer, and a copy's offset, which only a
// write takes unaligned. The library has depth32float, and
// depth24plus-stencil8 as its depth-stencil format; the other four are
// counted.
static void TestDepthCopies(void)
{
    char name[160];
    static const mrhiExtent3d sizes[] = {{8, 1, 1}, {4, 4, 1}, {4, 4, 3}};
    static const uint32_t offsets[] = {1, 2, 4, 6, 8};
    for (size_t f = 0; f < sizeof(s_depthCopies) / sizeof(s_depthCopies[0]); ++f)
    {
        const DepthCopy* d = &s_depthCopies[f];
        for (Method m = WRITE_TEXTURE; m <= TEXTURE_TO_BUFFER; ++m)
        {
            for (mrhiTextureAspect a = mrhi_aspectAll; a <= mrhi_aspectStencilOnly; ++a)
            {
                bool copies = CopiesAspect(d, m, a);
                if (d->format == mrhi_formatNone)
                {
                    s_inexpressible += 1 + (copies && a != mrhi_aspectAll ? 3 * 2 + 5 : 0);
                    continue;
                }
                TransferCase c = DepthCase(m, d->format, a, (mrhiExtent3d){1, 1, 1});
                c.dataSize = 32;
                snprintf(name, sizeof(name),
                         "buffer_texture_copies:depth_stencil_format,copy_usage_and_aspect %s %s "
                         "%s",
                         d->name, s_aspects[a], s_methods[m]);
                Expect(name, RunTransfer(&c), copies);
                if (!copies || a == mrhi_aspectAll)
                {
                    continue;
                }
                uint32_t bytes = d->bytes[a - 1];
                for (size_t s = 0; s < 3; ++s)
                {
                    c = DepthCase(m, d->format, a, sizes[s]);
                    c.bytesPerRow = AspectRow(m, bytes, sizes[s].width);
                    c.rowsPerImage = sizes[s].height;
                    for (uint32_t fewer = 0; fewer <= 4; fewer += 4)
                    {
                        c.dataSize = AspectBytes(m, bytes, sizes[s]) - fewer;
                        snprintf(name, sizeof(name),
                                 "buffer_texture_copies:depth_stencil_format,copy_buffer_size %s "
                                 "%s %s %ux%ux%u, %s",
                                 d->name, s_aspects[a], s_methods[m], sizes[s].width,
                                 sizes[s].height, sizes[s].depthOrLayers,
                                 fewer == 0 ? "enough" : "smaller");
                        Expect(name, RunTransfer(&c), fewer == 0);
                    }
                }
                for (size_t o = 0; o < 5; ++o)
                {
                    const mrhiExtent3d size = {4, 4, 1};
                    c = DepthCase(m, d->format, a, size);
                    c.offset = offsets[o];
                    c.bytesPerRow = AspectRow(m, bytes, size.width);
                    c.rowsPerImage = size.height;
                    uint64_t need = AspectBytes(m, bytes, size) + offsets[o];
                    c.dataSize = m == WRITE_TEXTURE ? need : (need + 3) / 4 * 4;
                    snprintf(name, sizeof(name),
                             "buffer_texture_copies:depth_stencil_format,copy_buffer_offset %s "
                             "%s %s %u",
                             d->name, s_aspects[a], s_methods[m], offsets[o]);
                    Expect(name, RunTransfer(&c), m == WRITE_TEXTURE || offsets[o] % 4 == 0);
                }
            }
        }
    }
}

// buffer_texture_copies:sample_count, texture_buffer_usages and
// device_mismatch: a copy between a buffer and a bgra8unorm texture of
// 1 or 4 samples, and between one of each single usage the CTS gives,
// but transient attachment, which it leaves out. The map usages and a
// buffer or texture of another device are counted.
static void TestCopyUsages(void)
{
    char name[160];
    static const mrhiTextureUsage textureUsages[] = {
        mrhi_textureCopySource, mrhi_textureCopyDestination, mrhi_textureSampled,
        mrhi_textureStorage, mrhi_textureRenderTarget};
    const Block bgra8 = {mrhi_formatBgra8Unorm, 4, 1, 1, false};
    for (Method m = BUFFER_TO_TEXTURE; m <= TEXTURE_TO_BUFFER; ++m)
    {
        for (uint32_t samples = 1; samples <= 4; samples += 3)
        {
            TransferCase c = {
                .method = m,
                .texture = CopyTexture(&bgra8, mrhi_texture2d, (mrhiExtent3d){16, 1, 1}),
                .dataSize = 64,
                .size = {16, 1, 1},
            };
            c.texture.sampleCount = samples;
            c.texture.usage |= samples > 1 ? mrhi_textureRenderTarget : 0;
            snprintf(name, sizeof(name), "buffer_texture_copies:sample_count %s %u", s_methods[m],
                     samples);
            Expect(name, RunTransfer(&c), samples == 1);
        }
        mrhiTextureUsage textureNeeds =
            m == BUFFER_TO_TEXTURE ? mrhi_textureCopyDestination : mrhi_textureCopySource;
        mrhiBufferUsage bufferNeeds = m == BUFFER_TO_TEXTURE ? COPY_SRC : COPY_DST;
        for (size_t t = 0; t < 5; ++t)
        {
            // MAP_READ and MAP_WRITE, then the library's usages.
            s_inexpressible += textureUsages[t] == textureNeeds ? 2 : 0;
            for (size_t b = 0; b < sizeof(s_usages) / sizeof(s_usages[0]); ++b)
            {
                bool textureValid = textureUsages[t] == textureNeeds;
                bool bufferValid = s_usages[b] == bufferNeeds;
                if (!textureValid && !bufferValid)
                {
                    continue;
                }
                TransferCase c = {
                    .method = m,
                    .block = &s_blocks[2],
                    .texture = CopyTexture(&s_blocks[2], mrhi_texture2d, (mrhiExtent3d){16, 16, 1}),
                    .dataSize = 32,
                    .size = {1, 1, 1},
                    .bufferUsage = s_usages[b],
                };
                c.texture.usage = textureUsages[t];
                snprintf(name, sizeof(name),
                         "buffer_texture_copies:texture_buffer_usages %s texture %#x, buffer %#x",
                         s_methods[m], (unsigned)textureUsages[t], (unsigned)s_usages[b]);
                Expect(name, RunTransfer(&c), textureValid && bufferValid);
            }
        }
        TransferCase c = {
            .method = m,
            .block = &s_blocks[2],
            .texture = CopyTexture(&s_blocks[2], mrhi_texture2d, (mrhiExtent3d){1, 1, 1}),
            .dataSize = 32,
            .size = {1, 1, 1},
        };
        snprintf(name, sizeof(name), "buffer_texture_copies:device_mismatch %s neither",
                 s_methods[m]);
        Expect(name, RunTransfer(&c), true);
        s_inexpressible += 2;
    }
}

// An offset_and_bytesPerRow row: the origin, the copy's size and the
// offset in half blocks, and the alignment of the texture's row bytes.
typedef struct OffsetRow
{
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t offset;
    uint32_t alignment;
} OffsetRow;

static const OffsetRow s_offsetRows[] = {
    {2, 2, 128, 4, 2, 256}, {0, 0, 128, 4, 3, 256}, {0, 0, 128, 4, 0, 128}, {0, 0, 128, 4, 0, 384},
    {3, 0, 128, 4, 0, 256}, {0, 3, 128, 4, 0, 256}, {0, 0, 129, 4, 0, 256}, {0, 0, 128, 5, 0, 256},
};

// Half blocks of a size, rounded up as the CTS's Math.ceil does.
static uint32_t Halves(uint32_t halves, uint32_t size)
{
    return (halves * size + 1) / 2;
}

// The CTS's case of a row on a format in 2D or 3D, and whether it
// succeeds: an offset of whole blocks, a bytesPerRow multiple of 256,
// and an origin and size of whole blocks.
static TransferCase OffsetCase(Method method, const Block* block, bool volume, const OffsetRow* r,
                               bool* successOut)
{
    uint32_t widthBlocks = (r->x + 1) / 2 + (r->width + 1) / 2;
    uint32_t heightBlocks = (r->y + 1) / 2 + (r->height + 1) / 2;
    uint32_t rowBytes = block->bytes * widthBlocks;
    uint32_t rows = (r->height + 1) / 2;
    uint32_t offset = Halves(r->offset, block->bytes);
    uint32_t bytesPerRow = (rowBytes + r->alignment - 1) / r->alignment * r->alignment;
    TransferCase c = {
        .method = method,
        .block = block,
        .texture = CopyTexture(
            block, volume ? mrhi_texture3d : mrhi_texture2d,
            (mrhiExtent3d){widthBlocks * block->width, heightBlocks * block->height, 1}),
        .at = {.x = Halves(r->x, block->width), .y = Halves(r->y, block->height)},
        .offset = offset,
        .bytesPerRow = bytesPerRow,
        .dataSize = offset + (uint64_t)(rows - 1) * bytesPerRow + rowBytes,
        .size = {Halves(r->width, block->width), Halves(r->height, block->height), 1},
        .bufferUsage = method == BUFFER_TO_TEXTURE ? COPY_SRC : COPY_DST,
    };
    c.texture.usage =
        method == BUFFER_TO_TEXTURE ? mrhi_textureCopyDestination : mrhi_textureCopySource;
    *successOut = offset % block->bytes == 0 && bytesPerRow % 256 == 0 &&
                  c.at.x % block->width == 0 && c.at.y % block->height == 0 &&
                  c.size.width % block->width == 0 && c.size.height % block->height == 0;
    return c;
}

// buffer_texture_copies:offset_and_bytesPerRow, over the library's color
// formats. Its fractional offsets and sizes are of one-byte formats
// only. The 1D cases are counted, and so are the BC and ASTC ones in
// 3D, which the CTS's device takes with its sliced 3D features and the
// library does not have; ETC2 in 3D is filtered out.
static void TestOffsetRows(void)
{
    char name[160];
    for (Method m = BUFFER_TO_TEXTURE; m <= TEXTURE_TO_BUFFER; ++m)
    {
        for (mrhiFormat format = RGBA8; format <= LAST_FORMAT; ++format)
        {
            if (format == mrhi_formatDepth32Float || format == DEPTH)
            {
                continue;
            }
            mrhiExtent3d extent = BlockOf(format);
            const Block block = {format, BytesOf(format), extent.width, extent.height,
                                 extent.width > 1};
            bool etc2 = format >= mrhi_formatEtc2Rgb8Unorm && format < mrhi_formatAstc4x4Unorm;
            for (Dimension d = DIMENSION_1D; d <= DIMENSION_3D; ++d)
            {
                if ((d == DIMENSION_1D && block.compressed) || (d == DIMENSION_3D && etc2))
                {
                    continue;
                }
                for (size_t i = 0; i < sizeof(s_offsetRows) / sizeof(s_offsetRows[0]); ++i)
                {
                    const OffsetRow* r = &s_offsetRows[i];
                    bool fractional = r->offset % 2 != 0 || r->width % 2 != 0 || r->height % 2 != 0;
                    if ((fractional && block.bytes > 1) || (d == DIMENSION_1D && r->y > 0))
                    {
                        continue;
                    }
                    if (d == DIMENSION_1D || (d == DIMENSION_3D && block.compressed))
                    {
                        ++s_inexpressible;
                        continue;
                    }
                    bool success = false;
                    TransferCase c = OffsetCase(m, &block, d == DIMENSION_3D, r, &success);
                    snprintf(name, sizeof(name),
                             "buffer_texture_copies:offset_and_bytesPerRow %s format %d, %s, row "
                             "%zu",
                             s_methods[m], (int)format, s_dimensions[d], i);
                    Expect(name, RunTransfer(&c), success);
                }
            }
        }
    }
}

// queue,writeTexture: texture_state, usages, sample_count and
// texture,device_mismatch write 16 bytes into one texel. A destroyed or
// invalid texture is refused as stale at its import, where WebGPU
// refuses the write; the mismatched texture is counted, as the
// library's ids name no device.
static void TestWriteTextures(void)
{
    char name[96];
    static const char* const states[] = {"valid", "destroyed", "invalid"};
    for (TextureState s = TEXTURE_VALID; s <= TEXTURE_INVALID; ++s)
    {
        TransferCase c = {
            .method = WRITE_TEXTURE,
            .block = &s_blocks[2],
            .texture = CopyTexture(&s_blocks[2], mrhi_texture2d, (mrhiExtent3d){1, 1, 1}),
            .dataSize = 16,
            .size = {1, 1, 1},
            .textureState = s,
        };
        c.texture.usage |= mrhi_textureSampled | mrhi_textureStorage | mrhi_textureRenderTarget;
        snprintf(name, sizeof(name), "writeTexture:texture_state %s", states[s]);
        if (s == TEXTURE_VALID)
        {
            Expect(name, RunTransfer(&c), true);
        }
        else
        {
            ExpectStale(name, RunTransfer(&c));
        }
    }
    static const mrhiTextureUsage usages[] = {mrhi_textureCopyDestination, mrhi_textureStorage,
                                              mrhi_textureStorage | mrhi_textureCopySource,
                                              mrhi_textureStorage | mrhi_textureCopyDestination};
    for (size_t u = 0; u < 4; ++u)
    {
        TransferCase c = {
            .method = WRITE_TEXTURE,
            .block = &s_blocks[2],
            .texture = CopyTexture(&s_blocks[2], mrhi_texture2d, (mrhiExtent3d){16, 16, 1}),
            .dataSize = 16,
            .size = {1, 1, 1},
        };
        c.texture.usage = usages[u];
        snprintf(name, sizeof(name), "writeTexture:usages %#x", (unsigned)usages[u]);
        Expect(name, RunTransfer(&c), (usages[u] & mrhi_textureCopyDestination) != 0);
    }
    const Block bgra8 = {mrhi_formatBgra8Unorm, 4, 1, 1, false};
    for (uint32_t samples = 1; samples <= 4; samples += 3)
    {
        TransferCase c = {
            .method = WRITE_TEXTURE,
            .block = &bgra8,
            .texture = CopyTexture(&bgra8, mrhi_texture2d, (mrhiExtent3d){16, 16, 1}),
            .dataSize = 16,
            .size = {1, 1, 1},
        };
        c.texture.usage = mrhi_textureCopyDestination | mrhi_textureRenderTarget;
        c.texture.sampleCount = samples;
        snprintf(name, sizeof(name), "writeTexture:sample_count %u", samples);
        Expect(name, RunTransfer(&c), samples == 1);
        if (samples == 1)
        {
            Expect("writeTexture:texture,device_mismatch false", RunTransfer(&c), true);
        }
    }
    s_inexpressible += 1;
}

// What a dynamic_state case sets.
typedef enum DynamicState
{
    SET_VIEWPORT,
    SET_SCISSOR,
    SET_BLEND_CONSTANT,
    SET_STENCIL_REFERENCE,
} DynamicState;

// A dynamic_state case: the state and its value, set in a pass of one
// rgba8unorm target of the size, kept and stored, as the CTS's pass.
typedef struct DynamicCase
{
    DynamicState state;
    uint32_t width;
    uint32_t height;
    mrhiViewport viewport;
    mrhiScissorRect scissor;
    mrhiColor color;
    uint32_t reference;
} DynamicCase;

// The case's setter's refusal, or success.
static mrhiResult SetDynamicState(const DynamicCase* c)
{
    mrhiTextureDef target = mrhiDefaultTextureDef();
    target.format = RGBA8;
    target.width = c->width;
    target.height = c->height;
    target.usage = mrhi_textureRenderTarget;
    mrhiTextureId texture = {0};
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId resource = {0};
    CHECK(mrhiCreateTexture(s_device, &target, &texture) == mrhi_success &&
              mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success &&
              mrhiImportTexture(s_device, texture, &resource) == mrhi_success,
          "a frame of the target");
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.colorTargets[0] = (mrhiColorTarget){.resource = resource};
    def.colorTargetCount = 1;
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success &&
              mrhiDevice_CompileFrame(s_device) == mrhi_success &&
              mrhiPass_Begin(s_device, pass) == mrhi_success,
          "the pass begun");
    mrhiResult status = mrhi_success;
    switch (c->state)
    {
    case SET_VIEWPORT:
        status = mrhiPass_SetViewport(s_device, pass, &c->viewport);
        break;
    case SET_SCISSOR:
        status = mrhiPass_SetScissor(s_device, pass, &c->scissor);
        break;
    case SET_BLEND_CONSTANT:
        status = mrhiPass_SetBlendConstant(s_device, pass, &c->color);
        break;
    case SET_STENCIL_REFERENCE:
        status = mrhiPass_SetStencilReference(s_device, pass, c->reference);
        break;
    }
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success &&
              mrhiDestroyTexture(s_device, texture) == mrhi_success,
          "dropped");
    return status;
}

// A viewport case of a 1x1 target: the viewport's rectangle and depths.
static mrhiResult SetViewport(float x, float y, float width, float height, float minDepth,
                              float maxDepth)
{
    const DynamicCase c = {
        .state = SET_VIEWPORT,
        .width = 1,
        .height = 1,
        .viewport = {x, y, width, height, minDepth, maxDepth},
    };
    return SetDynamicState(&c);
}

// A step of a setViewport,xy_rect_contained_in_bounds row to the next
// float below or above, as the CTS's nextAfterF32 takes.
#define STEP_DOWN 9
#define STEP_UP   10

// A setViewport,xy_rect_contained_in_bounds row: the edge in maximum
// viewport sizes, the edge's and the size's steps, and the outcome.
typedef struct BoundsRow
{
    int8_t om;
    int8_t od;
    int8_t sd;
    bool success;
} BoundsRow;

static const BoundsRow s_boundsRows[] = {
    {0, 0, 0, true},        {-1, 0, 0, true},  {-2, 0, 0, true},         {1, -1, 0, true},
    {0, -1, 0, true},       {0, 1, 0, true},   {1, 0, -1, true},         {-2, -1, 0, false},
    {1, 0, 0, false},       {1, 1, -1, false}, {1, STEP_DOWN, 0, false}, {0, 0, 1, false},
    {0, 0, STEP_UP, false},
};

// A value moved by a row's step: a whole number, or the next float of
// a positive value, by its bits, as the tests link no math library.
static float Stepped(float value, int8_t step)
{
    union
    {
        float value;
        uint32_t bits;
    } next = {value};
    next.bits += step == STEP_UP ? 1u : UINT32_MAX;
    return step == STEP_DOWN || step == STEP_UP ? next.value : value + (float)step;
}

// A step as the CTS's rows name it.
static const char* StepName(int8_t step)
{
    static const char* const names[] = {"-1", "0", "1"};
    return step == STEP_DOWN ? "negative" : step == STEP_UP ? "positive" : names[step + 1];
}

// encoding,cmds,render,dynamic_state: setViewport's cases on a 1x1
// target, but exceeds_attachment_size's on targets of 3 and 1024. The
// maximum viewport is the device's maxTextureDimension2D: a viewport
// past it is mrhi_errorCapacity, as the adapter's is above it.
static void TestViewports(void)
{
    char name[128];
    static const float sizes[][4] = {
        {0, 0, 0, 0}, {0, 0, -1, 0}, {0, 0, 0, -1}, {1, 0, -1, 0}, {0, 1, 0, -1}};
    for (size_t i = 0; i < 5; ++i)
    {
        const float* s = sizes[i];
        snprintf(name, sizeof(name), "setViewport,width_height_nonnegative %g, %g, %g, %g",
                 (double)s[0], (double)s[1], (double)s[2], (double)s[3]);
        Expect(name, SetViewport(s[0], s[1], s[2], s[3], 0, 1), s[2] >= 0 && s[3] >= 0);
    }
    for (uint32_t size = 3; size <= 1024; size += 1021)
    {
        const DynamicCase c = {
            .state = SET_VIEWPORT,
            .width = size,
            .height = size,
            .viewport = {0, 0, (float)size + 1, (float)size + 1, 0, 1},
        };
        snprintf(name, sizeof(name), "setViewport,exceeds_attachment_size %u", size);
        Expect(name, SetDynamicState(&c), true);
    }
    mrhiLimits limits = {0};
    CHECK(mrhiDevice_GetLimits(s_device, &limits) == mrhi_success, "the limits");
    float max = (float)limits.textureDimension2d;
    for (size_t d = 0; d < 2; ++d)
    {
        for (size_t i = 0; i < sizeof(s_boundsRows) / sizeof(s_boundsRows[0]); ++i)
        {
            const BoundsRow* r = &s_boundsRows[i];
            float xy[2] = {0, 0};
            float wh[2] = {max, max};
            xy[d] = Stepped(max * r->om, r->od);
            wh[d] = Stepped(wh[d], r->sd);
            snprintf(name, sizeof(name),
                     "setViewport,xy_rect_contained_in_bounds %zu, om %d, od %s, sd %s", d, r->om,
                     StepName(r->od), StepName(r->sd));
            ExpectPastLimit(name, SetViewport(xy[0], xy[1], wh[0], wh[1], 0, 1), r->success);
        }
    }
    static const float depths[][2] = {{0, 1},       {-0.0f, -0.0f}, {1, 1},
                                      {0.3f, 0.7f}, {0.7f, 0.7f},   {0.3f, 0.3f},
                                      {-0.1f, 1},   {0, 1.1f},      {0.5f, 0.49999f}};
    for (size_t i = 0; i < 9; ++i)
    {
        float lo = depths[i][0];
        float hi = depths[i][1];
        snprintf(name, sizeof(name), "setViewport,depth_rangeAndOrder %g, %g", (double)lo,
                 (double)hi);
        Expect(name, SetViewport(0, 0, 1, 1, lo, hi),
               0 <= lo && lo <= 1 && 0 <= hi && hi <= 1 && lo <= hi);
    }
}

// dynamic_state's setScissorRect, setBlendConstant and setStencilReference
// cases. A negative scissor is a TypeError in JavaScript, where the
// library's edges and sizes are unsigned: those four cases are counted.
// xy_rect_contained_in_attachment's heights are the attachment's width
// and a step, as the CTS writes them.
static void TestScissors(void)
{
    char name[128];
    DynamicCase c = {.state = SET_SCISSOR, .width = 1, .height = 1};
    Expect("setScissorRect,x_y_width_height_nonnegative 0, 0, 0, 0", SetDynamicState(&c), true);
    s_inexpressible += 4;
    static const uint32_t attachments[][2] = {{3, 5}, {5, 3}, {1024, 1}, {1, 1024}};
    static const int32_t steps[][4] = {{0, 0, 0, 0},  {1, 0, -1, 0}, {0, 1, 0, -1},
                                       {0, 0, -1, 0}, {0, 0, 0, -1}, {1, 0, 0, 0},
                                       {0, 1, 0, 0},  {0, 0, 1, 0},  {0, 0, 0, 1}};
    for (size_t a = 0; a < 4; ++a)
    {
        for (size_t i = 0; i < 9; ++i)
        {
            const int32_t* s = steps[i];
            c.width = attachments[a][0];
            c.height = attachments[a][1];
            c.scissor = (mrhiScissorRect){(uint32_t)s[0], (uint32_t)s[1],
                                          (uint32_t)((int32_t)c.width + s[2]),
                                          (uint32_t)((int32_t)c.width + s[3])};
            snprintf(name, sizeof(name),
                     "setScissorRect,xy_rect_contained_in_attachment %ux%u, %d, %d, %d, %d",
                     c.width, c.height, s[0], s[1], s[2], s[3]);
            Expect(name, SetDynamicState(&c),
                   c.scissor.x + c.scissor.width <= c.width &&
                       c.scissor.y + c.scissor.height <= c.height);
        }
    }
    static const double colors[][4] = {
        {1, 1, 1, 1}, {-1, -1, -1, -1}, {9007199254740991.0, -9007199254740991.0, -0.0, 100000}};
    c = (DynamicCase){.state = SET_BLEND_CONSTANT, .width = 1, .height = 1};
    for (size_t i = 0; i < 3; ++i)
    {
        c.color = (mrhiColor){(float)colors[i][0], (float)colors[i][1], (float)colors[i][2],
                              (float)colors[i][3]};
        snprintf(name, sizeof(name), "setBlendConstant %zu", i);
        Expect(name, SetDynamicState(&c), true);
    }
    static const uint32_t references[] = {1, 0, 1000, 0xffffffffu};
    c = (DynamicCase){.state = SET_STENCIL_REFERENCE, .width = 1, .height = 1};
    for (size_t i = 0; i < 4; ++i)
    {
        c.reference = references[i];
        snprintf(name, sizeof(name), "setStencilReference %u", references[i]);
        Expect(name, SetDynamicState(&c), true);
    }
}

// The CTS's encoder types the library has, as passes: the command
// encoder's commands are a transfer pass's, and a compute pass is one of
// the graphics class without targets.
typedef enum DebugEncoder
{
    DEBUG_COMMANDS,
    DEBUG_COMPUTE,
    DEBUG_RENDER,
} DebugEncoder;

static const char* const s_debugEncoders[] = {"non-pass", "compute pass", "render pass"};

// A debug case: its pushes, of the label or, when none, of each push's
// index, a marker when it has one, and its pops.
typedef struct DebugCase
{
    DebugEncoder encoder;
    uint32_t pushes;
    const char* group;
    size_t groupLength;
    const char* marker;
    size_t markerLength;
    uint32_t pops;
} DebugCase;

// The case in a pass of its encoder, a render pass with one 16x16
// rgba8unorm target cleared, ended after its calls: the first refusal,
// or success.
static mrhiResult RunDebug(const DebugCase* c)
{
    bool render = c->encoder == DEBUG_RENDER;
    mrhiTextureId texture =
        render ? MakeAttachment(&(Attachment){.format = RGBA8}) : (mrhiTextureId){0};
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.passClass = c->encoder == DEBUG_COMMANDS ? mrhi_passTransfer : mrhi_passGraphics;
    def.colorTargets[0].load = mrhi_loadClear;
    def.colorTargetCount = render ? 1 : 0;
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success &&
              (!render ||
               mrhiImportTexture(s_device, texture, &def.colorTargets[0].resource) == mrhi_success),
          "a frame");
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success &&
              mrhiDevice_CompileFrame(s_device) == mrhi_success &&
              mrhiPass_Begin(s_device, pass) == mrhi_success,
          "the pass begun");
    mrhiResult status = mrhi_success;
    for (uint32_t i = 0; i < c->pushes && status == mrhi_success; ++i)
    {
        const char index = (char)('0' + i);
        status = c->group != nullptr
                     ? mrhiPass_PushDebugGroup(s_device, pass, c->group, c->groupLength)
                     : mrhiPass_PushDebugGroup(s_device, pass, &index, 1);
    }
    if (status == mrhi_success && c->marker != nullptr)
    {
        status = mrhiPass_InsertDebugMarker(s_device, pass, c->marker, c->markerLength);
    }
    for (uint32_t i = 0; i < c->pops && status == mrhi_success; ++i)
    {
        status = mrhiPass_PopDebugGroup(s_device, pass);
    }
    status = status == mrhi_success ? mrhiPass_End(s_device, pass) : status;
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success &&
              (!render || mrhiDestroyTexture(s_device, texture) == mrhi_success),
          "dropped");
    return status;
}

// A label of the CTS's, as its UTF-8 bytes, and whether the library
// takes it: 1 to MRHI_MAX_LABEL_BYTES bytes without NUL, where WebGPU
// takes any string.
typedef struct DebugLabel
{
    const char* name;
    const char* bytes;
    size_t length;
    bool taken;
} DebugLabel;

#define DEBUG_LABEL(name, bytes, taken) {name, bytes, sizeof(bytes) - 1, taken}

static const DebugLabel s_groupLabels[] = {
    DEBUG_LABEL("empty", "", false),
    DEBUG_LABEL("group", "group", true),
    DEBUG_LABEL("null\\0in\\0group\\0label", "null\0in\0group\0label", false),
    DEBUG_LABEL("\\0null at beginning", "\0null at beginning", false),
    DEBUG_LABEL("sun and pointing up", "\xF0\x9F\x8C\x9E\xF0\x9F\x91\x86", true),
};

static const DebugLabel s_markerLabels[] = {
    DEBUG_LABEL("empty", "", false),
    DEBUG_LABEL("marker", "marker", true),
    DEBUG_LABEL("null\\0in\\0marker", "null\0in\0marker", false),
    DEBUG_LABEL("\\0null at beginning", "\0null at beginning", false),
    DEBUG_LABEL("sun and pointing up", "\xF0\x9F\x8C\x9E\xF0\x9F\x91\x86", true),
};

// Expects a label case's outcome: the CTS's success for a label the
// library takes, else the library's refusal, which differs.
static void ExpectLabel(const char* test, mrhiResult result, const DebugLabel* label)
{
    Expect(test, result, label->taken);
    s_differs += label->taken ? 0 : 1;
}

// encoding,cmds,debug: debug_group_balanced, debug_group and
// debug_marker, in each pass the CTS's encoder types become; the render
// bundle's cases are counted, as the library has no bundles. And
// debugMarker: push_pop_call_count_unbalance of the command encoder and
// of a compute and a render pass, around a marker.
static void TestDebugGroups(void)
{
    char name[160];
    for (DebugEncoder e = DEBUG_COMMANDS; e <= DEBUG_RENDER; ++e)
    {
        for (uint32_t pushes = 0; pushes <= 2; ++pushes)
        {
            for (uint32_t pops = 0; pops <= 2; ++pops)
            {
                const DebugCase c = {.encoder = e, .pushes = pushes, .pops = pops};
                snprintf(name, sizeof(name), "debug:debug_group_balanced %s, %u pushes, %u pops",
                         s_debugEncoders[e], pushes, pops);
                Expect(name, RunDebug(&c), pushes == pops);
            }
        }
        for (size_t l = 0; l < 5; ++l)
        {
            const DebugLabel* g = &s_groupLabels[l];
            const DebugCase group = {
                .encoder = e, .pushes = 1, .group = g->bytes, .groupLength = g->length, .pops = 1};
            snprintf(name, sizeof(name), "debug:debug_group %s, %s", s_debugEncoders[e], g->name);
            ExpectLabel(name, RunDebug(&group), g);
            const DebugLabel* m = &s_markerLabels[l];
            const DebugCase marker = {.encoder = e, .marker = m->bytes, .markerLength = m->length};
            snprintf(name, sizeof(name), "debug:debug_marker %s, %s", s_debugEncoders[e], m->name);
            ExpectLabel(name, RunDebug(&marker), m);
        }
        for (uint32_t pushes = 1; pushes <= 3; ++pushes)
        {
            for (uint32_t pops = 1; pops <= 3; ++pops)
            {
                const DebugCase c = {.encoder = e,
                                     .pushes = pushes,
                                     .group = "EventStart",
                                     .groupLength = 10,
                                     .marker = "Marker",
                                     .markerLength = 6,
                                     .pops = pops};
                snprintf(name, sizeof(name), "debugMarker:push_pop_call_count_unbalance,%s %u, %u",
                         e == DEBUG_COMMANDS  ? "command_encoder"
                         : e == DEBUG_COMPUTE ? "render_compute_pass compute"
                                              : "render_compute_pass render",
                         pushes, pops);
                Expect(name, RunDebug(&c), pushes == pops);
            }
        }
    }
    s_inexpressible += 9 + 5 + 5;
}

// The pipeline set in a render pass of one 16x16 rgba8unorm target,
// cleared, ended after it: the first refusal, or success.
static mrhiResult RunSetPipeline(mrhiGraphicsPipelineId pipeline)
{
    mrhiTextureId texture = MakeAttachment(&(Attachment){.format = RGBA8});
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.colorTargets[0].load = mrhi_loadClear;
    def.colorTargetCount = 1;
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success &&
              mrhiImportTexture(s_device, texture, &def.colorTargets[0].resource) == mrhi_success,
          "a frame");
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success &&
              mrhiDevice_CompileFrame(s_device) == mrhi_success &&
              mrhiPass_Begin(s_device, pass) == mrhi_success,
          "the pass begun");
    mrhiResult status = mrhiPass_SetGraphicsPipeline(s_device, pass, pipeline);
    status = status == mrhi_success ? mrhiPass_End(s_device, pass) : status;
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success &&
              mrhiDestroyTexture(s_device, texture) == mrhi_success,
          "dropped");
    return status;
}

// setPipeline.spec.ts. The valid pipeline, and the one of device_mismatch
// on the same device, is the noop pipeline of one rgba8unorm target, as
// the CTS's. The invalid one is the CTS's error pipeline: its creation,
// with an empty vertex entry, is refused and leaves the null id, which
// setting refuses as stale. The render bundle's 4 cases and the
// mismatched device's are counted.
static void TestSetPipeline(void)
{
    Expect("setPipeline:invalid_pipeline render pass, valid", RunSetPipeline(s_graphics), true);
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_noopContainer;
    shaderDef.byteCount = sizeof(s_noopContainer);
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(s_device, &shaderDef, &shader) == mrhi_success, "the noop shader");
    mrhiGraphicsPipelineDef error = mrhiDefaultGraphicsPipelineDef();
    error.shader = shader;
    error.vertexEntry = "";
    error.vertexEntryLength = 0;
    mrhiGraphicsPipelineId invalid = {0};
    mrhiRequestId request;
    CHECK(mrhiCreateGraphicsPipeline(s_device, &error, &invalid, &request) != mrhi_success &&
              invalid.index1 == 0 && mrhiDestroyShader(s_device, shader) == mrhi_success,
          "the error pipeline refused");
    ExpectStale("setPipeline:invalid_pipeline render pass, invalid", RunSetPipeline(invalid));
    Expect("setPipeline:pipeline,device_mismatch render pass, false", RunSetPipeline(s_graphics),
           true);
    s_inexpressible += 2 + 3;
}

// Reports a draw the library refuses as mrhi_errorState: a draw before
// the vertex buffers its pipeline reads are set, where the CTS expects
// a validation error.
static void ExpectState(const char* test, mrhiResult result, bool succeeds)
{
    if (result != (succeeds ? mrhi_success : mrhi_errorState))
    {
        printf("FAIL: %s: %d, the CTS expects %s\n", test, (int)result,
               succeeds ? "success" : "a validation error");
        ++s_failures;
    }
}

// A draw of 3 vertices with one of the two pipelines, after setting the
// pass's first buffers vertex buffers.
typedef struct TrackingDraw
{
    uint32_t pipeline;
    uint32_t buffers;
} TrackingDraw;

typedef struct TrackingPass
{
    uint32_t drawCount;
    TrackingDraw draws[2];
} TrackingPass;

// The passes in one frame, each with one 16x16 rgba8unorm target,
// cleared, and two 256-byte vertex buffers: the first refusal, or
// success.
static mrhiResult RunTracking(const mrhiGraphicsPipelineId pipelines[2], const TrackingPass* passes,
                              uint32_t passCount)
{
    mrhiBufferId buffers[2] = {MakeBuffer(256, mrhi_bufferVertex),
                               MakeBuffer(256, mrhi_bufferVertex)};
    mrhiTextureId textures[2] = {0};
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId vertices[2] = {0};
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success &&
              mrhiImportBuffer(s_device, buffers[0], &vertices[0]) == mrhi_success &&
              mrhiImportBuffer(s_device, buffers[1], &vertices[1]) == mrhi_success,
          "a frame");
    const mrhiAccess accesses[2] = {Whole(vertices[0], mrhi_accessVertex),
                                    Whole(vertices[1], mrhi_accessVertex)};
    mrhiPassId ids[2] = {0};
    for (uint32_t p = 0; p < passCount; ++p)
    {
        textures[p] = MakeAttachment(&(Attachment){.format = RGBA8});
        mrhiPassDef def = mrhiDefaultPassDef();
        def.neverCull = true;
        def.accesses = accesses;
        def.accessCount = 2;
        def.colorTargets[0].load = mrhi_loadClear;
        def.colorTargetCount = 1;
        CHECK(mrhiImportTexture(s_device, textures[p], &def.colorTargets[0].resource) ==
                      mrhi_success &&
                  mrhiAddPass(s_device, &def, &ids[p]) == mrhi_success,
              "a pass");
    }
    CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success, "compiled");
    mrhiResult status = mrhi_success;
    for (uint32_t p = 0; p < passCount && status == mrhi_success; ++p)
    {
        status = mrhiPass_Begin(s_device, ids[p]);
        for (uint32_t d = 0; d < passes[p].drawCount && status == mrhi_success; ++d)
        {
            const TrackingDraw* draw = &passes[p].draws[d];
            status = mrhiPass_SetGraphicsPipeline(s_device, ids[p], pipelines[draw->pipeline]);
            for (uint32_t i = 0; i < draw->buffers && status == mrhi_success; ++i)
            {
                status =
                    mrhiPass_SetVertexBuffer(s_device, ids[p], i, vertices[i], 0, MRHI_WHOLE_SIZE);
            }
            status = status == mrhi_success ? mrhiPass_Draw(s_device, ids[p], 3, 1, 0, 0) : status;
        }
        status = status == mrhi_success ? mrhiPass_End(s_device, ids[p]) : status;
    }
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success &&
              mrhiDestroyBuffer(s_device, buffers[0]) == mrhi_success &&
              mrhiDestroyBuffer(s_device, buffers[1]) == mrhi_success,
          "dropped");
    for (uint32_t p = 0; p < passCount; ++p)
    {
        CHECK(mrhiDestroyTexture(s_device, textures[p]) == mrhi_success, "a target destroyed");
    }
    return status;
}

// api,validation,encoding,cmds,render,state_tracking: its
// vertex_buffers_inherit_from_previous_pipeline and
// vertex_buffers_do_not_inherit_between_render_passes. Its
// all_needed_vertex_buffer_should_be_bound and
// all_needed_index_buffer_should_be_bound are not ported: the CTS marks
// them unimplemented, so they have no cases to translate or count.
// The CTS's pipelines read one float32x3 attribute and two from one
// per-vertex buffer 0 of a 12-byte stride, all at offset 0. The suite's
// instanced container has two vertex inputs, so both pipelines here
// read two such attributes. The vertex layout is the pipeline's, not
// the container's, so both read buffer 0 alone, as in the CTS, and the
// cases depend only on it. A draw without it is refused as
// mrhi_errorState.
static void TestStateTracking(void)
{
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_instancedContainer;
    shaderDef.byteCount = sizeof(s_instancedContainer);
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(s_device, &shaderDef, &shader) == mrhi_success, "the instanced shader");
    static const mrhiVertexBufferLayout buffer = {.stride = 12};
    static const mrhiVertexAttribute attributes[2] = {
        {.buffer = 0, .location = 0, .format = mrhi_vertexFloat32x3},
        {.buffer = 0, .location = 1, .format = mrhi_vertexFloat32x3},
    };
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    def.colorTargetCount = 1;
    def.vertexBuffers = &buffer;
    def.vertexBufferCount = 1;
    def.vertexAttributes = attributes;
    def.vertexAttributeCount = 2;
    mrhiGraphicsPipelineId pipelines[2] = {0};
    mrhiRequestId request;
    for (uint32_t i = 0; i < 2; ++i)
    {
        CHECK(mrhiCreateGraphicsPipeline(s_device, &def, &pipelines[i], &request) == mrhi_success,
              "a vertex pipeline");
        Answered(request);
    }
    const TrackingPass unset = {1, {{0, 0}}};
    const TrackingPass inherited = {2, {{1, 2}, {0, 0}}};
    const TrackingPass both = {1, {{1, 2}}};
    const TrackingPass reset[2][2] = {{both, {1, {{0, 1}}}}, {both, unset}};
    ExpectState("state_tracking:vertex_buffers_inherit_from_previous_pipeline, not set",
                RunTracking(pipelines, &unset, 1), false);
    ExpectState("state_tracking:vertex_buffers_inherit_from_previous_pipeline, inherited",
                RunTracking(pipelines, &inherited, 1), true);
    ExpectState("state_tracking:vertex_buffers_do_not_inherit_between_render_passes, set in each",
                RunTracking(pipelines, reset[0], 2), true);
    ExpectState("state_tracking:vertex_buffers_do_not_inherit_between_render_passes, not inherited",
                RunTracking(pipelines, reset[1], 2), false);
    CHECK(mrhiDestroyGraphicsPipeline(s_device, pipelines[0]) == mrhi_success &&
              mrhiDestroyGraphicsPipeline(s_device, pipelines[1]) == mrhi_success &&
              mrhiDestroyShader(s_device, shader) == mrhi_success,
          "the vertex pipelines destroyed");
}

// An indexed draw of the case in a render pass of one 1x1 rgba8unorm
// target, cleared, with a uint32 index buffer of the bytes given: the
// first refusal, or success.
static mrhiResult RunIndexAccess(mrhiGraphicsPipelineId pipeline, uint64_t bytes,
                                 uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex)
{
    mrhiBufferId buffer = MakeBuffer(bytes, mrhi_bufferIndex);
    mrhiTextureId texture = MakeAttachment(&(Attachment){.format = RGBA8, .size = 1});
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId indices = {0};
    mrhiPassDef def = mrhiDefaultPassDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success &&
              mrhiImportBuffer(s_device, buffer, &indices) == mrhi_success &&
              mrhiImportTexture(s_device, texture, &def.colorTargets[0].resource) == mrhi_success,
          "a frame");
    const mrhiAccess access = Whole(indices, mrhi_accessIndex);
    def.neverCull = true;
    def.accesses = &access;
    def.accessCount = 1;
    def.colorTargets[0].load = mrhi_loadClear;
    def.colorTargetCount = 1;
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success &&
              mrhiDevice_CompileFrame(s_device) == mrhi_success &&
              mrhiPass_Begin(s_device, pass) == mrhi_success &&
              mrhiPass_SetGraphicsPipeline(s_device, pass, pipeline) == mrhi_success &&
              mrhiPass_SetIndexBuffer(s_device, pass, indices, mrhi_indexUint32, 0,
                                      MRHI_WHOLE_SIZE) == mrhi_success,
          "the pass begun");
    mrhiResult status =
        mrhiPass_DrawIndexed(s_device, pass, indexCount, instanceCount, firstIndex, 0, 0);
    status = status == mrhi_success ? mrhiPass_End(s_device, pass) : status;
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success &&
              mrhiDestroyTexture(s_device, texture) == mrhi_success &&
              mrhiDestroyBuffer(s_device, buffer) == mrhi_success,
          "dropped");
    return status;
}

// api,validation,encoding,cmds,index_access. The pipeline is the noop
// one as a triangle strip of uint32 strip indices, as the CTS's. The
// index buffer holds 6 uint32 indices, or none; the library's new
// buffer reads as zeros where the CTS writes 0, 1, 2, 3, 1, 2, which no
// case's outcome reads.
static void TestIndexAccess(void)
{
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_noopContainer;
    shaderDef.byteCount = sizeof(s_noopContainer);
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(s_device, &shaderDef, &shader) == mrhi_success, "the noop shader");
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    def.colorTargetCount = 1;
    def.topology = mrhi_topologyTriangleStrip;
    def.stripIndexFormat = mrhi_indexUint32;
    mrhiGraphicsPipelineId strip = {0};
    mrhiRequestId request;
    CHECK(mrhiCreateGraphicsPipeline(s_device, &def, &strip, &request) == mrhi_success,
          "the strip pipeline");
    Answered(request);
    static const uint32_t bounds[13][2] = {
        {6, 0},
        {5, 1},
        {1, 5},
        {0, 6},
        {0, 7},
        {7, 0},
        {6, 1},
        {1, 6},
        {6, 10000},
        {10000, 0},
        {UINT32_MAX, UINT32_MAX},
        {UINT32_MAX, 2},
        {2, UINT32_MAX},
    };
    static const uint32_t empty[4][2] = {{3, 1}, {0, 1}, {3, 0}, {0, 0}};
    static const uint32_t instances[2] = {1, 10000};
    char name[160];
    for (uint32_t i = 0; i < 2; ++i)
    {
        for (size_t b = 0; b < 13; ++b)
        {
            snprintf(name, sizeof(name), "index_access:out_of_bounds %u, %u, %u", bounds[b][0],
                     bounds[b][1], instances[i]);
            Expect(name, RunIndexAccess(strip, 24, bounds[b][0], instances[i], bounds[b][1]),
                   (uint64_t)bounds[b][0] + bounds[b][1] <= 6);
        }
        for (size_t e = 0; e < 4; ++e)
        {
            snprintf(name, sizeof(name),
                     "index_access:out_of_bounds_zero_sized_index_buffer %u, %u, %u", empty[e][0],
                     empty[e][1], instances[i]);
            Expect(name, RunIndexAccess(strip, 0, empty[e][0], instances[i], empty[e][1]),
                   empty[e][0] + empty[e][1] == 0);
        }
    }
    CHECK(mrhiDestroyGraphicsPipeline(s_device, strip) == mrhi_success &&
              mrhiDestroyShader(s_device, shader) == mrhi_success,
          "the strip pipeline destroyed");
}

// A compute pass, one of the graphics class without targets, writing
// timestamps into the set at its begin and end queries, MRHI_NO_QUERY
// for none, ended: the first refusal, or success.
static mrhiResult RunComputeTimestamps(mrhiQuerySetId set, uint32_t begin, uint32_t end)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success, "a frame");
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.timestampQuerySet = set;
    def.timestampBegin = begin;
    def.timestampEnd = end;
    mrhiPassId pass = {0};
    mrhiResult status = mrhiAddPass(s_device, &def, &pass);
    if (status == mrhi_success)
    {
        CHECK(mrhiDevice_CompileFrame(s_device) == mrhi_success &&
                  mrhiPass_Begin(s_device, pass) == mrhi_success,
              "the pass begun");
        status = mrhiPass_End(s_device, pass);
    }
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success, "dropped");
    return status;
}

// api,validation,encoding,beginComputePass. The CTS's undefined query
// index is MRHI_NO_QUERY. Its invalid set is one of kMaxQueryCount + 1
// queries, whose creation the library refuses, leaving the null id,
// which a pass naming it with a query refuses as invalid: a pass's
// timestamps need a set. The mismatched device's case is counted.
static void TestBeginComputePass(void)
{
    static const char* const kinds[] = {"occlusion", "timestamp"};
    char name[128];
    for (uint32_t k = 0; k < 2; ++k)
    {
        mrhiQuerySetId set = MakeQuerySet(k == 0 ? mrhi_queryOcclusion : mrhi_queryTimestamp, 2);
        snprintf(name, sizeof(name), "beginComputePass:timestampWrites,query_set_type %s",
                 kinds[k]);
        Expect(name, RunComputeTimestamps(set, 0, 1), k == 1);
        CHECK(mrhiDestroyQuerySet(s_device, set) == mrhi_success, "the set destroyed");
    }
    mrhiQuerySetId valid = MakeQuerySet(mrhi_queryTimestamp, 1);
    Expect("beginComputePass:timestampWrites,invalid_query_set valid",
           RunComputeTimestamps(valid, 0, MRHI_NO_QUERY), true);
    mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
    def.kind = mrhi_queryTimestamp;
    def.count = 4097;
    mrhiQuerySetId invalid = {0};
    CHECK(mrhiCreateQuerySet(s_device, &def, &invalid) == mrhi_errorLimit && invalid.index1 == 0,
          "an invalid query set");
    Expect("beginComputePass:timestampWrites,invalid_query_set invalid",
           RunComputeTimestamps(invalid, 0, MRHI_NO_QUERY), false);
    mrhiQuerySetId two = MakeQuerySet(mrhi_queryTimestamp, 2);
    static const uint32_t indices[5] = {MRHI_NO_QUERY, 0, 1, 2, 3};
    static const char* const names[5] = {"undefined", "0", "1", "2", "3"};
    for (uint32_t b = 0; b < 5; ++b)
    {
        for (uint32_t e = 0; e < 5; ++e)
        {
            snprintf(name, sizeof(name), "beginComputePass:timestampWrites,query_index %s, %s",
                     names[b], names[e]);
            bool taken = b != e && (b == 0 || indices[b] < 2) && (e == 0 || indices[e] < 2);
            Expect(name, RunComputeTimestamps(two, indices[b], indices[e]), taken);
        }
    }
    Expect("beginComputePass:timestamp_query_set,device_mismatch false",
           RunComputeTimestamps(valid, 0, MRHI_NO_QUERY), true);
    CHECK(mrhiDestroyQuerySet(s_device, valid) == mrhi_success &&
              mrhiDestroyQuerySet(s_device, two) == mrhi_success,
          "the sets destroyed");
    s_inexpressible += 1;
}

// A step of an indexed draw case: a pipeline set, an index buffer set
// in a format, or an indexed draw of a count of indices.
typedef enum DrawStep
{
    STEP_PIPELINE,
    STEP_INDEX,
    STEP_DRAW,
} DrawStep;

typedef struct DrawOp
{
    DrawStep step;
    uint32_t value;
} DrawOp;

// An indexed draw case's frame: an index buffer of its bytes, bound
// from 0 for bound bytes, and a 20-byte indirect buffer.
typedef struct IndexedFrame
{
    const mrhiGraphicsPipelineId* pipelines;
    uint64_t bytes;
    uint64_t bound;
    bool indirect;
} IndexedFrame;

// The steps in a render pass of one 16x16 rgba8unorm target, cleared,
// declaring both buffers: the first refusal, or success.
static mrhiResult RunIndexedSteps(const IndexedFrame* f, const DrawOp* ops, uint32_t count)
{
    mrhiBufferId buffers[2] = {MakeBuffer(f->bytes, mrhi_bufferIndex),
                               MakeBuffer(20, mrhi_bufferIndirect)};
    mrhiTextureId texture = MakeAttachment(&(Attachment){.format = RGBA8});
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId resources[2] = {0};
    mrhiPassDef def = mrhiDefaultPassDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success &&
              mrhiImportBuffer(s_device, buffers[0], &resources[0]) == mrhi_success &&
              mrhiImportBuffer(s_device, buffers[1], &resources[1]) == mrhi_success &&
              mrhiImportTexture(s_device, texture, &def.colorTargets[0].resource) == mrhi_success,
          "a frame");
    const mrhiAccess accesses[2] = {Whole(resources[0], mrhi_accessIndex),
                                    Whole(resources[1], mrhi_accessIndirect)};
    def.neverCull = true;
    def.accesses = accesses;
    def.accessCount = 2;
    def.colorTargets[0].load = mrhi_loadClear;
    def.colorTargetCount = 1;
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success &&
              mrhiDevice_CompileFrame(s_device) == mrhi_success &&
              mrhiPass_Begin(s_device, pass) == mrhi_success,
          "the pass begun");
    mrhiResult status = mrhi_success;
    for (uint32_t i = 0; i < count && status == mrhi_success; ++i)
    {
        uint32_t value = ops[i].value;
        status = ops[i].step == STEP_PIPELINE
                     ? mrhiPass_SetGraphicsPipeline(s_device, pass, f->pipelines[value])
                 : ops[i].step == STEP_INDEX
                     ? mrhiPass_SetIndexBuffer(s_device, pass, resources[0], (mrhiIndexFormat)value,
                                               0, f->bound)
                 : f->indirect ? mrhiPass_DrawIndexedIndirect(s_device, pass, resources[1], 0)
                               : mrhiPass_DrawIndexed(s_device, pass, value, 1, 0, 0, 0);
    }
    status = status == mrhi_success ? mrhiPass_End(s_device, pass) : status;
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success &&
              mrhiDestroyTexture(s_device, texture) == mrhi_success &&
              mrhiDestroyBuffer(s_device, buffers[0]) == mrhi_success &&
              mrhiDestroyBuffer(s_device, buffers[1]) == mrhi_success,
          "dropped");
    return status;
}

// The noop pipeline of a topology and strip index format.
static mrhiGraphicsPipelineId
MakeTopologyPipeline(mrhiShaderId shader, mrhiPrimitiveTopology topology, mrhiIndexFormat strip)
{
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    def.colorTargetCount = 1;
    def.topology = topology;
    def.stripIndexFormat = strip;
    mrhiGraphicsPipelineId pipeline = {0};
    mrhiRequestId request;
    CHECK(mrhiCreateGraphicsPipeline(s_device, &def, &pipeline, &request) == mrhi_success,
          "a topology pipeline");
    Answered(request);
    return pipeline;
}

static const char* const s_indexFormats[] = {"undefined", "uint16", "uint32"};
static const char* const s_indexedDraws[] = {"drawIndexed", "drawIndexedIndirect"};
static const char* const s_setOrders[] = {"buffer first", "pipeline first"};

// draw:index_buffer_format: a pipeline of each topology, with each
// strip index format for a strip and none otherwise, and a 16-byte
// index buffer of each format, set in both orders before a draw of 3
// indices, direct and indirect. A strip whose format differs from the
// buffer's is refused as mrhi_errorState.
static void TestIndexBufferFormats(const mrhiGraphicsPipelineId pipelines[9])
{
    static const char* const topologies[] = {"point-list", "line-list", "line-strip",
                                             "triangle-list", "triangle-strip"};
    char name[160];
    uint32_t made = 0;
    for (uint32_t topology = 0; topology < 5; ++topology)
    {
        bool isStrip = topology == mrhi_topologyLineStrip || topology == mrhi_topologyTriangleStrip;
        for (uint32_t strip = 0; strip < (isStrip ? 3u : 1u); ++strip, ++made)
        {
            for (uint32_t format = 1; format <= 2; ++format)
            {
                for (uint32_t d = 0; d < 2; ++d)
                {
                    const IndexedFrame f = {pipelines, 16, MRHI_WHOLE_SIZE, d == 1};
                    for (uint32_t first = 0; first < 2; ++first)
                    {
                        const DrawOp ops[3] = {
                            {first ? STEP_PIPELINE : STEP_INDEX, first ? made : format},
                            {first ? STEP_INDEX : STEP_PIPELINE, first ? format : made},
                            {STEP_DRAW, 3}};
                        snprintf(name, sizeof(name), "draw:index_buffer_format %s, %s, %s, %s, %s",
                                 topologies[topology], s_indexFormats[strip],
                                 s_indexFormats[format], s_indexedDraws[d], s_setOrders[first]);
                        ExpectState(name, RunIndexedSteps(&f, ops, 3), !isStrip || strip == format);
                    }
                }
            }
        }
    }
}

// draw:index_buffer_format_dirtying and index_buffer_OOB. Pipelines 8
// and 7 are the triangle strips of uint32 and uint16 strip indices;
// pipeline 5 is the triangle list. An indirect draw's indices are not
// checked on the CPU, as the CTS's.
static void TestIndexBufferDirtying(const mrhiGraphicsPipelineId pipelines[9])
{
    static const char* const dirties[] = {"pipeline", "indexBuffer", "neither"};
    char name[160];
    for (uint32_t dirty = 0; dirty < 3; ++dirty)
    {
        for (uint32_t d = 0; d < 2; ++d)
        {
            const IndexedFrame f = {pipelines, 16, MRHI_WHOLE_SIZE, d == 1};
            const DrawOp ops[5] = {{STEP_PIPELINE, 8},
                                   {STEP_INDEX, mrhi_indexUint32},
                                   {STEP_DRAW, 3},
                                   dirty == 0   ? (DrawOp){STEP_PIPELINE, 7}
                                   : dirty == 1 ? (DrawOp){STEP_INDEX, mrhi_indexUint16}
                                                : (DrawOp){STEP_DRAW, 3},
                                   {STEP_DRAW, 3}};
            snprintf(name, sizeof(name), "draw:index_buffer_format_dirtying %s, %s", dirties[dirty],
                     s_indexedDraws[d]);
            ExpectState(name, RunIndexedSteps(&f, ops, dirty == 2 ? 4 : 5), dirty == 2);
        }
    }
    for (uint32_t elements = 10; elements <= 100; elements += 90)
    {
        for (uint32_t count = 10; count <= 11; ++count)
        {
            for (uint32_t d = 0; d < 2; ++d)
            {
                for (uint32_t format = 1; format <= 2; ++format)
                {
                    uint32_t width = format * 2;
                    const IndexedFrame f = {pipelines, elements * width, 10 * width, d == 1};
                    for (uint32_t first = 0; first < 2; ++first)
                    {
                        const DrawOp ops[3] = {
                            {first ? STEP_PIPELINE : STEP_INDEX, first ? 5 : format},
                            {first ? STEP_INDEX : STEP_PIPELINE, first ? format : 5},
                            {STEP_DRAW, count}};
                        snprintf(name, sizeof(name), "draw:index_buffer_OOB %u, 10, %u, %s, %s, %s",
                                 elements, count, s_indexedDraws[d], s_indexFormats[format],
                                 s_setOrders[first]);
                        Expect(name, RunIndexedSteps(&f, ops, 3), count <= 10 || d == 1);
                    }
                }
            }
        }
    }
}

// api,validation,encoding,cmds,render,draw: its index buffer tests.
// Each case runs in a render pass, in both orders of setPipeline and
// setIndexBuffer where the CTS sets both; the CTS's render bundle half
// of each case is not run, since the library has no bundles. The
// indirect buffer reads as zeros where the CTS writes the draw's
// arguments, which no case's outcome reads.
static void TestDrawIndexBuffers(void)
{
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_noopContainer;
    shaderDef.byteCount = sizeof(s_noopContainer);
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(s_device, &shaderDef, &shader) == mrhi_success, "the noop shader");
    mrhiGraphicsPipelineId pipelines[9] = {0};
    uint32_t made = 0;
    for (uint32_t topology = 0; topology < 5; ++topology)
    {
        bool isStrip = topology == mrhi_topologyLineStrip || topology == mrhi_topologyTriangleStrip;
        for (uint32_t strip = 0; strip < (isStrip ? 3u : 1u); ++strip)
        {
            pipelines[made++] = MakeTopologyPipeline(shader, (mrhiPrimitiveTopology)topology,
                                                     (mrhiIndexFormat)strip);
        }
    }
    TestIndexBufferFormats(pipelines);
    TestIndexBufferDirtying(pipelines);
    for (uint32_t i = 0; i < 9; ++i)
    {
        CHECK(mrhiDestroyGraphicsPipeline(s_device, pipelines[i]) == mrhi_success,
              "a topology pipeline destroyed");
    }
    CHECK(mrhiDestroyShader(s_device, shader) == mrhi_success, "the noop shader destroyed");
}

// A buffer a vertex buffer case binds: one of its buffers, from a byte
// for bytes, or none.
typedef struct VertexBind
{
    int32_t buffer;
    uint64_t offset;
    uint64_t size;
} VertexBind;

#define NO_BIND ((VertexBind){-1, 0, 0})

// A vertex buffer case: its buffers' bytes, what it binds to vertex
// slots 1 and 7 and as a uint16 index buffer, and a draw of the CTS's
// draw types, whose counts and firsts are of indices for an indexed
// draw. holes sets slots 0 and 2 to 6 to no bytes of its first buffer,
// for a pipeline that reads all eight.
typedef struct VertexFrame
{
    mrhiGraphicsPipelineId pipeline;
    uint64_t bytes[3];
    uint32_t bufferCount;
    VertexBind slots[2];
    VertexBind index;
    bool holes;
    uint32_t type;
    uint32_t count;
    uint32_t instanceCount;
    uint32_t first;
    int32_t baseVertex;
    uint32_t firstInstance;
} VertexFrame;

static const char* const s_drawTypes[] = {"draw", "drawIndexed", "drawIndirect",
                                          "drawIndexedIndirect"};

static mrhiResult SetVertexBind(mrhiPassId pass, const mrhiResourceId* resources, uint32_t slot,
                                VertexBind bind)
{
    return bind.buffer < 0 ? mrhi_success
                           : mrhiPass_SetVertexBuffer(s_device, pass, slot, resources[bind.buffer],
                                                      bind.offset, bind.size);
}

// The binds and the draw, with the pipeline set before or after the
// binds: the first refusal, or success.
static mrhiResult RunVertexSteps(const VertexFrame* f, mrhiPassId pass,
                                 const mrhiResourceId* resources, bool pipelineFirst)
{
    mrhiResult status =
        pipelineFirst ? mrhiPass_SetGraphicsPipeline(s_device, pass, f->pipeline) : mrhi_success;
    if (status == mrhi_success && f->index.buffer >= 0)
    {
        status = mrhiPass_SetIndexBuffer(s_device, pass, resources[f->index.buffer],
                                         mrhi_indexUint16, f->index.offset, f->index.size);
    }
    for (uint32_t slot = 0; slot < 8 && status == mrhi_success; ++slot)
    {
        VertexBind hole = f->holes ? (VertexBind){0, 0, 0} : NO_BIND;
        status = SetVertexBind(pass, resources, slot,
                               slot == 1   ? f->slots[0]
                               : slot == 7 ? f->slots[1]
                                           : hole);
    }
    if (status == mrhi_success && !pipelineFirst)
    {
        status = mrhiPass_SetGraphicsPipeline(s_device, pass, f->pipeline);
    }
    mrhiResourceId indirect = resources[f->bufferCount];
    if (status == mrhi_success)
    {
        status = f->type == 0 ? mrhiPass_Draw(s_device, pass, f->count, f->instanceCount, f->first,
                                              f->firstInstance)
                 : f->type == 1 ? mrhiPass_DrawIndexed(s_device, pass, f->count, f->instanceCount,
                                                       f->first, f->baseVertex, f->firstInstance)
                 : f->type == 2 ? mrhiPass_DrawIndirect(s_device, pass, indirect, 0)
                                : mrhiPass_DrawIndexedIndirect(s_device, pass, indirect, 0);
    }
    return status == mrhi_success ? mrhiPass_End(s_device, pass) : status;
}

// The case in a render pass of the suite's target, cleared, declaring
// each buffer with the vertex and index accesses and a 20-byte indirect
// buffer: the first refusal, or success.
static mrhiResult RunVertexCase(const VertexFrame* f, bool pipelineFirst)
{
    mrhiBufferId buffers[4] = {0};
    mrhiResourceId resources[4] = {0};
    mrhiAccess accesses[7] = {0};
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiPassDef def = mrhiDefaultPassDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success &&
              mrhiImportTexture(s_device, s_target, &def.colorTargets[0].resource) == mrhi_success,
          "a frame");
    uint32_t count = 0;
    for (uint32_t i = 0; i < f->bufferCount; ++i)
    {
        buffers[i] = MakeBuffer(f->bytes[i], mrhi_bufferVertex | mrhi_bufferIndex);
        CHECK(mrhiImportBuffer(s_device, buffers[i], &resources[i]) == mrhi_success, "a buffer");
        accesses[count++] = Whole(resources[i], mrhi_accessVertex);
        accesses[count++] = Whole(resources[i], mrhi_accessIndex);
    }
    buffers[f->bufferCount] = MakeBuffer(20, mrhi_bufferIndirect);
    CHECK(mrhiImportBuffer(s_device, buffers[f->bufferCount], &resources[f->bufferCount]) ==
              mrhi_success,
          "the indirect buffer");
    accesses[count++] = Whole(resources[f->bufferCount], mrhi_accessIndirect);
    def.neverCull = true;
    def.accesses = accesses;
    def.accessCount = count;
    def.colorTargets[0].load = mrhi_loadClear;
    def.colorTargetCount = 1;
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success &&
              mrhiDevice_CompileFrame(s_device) == mrhi_success &&
              mrhiPass_Begin(s_device, pass) == mrhi_success,
          "the pass begun");
    mrhiResult status = RunVertexSteps(f, pass, resources, pipelineFirst);
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success, "dropped");
    for (uint32_t i = 0; i <= f->bufferCount; ++i)
    {
        CHECK(mrhiDestroyBuffer(s_device, buffers[i]) == mrhi_success, "a buffer destroyed");
    }
    return status;
}

// The CTS's makeTestPipelineWithVertexAndInstanceBuffer: buffers 1 and
// 7 of the stride, stepping by vertex and by instance, each with one
// attribute of the format at the offset, at locations 2 and 6. Maul
// RHI's pipelines have no unused buffer, so buffers 0 and 2 to 6 have
// no attributes and a stride of 0, and every case binds them.
static mrhiGraphicsPipelineId MakeVertexPipeline(mrhiShaderId shader, uint32_t stride,
                                                 mrhiVertexFormat format, uint32_t offset)
{
    mrhiVertexBufferLayout buffers[8] = {0};
    buffers[1] = (mrhiVertexBufferLayout){stride, mrhi_stepVertex};
    buffers[7] = (mrhiVertexBufferLayout){stride, mrhi_stepInstance};
    const mrhiVertexAttribute attributes[2] = {{1, 2, format, offset}, {7, 6, format, offset}};
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    def.vertexBuffers = buffers;
    def.vertexBufferCount = 8;
    def.vertexAttributes = attributes;
    def.vertexAttributeCount = 2;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    def.colorTargetCount = 1;
    mrhiGraphicsPipelineId pipeline = {0};
    mrhiRequestId request;
    CHECK(mrhiCreateGraphicsPipeline(s_device, &def, &pipeline, &request) == mrhi_success,
          "a vertex pipeline");
    Answered(request);
    return pipeline;
}

static const char* const s_bools[] = {"false", "true"};

// The CTS's kDefaultParameterForDraw with its indexed or non-indexed
// defaults: 100 of each count and first, and a base vertex of 100.
static void DefaultDraw(VertexFrame* f, uint32_t type)
{
    f->type = type;
    f->count = 100;
    f->instanceCount = 100;
    f->first = 100;
    f->baseVertex = 100;
    f->firstInstance = 100;
}

// draw:unused_buffer_bound: a small buffer bound to vertex slots 1 and
// 7 or as the index buffer of a pipeline that reads none of them, and
// an index buffer of 200 uint16 indices for an indexed draw. Maul RHI
// takes an index range in whole indices, as in setIndexBuffer's cases,
// so the 16 cases of a 1-byte index range differ.
static void TestUnusedBufferBound(void)
{
    char name[160];
    for (uint32_t bits = 0; bits < 8; ++bits)
    {
        bool smallIndex = (bits & 4) != 0;
        for (uint32_t type = 0; type < 4; ++type)
        {
            bool indexed = type == 1 || type == 3;
            for (uint32_t c = 0; c < 4 && !(smallIndex && indexed); ++c)
            {
                uint64_t offset = (c & 2) * 2;
                uint64_t size = c & 1;
                VertexBind small = {0, offset, size};
                VertexFrame f = {
                    .pipeline = s_graphics,
                    .bytes = {offset + size, 400},
                    .bufferCount = 2,
                    .slots = {(bits & 2) != 0 ? small : NO_BIND, (bits & 1) != 0 ? small : NO_BIND},
                    .index = indexed      ? (VertexBind){1, 0, 400}
                             : smallIndex ? small
                                          : NO_BIND,
                };
                DefaultDraw(&f, type);
                bool differs = smallIndex && size == 1;
                s_differs += differs ? 1 : 0;
                for (uint32_t first = 0; first < 2; ++first)
                {
                    snprintf(
                        name, sizeof(name), "draw:unused_buffer_bound %s, %s, %s, %s, %u, %u, %s",
                        s_bools[smallIndex], s_bools[(bits & 2) != 0], s_bools[bits & 1],
                        s_drawTypes[type], (unsigned)offset, (unsigned)size, s_setOrders[first]);
                    Expect(name, RunVertexCase(&f, first == 1), !differs);
                }
            }
        }
    }
}

// The CTS's vertex_buffer_OOB pipelines: a stride of zero, exactly the
// attribute's reach, or 20 bytes more, rounded up to 4; an attribute
// offset of 0, 1, 2 or 7 times the lesser of 4 and the format's size.
typedef struct OobLayout
{
    uint32_t stride;
    uint32_t lastStride;
} OobLayout;

static const char* const s_boundSizes[] = {"exact", "zero", "oneTooSmall"};
static const char* const s_arrayStrides[] = {"zero", "exact", "oversize"};

static OobLayout OobLayoutOf(uint32_t strideState, uint32_t factor, uint32_t formatBytes)
{
    uint32_t lastStride = factor * (formatBytes < 4 ? formatBytes : 4) + formatBytes;
    uint32_t stride = strideState == 0 ? 0 : lastStride + (strideState == 2 ? 20 : 0);
    return (OobLayout){(stride + 3) & ~3u, lastStride};
}

// The bytes a case binds for a buffer read for count elements: the
// CTS's required size, none, or one byte less.
static uint64_t BoundBytes(OobLayout layout, uint32_t state, uint64_t count)
{
    uint64_t required =
        count > 0 ? layout.stride * (count - 1) + layout.lastStride : layout.lastStride;
    return state == 0 ? required : state == 1 ? 0 : required - 1;
}

// A vertex_buffer_OOB case's firsts and counts of vertices or of
// instances: one set for a zero stride count, four otherwise.
static const uint32_t s_oobCounts[5][2] = {
    {0, 0}, {0, 1}, {0, 10000}, {10000, 0}, {10000, 10000},
};

// One vertex_buffer_OOB case: its vertex and instance buffers bound
// from byte 200 for their bound sizes, and a 200-index buffer for an
// indexed draw, whose base vertex is the first vertex. Only a draw
// checks its vertex-step buffer, and only a direct draw its
// instance-step buffer; neither is checked for a zero stride count.
static void RunOobCase(const char* prefix, const VertexFrame* base, OobLayout layout,
                       const uint32_t states[2], const uint32_t* v, const uint32_t* i)
{
    char name[224];
    uint64_t vertexBytes = BoundBytes(layout, states[0], (uint64_t)v[0] + v[1]);
    uint64_t instanceBytes = BoundBytes(layout, states[1], (uint64_t)i[0] + i[1]);
    VertexFrame f = *base;
    f.bytes[0] = 200 + vertexBytes;
    f.bytes[1] = 200 + instanceBytes;
    f.slots[0] = (VertexBind){0, 200, vertexBytes};
    f.slots[1] = (VertexBind){1, 200, instanceBytes};
    bool indexed = f.type == 1 || f.type == 3;
    f.index = indexed ? (VertexBind){2, 0, 400} : NO_BIND;
    f.count = indexed ? 100 : v[1];
    f.first = indexed ? 100 : v[0];
    f.baseVertex = (int32_t)v[0];
    f.instanceCount = i[1];
    f.firstInstance = i[0];
    bool vertexOob = states[0] != 0 && f.type == 0 && v[0] + v[1] > 0;
    bool instanceOob = states[1] != 0 && f.type <= 1 && i[0] + i[1] > 0;
    for (uint32_t first = 0; first < 2; ++first)
    {
        snprintf(name, sizeof(name), "%s, %u, %u, %u, %u, %s", prefix, v[0], v[1], i[0], i[1],
                 s_setOrders[first]);
        Expect(name, RunVertexCase(&f, first == 1), !vertexOob && !instanceOob);
    }
}

// The subcases of one vertex_buffer_OOB pipeline and draw type: each
// set of vertex and instance firsts and counts but both counts 10000.
static void RunOobSubcases(const char* prefix, const VertexFrame* base, OobLayout layout,
                           const uint32_t states[2], bool vertexZero, bool instanceZero)
{
    for (uint32_t vs = vertexZero ? 0 : 1; vs < (vertexZero ? 1u : 5u); ++vs)
    {
        for (uint32_t is = instanceZero ? 0 : 1; is < (instanceZero ? 1u : 5u); ++is)
        {
            if (s_oobCounts[vs][1] != 10000 || s_oobCounts[is][1] != 10000)
            {
                RunOobCase(prefix, base, layout, states, s_oobCounts[vs], s_oobCounts[is]);
            }
        }
    }
}

// draw:vertex_buffer_OOB: a pipeline of each stride, attribute offset
// and format, its vertex and instance buffers bound for exactly what a
// draw reads, for nothing, or for one byte less, in each draw type.
static void TestVertexBufferOob(mrhiShaderId shader)
{
    static const uint32_t states[5][2] = {{0, 0}, {1, 0}, {2, 0}, {0, 1}, {0, 2}};
    static const uint32_t factors[4] = {0, 1, 2, 7};
    static const struct
    {
        mrhiVertexFormat format;
        uint32_t bytes;
        const char* name;
    } formats[3] = {{mrhi_vertexSnorm8x2, 2, "snorm8x2"},
                    {mrhi_vertexFloat32, 4, "float32"},
                    {mrhi_vertexFloat16x4, 8, "float16x4"}};
    mrhiGraphicsPipelineId pipelines[36] = {0};
    for (uint32_t p = 0; p < 36; ++p)
    {
        uint32_t bytes = formats[p % 3].bytes;
        OobLayout layout = OobLayoutOf(p / 12, factors[p / 3 % 4], bytes);
        pipelines[p] = MakeVertexPipeline(shader, layout.stride, formats[p % 3].format,
                                          layout.lastStride - bytes);
    }
    char prefix[160];
    for (uint32_t type = 0; type < 4; ++type)
    {
        for (uint32_t c = 0; c < 5 * 144; ++c)
        {
            uint32_t sizes = c / 144;
            uint32_t p = c / 48 % 3 * 12 + c % 12;
            bool vertexZero = (c / 24 & 1) != 0;
            bool instanceZero = (c / 12 & 1) != 0;
            OobLayout layout = OobLayoutOf(p / 12, factors[p / 3 % 4], formats[p % 3].bytes);
            VertexFrame base = {.pipeline = pipelines[p],
                                .bytes = {0, 0, 400},
                                .bufferCount = 3,
                                .holes = true,
                                .type = type};
            snprintf(prefix, sizeof(prefix),
                     "draw:vertex_buffer_OOB %s, %s, %s, %s, %s, %s, %u, 200, %s",
                     s_drawTypes[type], s_boundSizes[states[sizes][0]],
                     s_boundSizes[states[sizes][1]], s_arrayStrides[p / 12], s_bools[vertexZero],
                     s_bools[instanceZero], factors[p / 3 % 4], formats[p % 3].name);
            RunOobSubcases(prefix, &base, layout, states[sizes], vertexZero, instanceZero);
        }
    }
    for (uint32_t p = 0; p < 36; ++p)
    {
        CHECK(mrhiDestroyGraphicsPipeline(s_device, pipelines[p]) == mrhi_success,
              "a vertex pipeline destroyed");
    }
}

// The CTS's calcSetBufferOffset: a factor, in halves, of the bound
// size, rounded up to a byte and then to a multiple of 4.
static uint64_t HalfOffset(uint64_t size, uint32_t halves)
{
    uint64_t offset = (size * halves + 1) / 2;
    return (offset + 3) & ~(uint64_t)3;
}

// draw:buffer_binding_overlap: one buffer bound to vertex slots 1 and
// 7 and as the uint16 index buffer, each from an offset factor of its
// bound size, for the default draws over float32x4 attributes. Every
// case succeeds.
static void TestBufferBindingOverlap(mrhiShaderId shader)
{
    static const char* const factors[] = {"0", "0.5", "1", "1.5", "2"};
    mrhiGraphicsPipelineId pipelines[3] = {0};
    for (uint32_t s = 0; s < 3; ++s)
    {
        pipelines[s] =
            MakeVertexPipeline(shader, OobLayoutOf(s, 0, 16).stride, mrhi_vertexFloat32x4, 0);
    }
    char name[160];
    for (uint32_t type = 0; type < 4; ++type)
    {
        for (uint32_t c = 0; c < 125 * 3; ++c)
        {
            uint32_t v = c / 75;
            uint32_t i = c / 15 % 5;
            uint32_t x = c / 3 % 5;
            uint64_t bound = BoundBytes(OobLayoutOf(c % 3, 0, 16), 0, 200);
            VertexBind slots[2] = {{0, HalfOffset(bound, v), bound},
                                   {0, HalfOffset(bound, i), bound}};
            VertexBind index = {0, HalfOffset(400, x), 400};
            uint64_t bytes = slots[0].offset > slots[1].offset ? slots[0].offset : slots[1].offset;
            bytes = bytes + bound > index.offset + 400 ? bytes + bound : index.offset + 400;
            VertexFrame f = {.pipeline = pipelines[c % 3],
                             .bytes = {bytes},
                             .bufferCount = 1,
                             .slots = {slots[0], slots[1]},
                             .index = index,
                             .holes = true};
            DefaultDraw(&f, type);
            for (uint32_t first = 0; first < 2; ++first)
            {
                snprintf(name, sizeof(name), "draw:buffer_binding_overlap %s, %s, %s, %s, %s, %s",
                         s_drawTypes[type], factors[v], factors[i], factors[x],
                         s_arrayStrides[c % 3], s_setOrders[first]);
                Expect(name, RunVertexCase(&f, first == 1), true);
            }
        }
    }
    for (uint32_t s = 0; s < 3; ++s)
    {
        CHECK(mrhiDestroyGraphicsPipeline(s_device, pipelines[s]) == mrhi_success,
              "a vertex pipeline destroyed");
    }
}

// api,validation,encoding,cmds,render,draw: its vertex buffer tests,
// run as its index buffer tests are. last_buffer_setting_take_account
// is unimplemented in the CTS. max_draw_count's 44 cases have no
// counterpart: a pass def has no maxDrawCount to refuse draws past.
static void TestDrawVertexBuffers(void)
{
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_noopContainer;
    shaderDef.byteCount = sizeof(s_noopContainer);
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(s_device, &shaderDef, &shader) == mrhi_success, "the noop shader");
    TestUnusedBufferBound();
    TestVertexBufferOob(shader);
    TestBufferBindingOverlap(shader);
    CHECK(mrhiDestroyShader(s_device, shader) == mrhi_success, "the noop shader destroyed");
    s_inexpressible += 44;
}

// A pipeline of the CTS's getDescriptor, the suite's noop pipeline,
// with a primitive state: the refusal, or success, destroying it.
static mrhiResult MakePrimitivePipeline(mrhiShaderId shader, mrhiPrimitiveTopology topology,
                                        mrhiIndexFormat strip, bool unclipped)
{
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    def.colorTargetCount = 1;
    def.topology = topology;
    def.stripIndexFormat = strip;
    def.unclippedDepth = unclipped;
    mrhiGraphicsPipelineId pipeline = {0};
    mrhiRequestId request;
    mrhiResult status = mrhiCreateGraphicsPipeline(s_device, &def, &pipeline, &request);
    if (status == mrhi_success)
    {
        Answered(request);
        CHECK(mrhiDestroyGraphicsPipeline(s_device, pipeline) == mrhi_success,
              "a primitive pipeline destroyed");
    }
    return status;
}

// api,validation,render_pipeline,primitive_state. Its isAsync cases
// both make the pipeline the library's one way, refusing a def at the
// call and answering the request later. An undefined topology is the
// default triangle list, and an undefined strip index format
// mrhi_indexNone. The test device has no unclippedDepth, the CTS's
// depth-clip-control, so unclipped depth is refused for it.
static void TestPrimitiveState(void)
{
    static const char* const asyncs[] = {"false", "true"};
    static const char* const topologies[] = {"undefined",  "point-list",    "line-list",
                                             "line-strip", "triangle-list", "triangle-strip"};
    static const mrhiPrimitiveTopology kinds[] = {
        mrhi_topologyTriangleList, mrhi_topologyPointList,    mrhi_topologyLineList,
        mrhi_topologyLineStrip,    mrhi_topologyTriangleList, mrhi_topologyTriangleStrip};
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_noopContainer;
    shaderDef.byteCount = sizeof(s_noopContainer);
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(s_device, &shaderDef, &shader) == mrhi_success, "the noop shader");
    char name[128];
    for (uint32_t a = 0; a < 2; ++a)
    {
        for (uint32_t t = 0; t < 6; ++t)
        {
            bool strip =
                kinds[t] == mrhi_topologyLineStrip || kinds[t] == mrhi_topologyTriangleStrip;
            for (uint32_t format = 0; format < 3; ++format)
            {
                snprintf(name, sizeof(name), "primitive_state:strip_index_format %s, %s, %s",
                         asyncs[a], topologies[t], s_indexFormats[format]);
                Expect(name,
                       MakePrimitivePipeline(shader, kinds[t], (mrhiIndexFormat)format, false),
                       strip || format == mrhi_indexNone);
            }
        }
        for (uint32_t unclipped = 0; unclipped < 2; ++unclipped)
        {
            snprintf(name, sizeof(name), "primitive_state:unclipped_depth %s, %s", asyncs[a],
                     s_bools[unclipped]);
            ExpectUngranted(name,
                            MakePrimitivePipeline(shader, mrhi_topologyTriangleList, mrhi_indexNone,
                                                  unclipped == 1),
                            unclipped == 0);
        }
    }
    CHECK(mrhiDestroyShader(s_device, shader) == mrhi_success, "the noop shader destroyed");
}

// Reports a case past a limit of the adapter's as well as the device's
// whose outcome differs from the CTS's: mrhi_errorUnsupported, since no
// device of the adapter could ask for it.
static void ExpectPastAdapter(const char* test, mrhiResult result)
{
    if (result != mrhi_errorUnsupported)
    {
        printf("FAIL: %s: %d, the CTS expects a validation error past the adapter's limit\n", test,
               (int)result);
        ++s_failures;
    }
}

// A root block write of size bytes at offset, in a compute pass or a
// render pass of the suite's target, cleared, each of its own frame:
// the refusal, or success.
static mrhiResult WriteRootBlock(bool render, uint32_t offset, size_t size)
{
    static const uint8_t bytes[32] = {0};
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiPassDef def = mrhiDefaultPassDef();
    CHECK(mrhiDevice_BeginFrame(s_device, &frame) == mrhi_success, "a frame");
    def.neverCull = true;
    if (render)
    {
        CHECK(mrhiImportTexture(s_device, s_target, &def.colorTargets[0].resource) == mrhi_success,
              "the target");
        def.colorTargets[0].load = mrhi_loadClear;
        def.colorTargetCount = 1;
    }
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success &&
              mrhiDevice_CompileFrame(s_device) == mrhi_success &&
              mrhiPass_Begin(s_device, pass) == mrhi_success,
          "the pass begun");
    mrhiResult status = mrhiPass_SetRootBlock(s_device, pass, offset, bytes, size);
    status = status == mrhi_success ? mrhiPass_End(s_device, pass) : status;
    CHECK(mrhiDevice_DropFrame(s_device) == mrhi_success, "dropped");
    return status;
}

// The CTS's kTypedArrayBufferViewKeys but Float16Array, which its
// setImmediates cases filter out, with each one's element bytes.
static const struct
{
    const char* name;
    uint32_t bytes;
} s_typedArrays[] = {
    {"Uint8Array", 1},   {"Uint8ClampedArray", 1}, {"Uint16Array", 2},    {"Uint32Array", 4},
    {"Int8Array", 1},    {"Int16Array", 2},        {"Int32Array", 4},     {"Float32Array", 4},
    {"Float64Array", 8}, {"BigInt64Array", 8},     {"BigUint64Array", 8},
};

#define TYPED_ARRAYS (sizeof(s_typedArrays) / sizeof(s_typedArrays[0]))

static const char* const s_passTypes[] = {"compute pass", "render pass"};

// setImmediates:alignment: 8 bytes at 4 or 6, or 10 bytes at 4 for the
// arrays whose elements make 10. JavaScript throws on the 10, and the
// library refuses it.
static void TestImmediateAlignment(void)
{
    static const uint32_t ranges[3][2] = {{4, 8}, {6, 8}, {4, 10}};
    char name[128];
    for (uint32_t p = 0; p < 2; ++p)
    {
        for (size_t a = 0; a < TYPED_ARRAYS; ++a)
        {
            for (uint32_t r = 0; r < 3; ++r)
            {
                if (ranges[r][1] % s_typedArrays[a].bytes != 0)
                {
                    continue;
                }
                s_inexpressible += p == 0 ? 1 : 0;
                snprintf(name, sizeof(name), "setImmediates:alignment %s, %s, %u, %u",
                         s_passTypes[p], s_typedArrays[a].name, ranges[r][0], ranges[r][1]);
                Expect(name, WriteRootBlock(p == 1, ranges[r][0], ranges[r][1]),
                       ranges[r][0] % 4 == 0 && ranges[r][1] % 4 == 0);
            }
        }
    }
}

// setImmediates:overflow: 4 elements, or none, at 0; 4 elements at
// offsets that end past any root block; and data offsets past the
// array, which JavaScript throws on and C, passing a pointer and a
// size, cannot express.
static void TestImmediateOverflow(void)
{
    static const uint32_t offsets[4] = {0, 0, (UINT32_C(1) << 31) - 8, UINT32_MAX - 7};
    static const uint32_t counts[4] = {4, 0, 4, 4};
    char name[128];
    for (uint32_t p = 0; p < 2; ++p)
    {
        for (size_t a = 0; a < TYPED_ARRAYS; ++a)
        {
            // Its render bundle case, and its two cases past the array.
            s_inexpressible += (p == 0 ? 6 : 0) + 2;
            for (uint32_t c = 0; c < 4; ++c)
            {
                snprintf(name, sizeof(name), "setImmediates:overflow %s, %s, %u, 0, %u",
                         s_passTypes[p], s_typedArrays[a].name, offsets[c], counts[c]);
                mrhiResult status =
                    WriteRootBlock(p == 1, offsets[c], (size_t)counts[c] * s_typedArrays[a].bytes);
                if (c < 2)
                {
                    Expect(name, status, true);
                }
                else
                {
                    ExpectPastAdapter(name, status);
                }
            }
        }
    }
}

// setImmediates:out_of_bounds: 8 bytes, or one element of 8, ending at
// the device's root block or 4 bytes past it. Its data length one
// element short throws in JavaScript, which C cannot express.
static void TestImmediateBounds(void)
{
    char name[128];
    uint32_t most = s_limits.rootBlockBytes;
    for (uint32_t p = 0; p < 2; ++p)
    {
        for (size_t a = 0; a < TYPED_ARRAYS; ++a)
        {
            // Its render bundle cases, and its case of a short array.
            s_inexpressible += (p == 0 ? 3 : 0) + 1;
            for (uint32_t delta = 0; delta <= 4; delta += 4)
            {
                snprintf(name, sizeof(name), "setImmediates:out_of_bounds %s, %s, %u, 0",
                         s_passTypes[p], s_typedArrays[a].name, delta);
                ExpectPastLimit(name, WriteRootBlock(p == 1, most - 8 + delta, 8), delta == 0);
            }
        }
    }
}

// api,validation,encoding,cmds,setImmediates, whose setImmediates is
// mrhiPass_SetRootBlock. Each case runs in a compute pass and a render
// pass; its render bundle cases have no counterpart. The adapter's root
// block is above the device's, so a write past the device's is one a
// device asking for more would take.
static void TestSetImmediates(void)
{
    TestImmediateAlignment();
    TestImmediateOverflow();
    TestImmediateBounds();
}

// A pipeline of the CTS's getDescriptor, the suite's noop pipeline,
// with a multisample state, and for a depth case a depth32float target
// without depth writes, its fragment entry and color target left out
// when noFragment: the refusal, or success, destroying it.
static mrhiResult MakeMultisamplePipeline(mrhiShaderId shader, uint32_t count, bool alpha,
                                          bool depth, bool noFragment)
{
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    if (!noFragment)
    {
        def.fragmentEntry = "fs";
        def.fragmentEntryLength = 2;
        def.colorTargets[0].format = mrhi_formatRgba8Unorm;
        def.colorTargetCount = 1;
    }
    def.depthStencilFormat = depth ? mrhi_formatDepth32Float : mrhi_formatNone;
    def.sampleCount = count;
    def.alphaToCoverage = alpha;
    mrhiGraphicsPipelineId pipeline = {0};
    mrhiRequestId request;
    mrhiResult status = mrhiCreateGraphicsPipeline(s_device, &def, &pipeline, &request);
    if (status == mrhi_success)
    {
        Answered(request);
        CHECK(mrhiDestroyGraphicsPipeline(s_device, pipeline) == mrhi_success,
              "a multisample pipeline destroyed");
    }
    return status;
}

// api,validation,render_pipeline,multisample_state: count,
// alpha_to_coverage,count and alpha_to_coverage,fragment_stage_required,
// which the suite's noop container expresses. Its isAsync cases make
// the pipeline the library's one way, as primitive_state's. A count
// other than 1 or 4 that is a power of two is a capability the test
// device does not grant for rgba8unorm, mrhi_errorUnsupported; any
// other is invalid.
static void TestMultisampleState(void)
{
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_noopContainer;
    shaderDef.byteCount = sizeof(s_noopContainer);
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(s_device, &shaderDef, &shader) == mrhi_success, "the noop shader");
    static const char* const asyncs[] = {"false", "true"};
    static const uint32_t counts[] = {0, 1, 2, 3, 4, 8, 16, 1024};
    char name[128];
    for (uint32_t a = 0; a < 2; ++a)
    {
        for (size_t c = 0; c < sizeof(counts) / sizeof(counts[0]); ++c)
        {
            uint32_t count = counts[c];
            snprintf(name, sizeof(name), "multisample_state:count %s, %u", asyncs[a], count);
            mrhiResult status = MakeMultisamplePipeline(shader, count, false, false, false);
            bool power = count > 0 && count <= 16 && (count & (count - 1)) == 0;
            if (power && count != 1 && count != 4)
            {
                ExpectUngranted(name, status, false);
            }
            else
            {
                Expect(name, status, count == 1 || count == 4);
            }
        }
        for (uint32_t alpha = 0; alpha < 2; ++alpha)
        {
            for (uint32_t count = 1; count <= 4; count += 3)
            {
                snprintf(name, sizeof(name), "multisample_state:alpha_to_coverage,count %s, %s, %u",
                         asyncs[a], s_bools[alpha], count);
                Expect(name, MakeMultisamplePipeline(shader, count, alpha == 1, false, false),
                       alpha == 0 || count == 4);
            }
            for (uint32_t none = 0; none < 2; ++none)
            {
                snprintf(name, sizeof(name),
                         "multisample_state:alpha_to_coverage,fragment_stage_required %s, %s, %s",
                         asyncs[a], s_bools[alpha], s_bools[none]);
                Expect(name, MakeMultisamplePipeline(shader, 4, alpha == 1, true, none == 1),
                       alpha == 0 || none == 0);
            }
        }
    }
    CHECK(mrhiDestroyShader(s_device, shader) == mrhi_success, "the noop shader destroyed");
}

// A pipeline of the CTS's getDescriptor, the suite's noop pipeline,
// with the depth-stencil and primitive state of a def: the refusal, or
// success, destroying it.
static mrhiResult MakeDepthStencilPipeline(mrhiShaderId shader,
                                           const mrhiGraphicsPipelineDef* state)
{
    mrhiGraphicsPipelineDef def = *state;
    def.shader = shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    def.colorTargetCount = 1;
    mrhiGraphicsPipelineId pipeline = {0};
    mrhiRequestId request;
    mrhiResult status = mrhiCreateGraphicsPipeline(s_device, &def, &pipeline, &request);
    if (status == mrhi_success)
    {
        Answered(request);
        CHECK(mrhiDestroyGraphicsPipeline(s_device, pipeline) == mrhi_success,
              "a depth-stencil pipeline destroyed");
    }
    return status;
}

static const char* const s_compareFunctions[] = {
    "never", "less", "equal", "less-equal", "greater", "not-equal", "greater-equal", "always",
};

static const char* const s_stencilOperations[] = {
    "keep",           "zero",           "replace", "invert", "increment-clamp", "decrement-clamp",
    "increment-wrap", "decrement-wrap",
};

// depth_stencil_state:format: on each of the library's formats, as
// createTexture's tests over every format run, a pipeline takes only a
// depth or stencil one.
static void TestDepthStencilFormat(mrhiShaderId shader)
{
    char name[128];
    for (uint32_t a = 0; a < 2; ++a)
    {
        for (mrhiFormat format = RGBA8; format <= LAST_FORMAT; ++format)
        {
            mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
            def.depthStencilFormat = format;
            snprintf(name, sizeof(name), "depth_stencil_state:format %s, format %d", s_bools[a],
                     (int)format);
            Expect(name, MakeDepthStencilPipeline(shader, &def),
                   format == mrhi_formatDepth32Float || format == DEPTH);
        }
    }
}

// depthCompare_optional's outcome as the CTS works it out: compare 0 is
// always and 1 undefined; write 0 false, 1 true and 2 undefined; zeroed
// whether a face's depthFailOp is zero rather than keep.
static bool CompareOptional(const DepthStencilFormat* format, uint32_t compare, uint32_t write,
                            bool zeroed)
{
    bool succeeds = (write != 1 || format->depth) && (!zeroed || format->stencil);
    if (format->depth && (write == 2 || ((write == 1 || zeroed) && compare == 1)))
    {
        succeeds = false;
    }
    return succeeds;
}

// depthCompare_optional and depthWriteEnabled_optional. The C def has no
// undefined depthCompare or depthWriteEnabled: a case runs as the call
// that gives always and false, whose outcome is that of the CTS case it
// stands for, and differs where its own is not.
static void TestDepthOptional(mrhiShaderId shader)
{
    static const char* const compares[] = {"always", "undefined"};
    static const char* const writes[] = {"false", "true", "undefined"};
    static const char* const ops[] = {"keep", "zero"};
    char name[192];
    for (uint32_t a = 0; a < 2; ++a)
    {
        for (size_t f = 0; f < DEPTH_STENCIL_FORMATS; ++f)
        {
            const DepthStencilFormat* format = &s_depthStencilFormats[f];
            if (format->format == mrhi_formatNone)
            {
                s_inexpressible += 2 * 3 * 2 * 2 + 1;
                continue;
            }
            for (uint32_t c = 0; c < 2 * 3 * 2 * 2; ++c)
            {
                uint32_t compare = c / 12;
                uint32_t write = c / 4 % 3;
                uint32_t front = c / 2 % 2;
                uint32_t back = c % 2;
                mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
                def.depthStencilFormat = format->format;
                def.depthWrite = write == 1;
                def.stencilFront.depthFailOp = front == 1 ? mrhi_stencilZero : mrhi_stencilKeep;
                def.stencilBack.depthFailOp = back == 1 ? mrhi_stencilZero : mrhi_stencilKeep;
                bool zeroed = front == 1 || back == 1;
                bool valid = CompareOptional(format, 0, write == 1 ? 1 : 0, zeroed);
                snprintf(name, sizeof(name),
                         "depth_stencil_state:depthCompare_optional %s, %s, %s, %s, %s, %s",
                         s_bools[a], format->name, compares[compare], writes[write], ops[front],
                         ops[back]);
                Expect(name, MakeDepthStencilPipeline(shader, &def), valid);
                s_differs += valid != CompareOptional(format, compare, write, zeroed) ? 1 : 0;
            }
            mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
            def.depthStencilFormat = format->format;
            snprintf(name, sizeof(name), "depth_stencil_state:depthWriteEnabled_optional %s, %s",
                     s_bools[a], format->name);
            Expect(name, MakeDepthStencilPipeline(shader, &def), true);
            s_differs += format->depth ? 1 : 0;
        }
    }
}

// depth_test and depth_write: a depth test other than always, or a
// depth write, needs a depth format.
static void TestDepthAspect(mrhiShaderId shader, const DepthStencilFormat* format, uint32_t a)
{
    char name[160];
    for (uint32_t c = 0; c < 8; ++c)
    {
        mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
        def.depthStencilFormat = format->format;
        def.depthCompare = (mrhiCompareFunction)(mrhi_compareNever + c);
        snprintf(name, sizeof(name), "depth_stencil_state:depth_test %s, %s, %s", s_bools[a],
                 format->name, s_compareFunctions[c]);
        Expect(name, MakeDepthStencilPipeline(shader, &def),
               def.depthCompare == mrhi_compareAlways || format->depth);
    }
    for (uint32_t write = 0; write < 2; ++write)
    {
        mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
        def.depthStencilFormat = format->format;
        def.depthWrite = write == 1;
        snprintf(name, sizeof(name), "depth_stencil_state:depth_write %s, %s, %s", s_bools[a],
                 format->name, s_bools[write]);
        Expect(name, MakeDepthStencilPipeline(shader, &def), write == 0 || format->depth);
    }
}

// stencil_test and stencil_write: a face's stencil test other than
// always, or an operation other than keep, needs a stencil format. An
// undefined compare or operation is always or keep, the default of both
// WebGPU and the C def.
static void TestStencilAspect(mrhiShaderId shader, const DepthStencilFormat* format, uint32_t a)
{
    static const char* const faces[] = {"front", "back"};
    static const char* const opTypes[] = {"FailOp", "DepthFailOp", "PassOp"};
    char name[160];
    for (uint32_t face = 0; face < 2; ++face)
    {
        for (uint32_t c = 0; c < 9; ++c)
        {
            mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
            def.depthStencilFormat = format->format;
            mrhiStencilFace* state = face == 0 ? &def.stencilFront : &def.stencilBack;
            state->compare = c == 0 ? mrhi_compareAlways : (mrhiCompareFunction)c;
            snprintf(name, sizeof(name), "depth_stencil_state:stencil_test %s, %s, %s, %s",
                     s_bools[a], format->name, faces[face],
                     c == 0 ? "undefined" : s_compareFunctions[c - 1]);
            Expect(name, MakeDepthStencilPipeline(shader, &def),
                   state->compare == mrhi_compareAlways || format->stencil);
        }
        for (uint32_t type = 0; type < 3; ++type)
        {
            for (uint32_t op = 0; op < 9; ++op)
            {
                mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
                def.depthStencilFormat = format->format;
                mrhiStencilFace* state = face == 0 ? &def.stencilFront : &def.stencilBack;
                mrhiStencilOperation* field = type == 0   ? &state->failOp
                                              : type == 1 ? &state->depthFailOp
                                                          : &state->passOp;
                *field = op == 0 ? mrhi_stencilKeep : (mrhiStencilOperation)(op - 1);
                snprintf(name, sizeof(name), "depth_stencil_state:stencil_write %s, %s, %s%s, %s",
                         s_bools[a], format->name, faces[face], opTypes[type],
                         op == 0 ? "undefined" : s_stencilOperations[op - 1]);
                Expect(name, MakeDepthStencilPipeline(shader, &def),
                       *field == mrhi_stencilKeep || format->stencil);
            }
        }
    }
}

// depth_bias: a depth bias, by constant, slope or clamp, only for
// triangles. Its depth24plus becomes the library's depth-stencil format.
static void TestDepthBias(mrhiShaderId shader)
{
    static const char* const topologies[] = {"point-list", "line-list", "line-strip",
                                             "triangle-list", "triangle-strip"};
    static const char* const fields[] = {"depthBias", "depthBiasSlopeScale", "depthBiasClamp"};
    char name[160];
    for (uint32_t a = 0; a < 2; ++a)
    {
        for (uint32_t t = 0; t < 5; ++t)
        {
            for (uint32_t s = 0; s < 10; ++s)
            {
                mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
                def.topology = (mrhiPrimitiveTopology)t;
                def.depthStencilFormat = DEPTH;
                def.depthWrite = true;
                def.depthCompare = mrhi_compareLessEqual;
                int32_t value = s == 0 ? 0 : (int32_t)((s - 1) % 3) - 1;
                if (s >= 1 && s <= 3)
                {
                    def.depthBias = value;
                }
                else if (s >= 4 && s <= 6)
                {
                    def.depthBiasSlopeScale = (float)value;
                }
                else if (s >= 7)
                {
                    def.depthBiasClamp = (float)value;
                }
                if (s == 0)
                {
                    snprintf(name, sizeof(name), "depth_stencil_state:depth_bias %s, %s, {}",
                             s_bools[a], topologies[t]);
                }
                else
                {
                    snprintf(name, sizeof(name), "depth_stencil_state:depth_bias %s, %s, %s %d",
                             s_bools[a], topologies[t], fields[(s - 1) / 3], (int)value);
                }
                bool triangles = def.topology == mrhi_topologyTriangleList ||
                                 def.topology == mrhi_topologyTriangleStrip;
                Expect(name, MakeDepthStencilPipeline(shader, &def), value == 0 || triangles);
            }
        }
    }
}

// api,validation,render_pipeline,depth_stencil_state, on the suite's
// noop container. Its isAsync cases make the pipeline the library's one
// way, as primitive_state's. depth_write,frag_depth needs a fragment
// entry that writes depth, which no committed container has, so its
// cases are not counted: CheckDepthStencil already refuses that entry
// without a depth format.
static void TestDepthStencilState(void)
{
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_noopContainer;
    shaderDef.byteCount = sizeof(s_noopContainer);
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(s_device, &shaderDef, &shader) == mrhi_success, "the noop shader");
    TestDepthStencilFormat(shader);
    TestDepthOptional(shader);
    for (uint32_t a = 0; a < 2; ++a)
    {
        for (size_t f = 0; f < DEPTH_STENCIL_FORMATS; ++f)
        {
            const DepthStencilFormat* format = &s_depthStencilFormats[f];
            if (format->format == mrhi_formatNone)
            {
                // depth_test's 8 compares, depth_write's 2, stencil_test's
                // 2 faces of 9 and stencil_write's 6 face operations of 9.
                s_inexpressible += 8 + 2 + 2 * 9 + 6 * 9;
                continue;
            }
            TestDepthAspect(shader, format, a);
            TestStencilAspect(shader, format, a);
        }
    }
    TestDepthBias(shader);
    CHECK(mrhiDestroyShader(s_device, shader) == mrhi_success, "the noop shader destroyed");
}

// A vertex state as the CTS's vertex_state tests build it: the buffers
// up to the last one given, a hole a stride-0 buffer without
// attributes, and their attributes in one list, each buffer's together.
typedef struct VertexState
{
    mrhiVertexBufferLayout buffers[16];
    uint32_t bufferCount;
    mrhiVertexAttribute attributes[32];
    uint32_t attributeCount;
} VertexState;

// The CTS's kVertexFormats in its order, with their byte sizes. The
// library's formats follow the same order from mrhi_vertexUint8, and
// the last, the packed snorm format, has no counterpart.
static const struct
{
    const char* name;
    uint32_t bytes;
} s_vertexFormats[] = {
    {"uint8", 1},         {"uint8x2", 2},         {"uint8x4", 4},    {"sint8", 1},
    {"sint8x2", 2},       {"sint8x4", 4},         {"unorm8", 1},     {"unorm8x2", 2},
    {"unorm8x4", 4},      {"snorm8", 1},          {"snorm8x2", 2},   {"snorm8x4", 4},
    {"uint16", 2},        {"uint16x2", 4},        {"uint16x4", 8},   {"sint16", 2},
    {"sint16x2", 4},      {"sint16x4", 8},        {"unorm16", 2},    {"unorm16x2", 4},
    {"unorm16x4", 8},     {"snorm16", 2},         {"snorm16x2", 4},  {"snorm16x4", 8},
    {"float16", 2},       {"float16x2", 4},       {"float16x4", 8},  {"float32", 4},
    {"float32x2", 8},     {"float32x3", 12},      {"float32x4", 16}, {"uint32", 4},
    {"uint32x2", 8},      {"uint32x3", 12},       {"uint32x4", 16},  {"sint32", 4},
    {"sint32x2", 8},      {"sint32x3", 12},       {"sint32x4", 16},  {"unorm10-10-10-2", 4},
    {"unorm8x4-bgra", 4}, {"snorm10-10-10-2", 4},
};

#define VERTEX_FORMATS (sizeof(s_vertexFormats) / sizeof(s_vertexFormats[0]))

// Makes the state's buffers up to index, holes for those not yet given,
// and gives that one the stride.
static void SetVertexBuffer(VertexState* s, uint32_t index, uint32_t stride)
{
    CHECK(index < sizeof(s->buffers) / sizeof(s->buffers[0]), "a buffer within the state");
    while (s->bufferCount <= index)
    {
        s->buffers[s->bufferCount++] = (mrhiVertexBufferLayout){0, mrhi_stepVertex};
    }
    s->buffers[index].stride = stride;
}

// The CTS's addTestAttributes on a buffer, the last the state's list
// reached: extra float32 attributes at offset 0 on the lowest locations
// the skip mask leaves, then the test attribute, if one is given, at
// the start or the end of the buffer's attributes.
static void AddTestAttributes(VertexState* s, uint32_t buffer, const mrhiVertexAttribute* test,
                              bool atStart, uint32_t extra, uint32_t skip)
{
    CHECK(s->attributeCount + extra + 1 <= sizeof(s->attributes) / sizeof(s->attributes[0]),
          "attributes within the state");
    uint32_t location = 0;
    for (uint32_t added = 0; added < extra; ++location)
    {
        if (location < 32 && (skip & (1u << location)) != 0)
        {
            continue;
        }
        s->attributes[s->attributeCount++] =
            (mrhiVertexAttribute){buffer, location, mrhi_vertexFloat32, 0};
        ++added;
    }
    if (test == nullptr)
    {
        return;
    }
    uint32_t at = s->attributeCount;
    while (atStart && at > 0 && s->attributes[at - 1].buffer == buffer)
    {
        --at;
    }
    for (uint32_t i = s->attributeCount; i > at; --i)
    {
        s->attributes[i] = s->attributes[i - 1];
    }
    s->attributes[at] = *test;
    s->attributes[at].buffer = buffer;
    ++s->attributeCount;
}

// A pipeline of the CTS's vertex_state descriptor, the suite's noop
// pipeline, whose vertex entry has no inputs, with the state's buffers
// and attributes: the refusal, or success, destroying it.
static mrhiResult MakeVertexStatePipeline(mrhiShaderId shader, const VertexState* s)
{
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    def.colorTargetCount = 1;
    def.vertexBuffers = s->bufferCount > 0 ? s->buffers : nullptr;
    def.vertexBufferCount = s->bufferCount;
    def.vertexAttributes = s->attributeCount > 0 ? s->attributes : nullptr;
    def.vertexAttributeCount = s->attributeCount;
    mrhiGraphicsPipelineId pipeline = {0};
    mrhiRequestId request;
    mrhiResult status = mrhiCreateGraphicsPipeline(s_device, &def, &pipeline, &request);
    if (status == mrhi_success)
    {
        Answered(request);
        CHECK(mrhiDestroyGraphicsPipeline(s_device, pipeline) == mrhi_success,
              "a vertex state pipeline destroyed");
    }
    return status;
}

// max_vertex_buffer_limit and max_vertex_attribute_limit: 0, 1, the
// limit and one past it of buffers, the last with an attribute or not,
// and of attributes, spread 0, 1 or 4 to a buffer with the rest in the
// last buffer the limit allows.
static void TestVertexCountLimits(mrhiShaderId shader, const mrhiLimits* limits)
{
    char name[160];
    for (uint32_t v = 0; v < 8; ++v)
    {
        uint32_t count = v / 2 < 2 ? v / 2 : limits->vertexBuffers + v / 2 - 2;
        bool lastEmpty = v % 2 == 1;
        VertexState s = {0};
        for (uint32_t i = 0; i < count; ++i)
        {
            SetVertexBuffer(&s, i, 0);
        }
        if (count > 0 && !lastEmpty)
        {
            s.attributes[s.attributeCount++] =
                (mrhiVertexAttribute){count - 1, 0, mrhi_vertexFloat32, 0};
        }
        snprintf(name, sizeof(name),
                 "vertex_state:max_vertex_buffer_limit %u buffers, lastEmpty %s", count,
                 s_bools[lastEmpty]);
        ExpectPastLimit(name, MakeVertexStatePipeline(shader, &s), count <= limits->vertexBuffers);
    }
    static const uint32_t perBuffer[] = {0, 1, 4};
    for (uint32_t v = 0; v < 4 * 3; ++v)
    {
        uint32_t count = v / 3 < 2 ? v / 3 : limits->vertexAttributes + v / 3 - 2;
        uint32_t per = perBuffer[v % 3];
        VertexState s = {0};
        for (uint32_t added = 0, buffer = 0; added < count; ++buffer)
        {
            uint32_t target = added + per < count ? added + per : count;
            target = buffer == limits->vertexBuffers - 1 ? count : target;
            SetVertexBuffer(&s, buffer, 0);
            for (; added < target; ++added)
            {
                s.attributes[s.attributeCount++] =
                    (mrhiVertexAttribute){buffer, added, mrhi_vertexFloat32, 0};
            }
        }
        snprintf(name, sizeof(name), "vertex_state:max_vertex_attribute_limit %u, %u a buffer",
                 count, per);
        ExpectPastLimit(name, MakeVertexStatePipeline(shader, &s),
                        count <= limits->vertexAttributes);
    }
}

// The CTS's vertexBufferIndexVariant: the first and second buffer, and
// the last the limit allows.
static uint32_t VertexBufferIndex(const mrhiLimits* limits, uint32_t variant)
{
    return variant < 2 ? variant : limits->vertexBuffers - 1;
}

// max_vertex_buffer_array_stride_limit and
// vertex_buffer_array_stride_limit_alignment: one buffer, the first,
// the second or the last, of a stride under, at or past the limit, and
// of one not a multiple of 4.
static void TestVertexStrides(mrhiShaderId shader, const mrhiLimits* limits)
{
    char name[160];
    uint32_t most = limits->vertexStride;
    const uint32_t limited[] = {0, 4, 256, most - 4, most, most + 4};
    const uint32_t aligned[] = {0, 1, 2, 4, most - 4, most - 2, most};
    for (uint32_t b = 0; b < 3; ++b)
    {
        uint32_t index = VertexBufferIndex(limits, b);
        for (size_t i = 0; i < sizeof(limited) / sizeof(limited[0]); ++i)
        {
            VertexState s = {0};
            SetVertexBuffer(&s, index, limited[i]);
            snprintf(name, sizeof(name),
                     "vertex_state:max_vertex_buffer_array_stride_limit buffer %u, stride %u",
                     index, limited[i]);
            ExpectPastLimit(name, MakeVertexStatePipeline(shader, &s), limited[i] <= most);
        }
        for (size_t i = 0; i < sizeof(aligned) / sizeof(aligned[0]); ++i)
        {
            VertexState s = {0};
            SetVertexBuffer(&s, index, aligned[i]);
            snprintf(name, sizeof(name),
                     "vertex_state:vertex_buffer_array_stride_limit_alignment buffer %u, stride %u",
                     index, aligned[i]);
            Expect(name, MakeVertexStatePipeline(shader, &s), aligned[i] % 4 == 0);
        }
    }
}

// vertex_attribute_shaderLocation_limit: a float32 attribute at
// location 0, 1, the limit's last or the limit, among 0, 1 or all but
// one of the other locations, in a buffer of stride 256.
static void TestVertexLocationLimit(mrhiShaderId shader, const mrhiLimits* limits)
{
    char name[192];
    uint32_t most = limits->vertexAttributes;
    const uint32_t extras[] = {0, 1, most - 1};
    const uint32_t locations[] = {0, 1, most - 1, most};
    for (uint32_t c = 0; c < 3 * 3 * 2 * 4; ++c)
    {
        uint32_t index = VertexBufferIndex(limits, c / 24);
        uint32_t extra = extras[c / 8 % 3];
        bool atStart = c / 4 % 2 == 1;
        uint32_t location = locations[c % 4];
        VertexState s = {0};
        SetVertexBuffer(&s, index, 256);
        const mrhiVertexAttribute test = {index, location, mrhi_vertexFloat32, 0};
        AddTestAttributes(&s, index, &test, atStart, extra, location < 32 ? 1u << location : 0);
        snprintf(name, sizeof(name),
                 "vertex_state:vertex_attribute_shaderLocation_limit buffer %u, %u extra, %s, "
                 "location %u",
                 index, extra, atStart ? "at start" : "at end", location);
        ExpectPastLimit(name, MakeVertexStatePipeline(shader, &s), location < most);
    }
}

// vertex_attribute_shaderLocation_unique: attributes A and B, in the
// same buffer or two, each at the start or the end, at locations 0, 1, 7
// or the limit's last, with 0 or 4 others beside A; only one location
// apiece is valid.
static void TestVertexLocationUnique(mrhiShaderId shader, const mrhiLimits* limits)
{
    char name[224];
    const uint32_t locations[] = {0, 1, 7, limits->vertexAttributes - 1};
    for (uint32_t c = 0; c < 3 * 3 * 2 * 2 * 4 * 4 * 2; ++c)
    {
        uint32_t indexA = VertexBufferIndex(limits, c / 384);
        uint32_t indexB = VertexBufferIndex(limits, c / 128 % 3);
        bool startA = c / 64 % 2 == 1;
        bool startB = c / 32 % 2 == 1;
        uint32_t locationA = locations[c / 8 % 4];
        uint32_t locationB = locations[c / 2 % 4];
        uint32_t extra = c % 2 == 1 ? 4 : 0;
        VertexState s = {0};
        SetVertexBuffer(&s, indexA, 256);
        SetVertexBuffer(&s, indexB, 256);
        const mrhiVertexAttribute a = {indexA, locationA, mrhi_vertexFloat32, 0};
        const mrhiVertexAttribute b = {indexB, locationB, mrhi_vertexFloat32, 0};
        AddTestAttributes(&s, indexA, &a, startA, extra, (1u << locationA) | (1u << locationB));
        AddTestAttributes(&s, indexB, &b, startB, 0, 0);
        snprintf(name, sizeof(name),
                 "vertex_state:vertex_attribute_shaderLocation_unique A buffer %u, %s, location "
                 "%u; B buffer %u, %s, location %u; %u extra",
                 indexA, startA ? "at start" : "at end", locationA, indexB,
                 startB ? "at start" : "at end", locationB, extra);
        Expect(name, MakeVertexStatePipeline(shader, &s), locationA != locationB);
    }
}

// One offset case of a format's attribute at location 0, in a buffer
// of the stride, first, second or the limit's last, among 0, 1 or all
// but one of the other locations, at the start or the end: run as the
// CTS's subcases, each with the outcome given, past the stride limit
// when past is set; or counted when the format has no counterpart.
static void RunOffsetSubcases(mrhiShaderId shader, const mrhiLimits* limits, const char* test,
                              uint32_t f, uint32_t stride, uint32_t offset, bool valid, bool past)
{
    char name[224];
    const uint32_t extras[] = {0, 1, limits->vertexAttributes - 1};
    for (uint32_t c = 0; c < 3 * 3 * 2; ++c)
    {
        if (f + 1 > mrhi_vertexUnorm8x4Bgra)
        {
            ++s_inexpressible;
            continue;
        }
        uint32_t index = VertexBufferIndex(limits, c / 6);
        uint32_t extra = extras[c / 2 % 3];
        bool atStart = c % 2 == 1;
        VertexState s = {0};
        SetVertexBuffer(&s, index, stride);
        const mrhiVertexAttribute attribute = {index, 0, (mrhiVertexFormat)(f + 1), offset};
        AddTestAttributes(&s, index, &attribute, atStart, extra, 1u);
        snprintf(name, sizeof(name),
                 "vertex_state:%s %s, stride %u, offset %u, buffer %u, %u extra, %s", test,
                 s_vertexFormats[f].name, stride, offset, index, extra,
                 atStart ? "at start" : "at end");
        mrhiResult status = MakeVertexStatePipeline(shader, &s);
        if (past)
        {
            ExpectPastLimit(name, status, valid);
        }
        else
        {
            Expect(name, status, valid);
        }
    }
}

// vertex_attribute_offset_alignment: each format's attribute in a
// buffer of stride 256 or the limit, at offsets near 0 and near the
// stride's end, each (mult, add) pair once, as the CTS's
// filterUniqueValueTestVariants keeps them. An offset is aligned to
// the lesser of 4 and the format's size.
static void TestVertexOffsetAlignment(mrhiShaderId shader, const mrhiLimits* limits)
{
    const uint32_t strides[] = {256, limits->vertexStride};
    for (uint32_t f = 0; f < VERTEX_FORMATS; ++f)
    {
        int64_t size = s_vertexFormats[f].bytes;
        const int64_t variants[9][2] = {
            {0, 0},     {0, size / 2},         {0, size},      {0, 2},         {0, 4},
            {1, -size}, {1, -size - size / 2}, {1, -size - 4}, {1, -size - 2},
        };
        for (uint32_t st = 0; st < 2; ++st)
        {
            for (uint32_t v = 0; v < 9; ++v)
            {
                bool seen = false;
                for (uint32_t w = 0; w < v; ++w)
                {
                    seen = seen ||
                           (variants[w][0] == variants[v][0] && variants[w][1] == variants[v][1]);
                }
                if (seen)
                {
                    continue;
                }
                uint32_t offset = (uint32_t)(strides[st] * variants[v][0] + variants[v][1]);
                uint32_t alignment = size < 4 ? (uint32_t)size : 4;
                RunOffsetSubcases(shader, limits, "vertex_attribute_offset_alignment", f,
                                  strides[st], offset, offset % alignment == 0, false);
            }
        }
    }
}

// vertex_attribute_contained_in_stride: each format's attribute in a
// buffer of stride 0, 256, the limit's last multiple of 4 or the limit,
// ending inside the stride, at its end or past it. A stride of 0 holds
// an attribute ending within the stride limit, so one past it is past
// a device limit.
static void TestVertexContainedInStride(mrhiShaderId shader, const mrhiLimits* limits)
{
    uint32_t most = limits->vertexStride;
    const uint32_t strides[] = {0, 256, most - 4, most};
    for (uint32_t f = 0; f < VERTEX_FORMATS; ++f)
    {
        int64_t size = s_vertexFormats[f].bytes;
        const int64_t variants[6][2] = {{0, 0},         {0, 4},    {1, -size},
                                        {1, -size + 4}, {0, size}, {1, 0}};
        for (uint32_t st = 0; st < 4; ++st)
        {
            int64_t base = strides[st] != 0 ? strides[st] : most;
            for (uint32_t v = 0; v < (size != 4 ? 6u : 4u); ++v)
            {
                int64_t offset = base * variants[v][0] + variants[v][1];
                RunOffsetSubcases(shader, limits, "vertex_attribute_contained_in_stride", f,
                                  strides[st], (uint32_t)offset, offset + size <= base,
                                  strides[st] == 0);
            }
        }
    }
}

// many_attributes_overlapping: one attribute per location the limit
// allows, each 4 bytes after the last and 16 bytes wide, in a buffer of
// stride 0.
static void TestVertexOverlaps(mrhiShaderId shader, const mrhiLimits* limits)
{
    static const mrhiVertexFormat formats[] = {mrhi_vertexFloat32x4, mrhi_vertexUint32x4,
                                               mrhi_vertexSint32x4};
    VertexState s = {0};
    SetVertexBuffer(&s, 0, 0);
    for (uint32_t i = 0; i < limits->vertexAttributes; ++i)
    {
        s.attributes[s.attributeCount++] = (mrhiVertexAttribute){0, i, formats[i % 3], i * 4};
    }
    Expect("vertex_state:many_attributes_overlapping", MakeVertexStatePipeline(shader, &s), true);
}

// api,validation,render_pipeline,vertex_state: its tests whose vertex
// shader has no inputs, on the suite's noop container. The CTS's limits
// are the device's; the adapter's are above them, so a case past one is
// mrhi_errorCapacity. vertex_shader_input_location_limit,
// vertex_shader_input_location_in_vertex_state and
// vertex_shader_type_matches_attribute_format need vertex entries with
// inputs, which no committed container has, so their cases are not
// counted: CheckVertex already refuses a vertex input without an
// attribute of its scalar class.
static void TestVertexState(void)
{
    mrhiLimits limits = {0};
    CHECK(mrhiDevice_GetLimits(s_device, &limits) == mrhi_success, "the limits");
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_noopContainer;
    shaderDef.byteCount = sizeof(s_noopContainer);
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(s_device, &shaderDef, &shader) == mrhi_success, "the noop shader");
    TestVertexCountLimits(shader, &limits);
    TestVertexStrides(shader, &limits);
    TestVertexLocationLimit(shader, &limits);
    TestVertexLocationUnique(shader, &limits);
    TestVertexOffsetAlignment(shader, &limits);
    TestVertexContainedInStride(shader, &limits);
    TestVertexOverlaps(shader, &limits);
    CHECK(mrhiDestroyShader(s_device, shader) == mrhi_success, "the noop shader destroyed");
}

int main(void)
{
    ResetAdapter();
    // The compressed families, for their formats' layouts.
    s_adapter.features.textureCompressionBc = true;
    s_adapter.features.textureCompressionEtc2 = true;
    s_adapter.features.textureCompressionAstc = true;
    // Limits above the device's defaults, as a GPU's are, so that a case
    // past a device limit is one a device asking for more would take.
    s_adapter.limits.workgroupsPerDimension = UINT32_MAX;
    s_adapter.limits.vertexBuffers = 16;
    s_adapter.limits.vertexAttributes = 2 * mrhiDefaultLimits().vertexAttributes;
    s_adapter.limits.vertexStride = 2 * mrhiDefaultLimits().vertexStride;
    s_adapter.limits.textureDimension2d = 16384;
    s_adapter.limits.textureDimension3d = 4096;
    s_adapter.limits.textureArrayLayers = 2048;
    s_adapter.limits.bufferBytes = 2 * mrhiDefaultLimits().bufferBytes;
    s_adapter.limits.rootBlockBytes = 2 * mrhiDefaultLimits().rootBlockBytes;
    // r32float multisampled, as WebGPU allows and many GPUs report, so that
    // a format that multisamples without resolving is there to resolve.
    s_adapter.limitedFormat = mrhi_formatR32Float;
    s_adapter.limitedCaps = (mrhiFormatCaps){
        .sampling = true,
        .rendering = true,
        .storage = true,
        .sampleCounts = mrhi_samples1Bit | mrhi_samples4Bit,
    };
    s_adapter.limits.colorBytesPerSample = 128;
    // Timestamps, for the query sets of each of the CTS's types.
    s_adapter.features.timestampQuery = true;
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.features = s_adapter.features;
    s_device = OpenWith(deviceDef, true);
    TestBufferCopies();
    TestClears();
    TestTextureCopies();
    TestLayouts();
    // The counts are pinned so that a change of either is seen.
    CHECK(s_differs == 192 && s_inexpressible == 0, "the layout cases set aside");
    TestTextureRelated();
    CHECK(s_differs == 876 && s_inexpressible == 0, "the texture copy cases set aside");
    MakePipelines();
    TestIndirectDraws();
    CHECK(s_differs == 876 && s_inexpressible == 0, "the indirect draw cases set aside");
    TestDispatches();
    TestVertexIndexBuffers();
    CHECK(s_differs == 878 && s_inexpressible == 0, "the draw cases set aside");
    TestRenderPasses();
    TestTransientTargets();
    TestDepthOps();
    TestViewUsages();
    TestTransientTextures();
    TestZeroSizes();
    TestMipCounts();
    TestMipBounds();
    TestDimensionFormats();
    TestSampleCounts();
    TestSampleShapes();
    TestSampledKinds();
    TestTextureUsages();
    TestUsageBits();
    TestViewFormatLists();
    TestSmallestSizes();
    TestLimitSizes();
    TestViewFormats();
    TestViewAspects();
    TestViewLayers();
    TestViewMips();
    TestViewDimensionCases();
    TestViewStates();
    TestViewUsageSubsets();
    TestViewUsageFormats();
    TestSamplers();
    TestBuffers();
    TestColorBytes();
    TestQuerySets();
    TestQueries();
    CHECK(s_differs == 2058 && s_inexpressible == 7048, "the pass and query cases set aside");
    TestBufferDestroys();
    TestTextureDestroys();
    TestDestroyedTargets();
    TestQuerySetDestroys();
    CHECK(s_differs == 2064 && s_inexpressible == 7079, "the destroy cases set aside");
    TestWriteBuffers();
    CHECK(s_differs == 2064 && s_inexpressible == 7141, "the write cases set aside");
    TestQueueDestroys();
    CHECK(s_differs == 2086 && s_inexpressible == 7151, "the queue destroy cases set aside");
    TestCopyBuffers();
    TestBytesPerRowAlignments();
    CHECK(s_differs == 2086 && s_inexpressible == 7641, "the buffer copy cases set aside");
    TestDepthCopies();
    TestCopyUsages();
    TestOffsetRows();
    CHECK(s_differs == 2086 && s_inexpressible == 8317, "the buffer texture copy cases set aside");
    TestWriteTextures();
    CHECK(s_differs == 2086 && s_inexpressible == 8318, "the writeTexture cases set aside");
    TestViewports();
    TestScissors();
    CHECK(s_differs == 2086 && s_inexpressible == 8322, "the dynamic state cases set aside");
    TestDebugGroups();
    CHECK(s_differs == 2104 && s_inexpressible == 8341, "the debug group cases set aside");
    TestSetPipeline();
    CHECK(s_differs == 2104 && s_inexpressible == 8346, "the setPipeline cases set aside");
    TestStateTracking();
    CHECK(s_differs == 2104 && s_inexpressible == 8346, "the state tracking cases set aside");
    TestIndexAccess();
    CHECK(s_differs == 2104 && s_inexpressible == 8346, "the index access cases set aside");
    TestBeginComputePass();
    CHECK(s_differs == 2104 && s_inexpressible == 8347, "the beginComputePass cases set aside");
    TestDrawIndexBuffers();
    CHECK(s_differs == 2104 && s_inexpressible == 8347, "the draw index buffer cases set aside");
    TestDrawVertexBuffers();
    CHECK(s_differs == 2120 && s_inexpressible == 8391, "the draw vertex buffer cases set aside");
    TestPrimitiveState();
    CHECK(s_differs == 2120 && s_inexpressible == 8391, "the primitive_state cases set aside");
    TestSetImmediates();
    CHECK(s_differs == 2120 && s_inexpressible == 8583, "the setImmediates cases set aside");
    TestMultisampleState();
    CHECK(s_differs == 2120 && s_inexpressible == 8583, "the multisample_state cases set aside");
    TestDepthStencilState();
    CHECK(s_differs == 2160 && s_inexpressible == 9439, "the depth_stencil_state cases set aside");
    TestVertexState();
    CHECK(s_differs == 2160 && s_inexpressible == 9943, "the vertex_state cases set aside");
    Close(s_device);
    TestBindGroupsPlusVertexBuffers();
    return s_failures == 0 ? 0 : 1;
}
