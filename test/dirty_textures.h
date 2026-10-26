// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// New textures a native suite fills with nonzero bytes through its own
// API before their first frame, which must still read as zeros there:
// one for each way a driver clears a new texture, and one larger than a
// driver's zeros. Fresh memory reads as zeros on many devices, so only
// bytes the program wrote first show a clear a driver left out.

#ifndef MAUL_RHI_TEST_DIRTY_TEXTURES_H
#define MAUL_RHI_TEST_DIRTY_TEXTURES_H

#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/resources.h"

#include <string.h>

#define DIRTY_TEXTURES 7

// A texture of the check, and what it is.
typedef struct DirtyTexture
{
    const char* what;
    mrhiTextureDef def;
} DirtyTexture;

static DirtyTexture DirtyTextureOf(const char* what, mrhiFormat format, mrhiTextureKind kind,
                                   uint32_t width, uint32_t height, uint32_t depthOrLayers,
                                   uint32_t mips, mrhiTextureUsage usage)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = format;
    def.kind = kind;
    def.width = width;
    def.height = height;
    def.depthOrLayers = depthOrLayers;
    def.mipLevels = mips;
    def.usage = usage | mrhi_textureCopySource;
    return (DirtyTexture){.what = what, .def = def};
}

// The textures: each 4 bytes a texel.
static void DirtyTextures(DirtyTexture textures[DIRTY_TEXTURES])
{
    textures[0] = DirtyTextureOf("a render target", mrhi_formatRgba8Unorm, mrhi_texture2d, 16, 16,
                                 1, 1, mrhi_textureRenderTarget);
    textures[1] = DirtyTextureOf("a depth target", mrhi_formatDepth32Float, mrhi_texture2d, 16, 16,
                                 1, 1, mrhi_textureRenderTarget);
    textures[2] = DirtyTextureOf("a depth texture only sampled", mrhi_formatDepth32Float,
                                 mrhi_texture2d, 16, 16, 1, 1, mrhi_textureSampled);
    textures[3] = DirtyTextureOf("storage mips and layers", mrhi_formatR32Float,
                                 mrhi_texture2dArray, 16, 16, 2, 2, mrhi_textureStorage);
    textures[4] = DirtyTextureOf("sampled mips and layers", mrhi_formatRgba8Unorm,
                                 mrhi_texture2dArray, 16, 16, 2, 2, mrhi_textureSampled);
    textures[5] = DirtyTextureOf("a volume render target", mrhi_formatRgba8Unorm, mrhi_texture3d,
                                 16, 16, 4, 2, mrhi_textureRenderTarget);
    textures[6] = DirtyTextureOf("rows past the zeros", mrhi_formatRgba8Unorm, mrhi_texture2d, 1024,
                                 512, 1, 1, mrhi_textureSampled);
}

// A part of a texture the check reads: a mip, a layer or depth slice,
// and rows of it.
typedef struct DirtyRegion
{
    uint32_t mip;
    uint32_t z;
    uint32_t y;
    uint32_t rows;
} DirtyRegion;

// The parts read: the first mip's last layer or slice and the last
// mip's first, whole, or the first and last rows of a large texture.
static uint32_t DirtyRegions(const mrhiTextureDef* def, DirtyRegion regions[2])
{
    if (def->height > 64)
    {
        regions[0] = (DirtyRegion){.rows = 1};
        regions[1] = (DirtyRegion){.y = def->height - 1, .rows = 1};
        return 2;
    }
    regions[0] = (DirtyRegion){.z = def->depthOrLayers - 1, .rows = def->height};
    uint32_t last = def->mipLevels - 1;
    regions[1] = (DirtyRegion){.mip = last, .rows = def->height >> last};
    return 2;
}

// Runs the texture's first frame, which reads those parts back: whether
// each reads as zeros.
static bool ReadsZerosFirst(mrhiDevice* device, mrhiTextureId texture, const mrhiTextureDef* def)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId resource = {0};
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
              mrhiImportTexture(device, texture, &resource) == mrhi_success,
          "the texture's first frame");
    mrhiAccess read = {
        .resource = resource,
        .kind = mrhi_accessCopySource,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
    mrhiPassDef pass = mrhiDefaultPassDef();
    pass.passClass = mrhi_passTransfer;
    pass.accesses = &read;
    pass.accessCount = 1;
    pass.neverCull = true;
    mrhiPassId back = {0};
    CHECK(mrhiAddPass(device, &pass, &back) == mrhi_success &&
              mrhiCompileFrame(device) == mrhi_success &&
              mrhiBeginPass(device, back) == mrhi_success,
          "a pass reading it");
    DirtyRegion regions[2];
    uint32_t count = DirtyRegions(def, regions);
    mrhiRequestId requests[2] = {{0}, {0}};
    size_t sizes[2] = {0, 0};
    bool depth = def->format == mrhi_formatDepth32Float;
    for (uint32_t i = 0; i < count; ++i)
    {
        const DirtyRegion* region = &regions[i];
        const mrhiTextureCopy source = {
            .resource = resource,
            .mip = region->mip,
            .y = region->y,
            .z = region->z,
            .aspect = depth ? mrhi_aspectDepthOnly : mrhi_aspectAll,
        };
        const mrhiExtent3d extent = {def->width >> region->mip, region->rows, 1};
        sizes[i] = (size_t)extent.width * extent.height * 4;
        CHECK(mrhiReadTexture(device, back, &source, &extent, &requests[i]) == mrhi_success,
              "a part read");
    }
    mrhiRequestId token = {0};
    CHECK(mrhiEndPass(device, back) == mrhi_success &&
              mrhiSubmitFrame(device, &token) == mrhi_success &&
              mrhiWaitFrame(device, token, UINT64_C(10000000000)) == mrhi_success,
          "read back and finished");
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(device, &record) == mrhi_success)
    {
    }
    static const uint8_t zeros[4096];
    static uint8_t taken[4096];
    bool zeroed = true;
    for (uint32_t i = 0; i < count; ++i)
    {
        size_t size = 0;
        zeroed =
            mrhiTakeReadback(device, requests[i], taken, sizeof(taken), &size) == mrhi_success &&
            size == sizes[i] && memcmp(taken, zeros, size) == 0 && zeroed;
    }
    return zeroed;
}

#endif // MAUL_RHI_TEST_DIRTY_TEXTURES_H
