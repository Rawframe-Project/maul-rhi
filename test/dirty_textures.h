// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// New textures a native suite fills with nonzero bytes through its own
// API before their first frame, which must still read as zeros there:
// one for each way a driver clears a new texture, one larger than a
// driver's zeros, and compressed ones whose smaller mips are not whole
// blocks. Fresh memory reads as zeros on many devices, so only
// bytes the program wrote first show a clear a driver left out.

#ifndef MAUL_RHI_TEST_DIRTY_TEXTURES_H
#define MAUL_RHI_TEST_DIRTY_TEXTURES_H

#include "test_harness.h"

#include "maul-rhi/capabilities.h"
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

#define DIRTY_COMPRESSED 4

// The compressed textures, whose blocks are 4 by 4 texels: 20 by 12 at
// the top, so that their second and third mips, 10 by 6 and 5 by 3, are
// not whole blocks. BC1 takes 8 bytes a block, and ETC2 and ASTC 16.
// BC7, of 16 bytes too, is two layers of 260 by 12: its rows of 1040
// bytes, and 528 and 272 at its mips of 130 and 65 texels, are longer
// than one of D3D12's 256-byte pitch units, so its copies take pitches
// padded to 1280, 768 and 512 bytes. A suite makes those its device has
// the feature for. Inline, as a suite without compressed textures
// takes neither this nor DirtyGranted.
static inline void DirtyCompressedTextures(DirtyTexture textures[DIRTY_COMPRESSED])
{
    textures[0] = DirtyTextureOf("BC1 mips", mrhi_formatBc1RgbaUnorm, mrhi_texture2d, 20, 12, 1, 3,
                                 mrhi_textureSampled);
    textures[1] = DirtyTextureOf("BC7 mips and layers", mrhi_formatBc7RgbaUnorm,
                                 mrhi_texture2dArray, 260, 12, 2, 3, mrhi_textureSampled);
    textures[2] = DirtyTextureOf("ETC2 mips", mrhi_formatEtc2Rgba8Unorm, mrhi_texture2d, 20, 12, 1,
                                 3, mrhi_textureSampled);
    textures[3] = DirtyTextureOf("ASTC 4x4 mips", mrhi_formatAstc4x4Unorm, mrhi_texture2d, 20, 12,
                                 1, 3, mrhi_textureSampled);
}

// Whether the device has the feature a texture's format needs.
static inline bool DirtyGranted(const mrhiTextureDef* def, const mrhiFeatures* features)
{
    switch (def->format)
    {
    case mrhi_formatBc1RgbaUnorm:
    case mrhi_formatBc7RgbaUnorm:
        return features->textureCompressionBc;
    case mrhi_formatEtc2Rgba8Unorm:
        return features->textureCompressionEtc2;
    case mrhi_formatAstc4x4Unorm:
        return features->textureCompressionAstc;
    default:
        return true;
    }
}

// A format's block of the check: its texels across and down, and its
// bytes; a texel of 4 bytes for the uncompressed formats.
static void DirtyBlock(mrhiFormat format, uint32_t* texelsOut, uint32_t* bytesOut)
{
    bool compressed = format == mrhi_formatBc1RgbaUnorm || format == mrhi_formatBc7RgbaUnorm ||
                      format == mrhi_formatEtc2Rgba8Unorm || format == mrhi_formatAstc4x4Unorm;
    *texelsOut = compressed ? 4 : 1;
    *bytesOut = format == mrhi_formatBc1RgbaUnorm ? 8 : compressed ? 16 : 4;
}

#define DIRTY_REGIONS 4

// The parts read: each mip's last layer or depth slice, whole, and the
// last mip's first layer, or the first and last rows of a large
// texture.
static uint32_t DirtyRegions(const mrhiTextureDef* def, DirtyRegion regions[DIRTY_REGIONS])
{
    if (def->height > 64)
    {
        regions[0] = (DirtyRegion){.rows = 1};
        regions[1] = (DirtyRegion){.y = def->height - 1, .rows = 1};
        return 2;
    }
    uint32_t count = 0;
    for (uint32_t mip = 0; mip < def->mipLevels && count < DIRTY_REGIONS - 1; ++mip)
    {
        uint32_t depth = def->depthOrLayers >> mip > 0 ? def->depthOrLayers >> mip : 1;
        uint32_t slices = def->kind == mrhi_texture3d ? depth : def->depthOrLayers;
        uint32_t rows = def->height >> mip > 0 ? def->height >> mip : 1;
        regions[count++] = (DirtyRegion){.mip = mip, .z = slices - 1, .rows = rows};
    }
    if (def->kind != mrhi_texture3d && def->depthOrLayers > 1)
    {
        uint32_t last = def->mipLevels - 1;
        uint32_t rows = def->height >> last > 0 ? def->height >> last : 1;
        regions[count++] = (DirtyRegion){.mip = last, .rows = rows};
    }
    return count;
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
    DirtyRegion regions[DIRTY_REGIONS];
    uint32_t count = DirtyRegions(def, regions);
    mrhiRequestId requests[DIRTY_REGIONS] = {{0}};
    size_t sizes[DIRTY_REGIONS] = {0};
    bool depth = def->format == mrhi_formatDepth32Float;
    uint32_t block = 1;
    uint32_t blockBytes = 4;
    DirtyBlock(def->format, &block, &blockBytes);
    for (uint32_t i = 0; i < count; ++i)
    {
        const DirtyRegion* region = &regions[i];
        // A mip of a compressed texture is copied in whole blocks.
        uint32_t width = def->width >> region->mip > 0 ? def->width >> region->mip : 1;
        uint32_t across = (width + block - 1) / block;
        uint32_t down = (region->rows + block - 1) / block;
        const mrhiTextureCopy source = {
            .resource = resource,
            .mip = region->mip,
            .y = region->y,
            .z = region->z,
            .aspect = depth ? mrhi_aspectDepthOnly : mrhi_aspectAll,
        };
        const mrhiExtent3d extent = {across * block, down * block, 1};
        sizes[i] = (size_t)across * down * blockBytes;
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
