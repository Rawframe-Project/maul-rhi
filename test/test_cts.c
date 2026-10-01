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

int main(void)
{
    ResetAdapter();
    s_device = OpenWith(mrhiDefaultDeviceDef(), true);
    TestBufferCopies();
    TestTextureCopies();
    Close(s_device);
    return s_failures == 0 ? 0 : 1;
}
