// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A native pass on Metal (maul-rhi/metal.h): a texture uploaded by the
// library, overwritten by the program's own command buffer blitting in
// texels of its own, and read back by the library, the program's texels
// coming back; new textures filled with ones through the device's queue
// before their first frame reading as zeros in it, whichever clear Metal
// takes; the device's native objects, the refusals, and the usage flags
// of the textures the library makes. Skipped
// where there is no Metal adapter, unless MAUL_RHI_REQUIRE_METAL is set.

#include "dirty_textures.h"
#include "test_harness.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/metal.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/vulkan.h"

#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The texture's side.
#define SIZE 16

static mrhiAccess Whole(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
}

// The program's command buffer, from the device's queue: a blit of green
// texels from a buffer of its own into the texture. Neither committed.
static id<MTLCommandBuffer> Record(id<MTLDevice> device, id<MTLCommandQueue> queue,
                                   id<MTLTexture> texture, id<MTLBuffer>* bufferOut)
{
    uint8_t texels[SIZE * SIZE * 4];
    for (size_t i = 0; i < sizeof(texels); i += 4)
    {
        texels[i] = 0;
        texels[i + 1] = 255;
        texels[i + 2] = 0;
        texels[i + 3] = 255;
    }
    id<MTLBuffer> buffer = [device newBufferWithBytes:texels
                                               length:sizeof(texels)
                                              options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> commands = [[queue commandBuffer] retain];
    id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
    [blit copyFromBuffer:buffer
               sourceOffset:0
          sourceBytesPerRow:SIZE * 4
        sourceBytesPerImage:SIZE * SIZE * 4
                 sourceSize:MTLSizeMake(SIZE, SIZE, 1)
                  toTexture:texture
           destinationSlice:0
           destinationLevel:0
          destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    *bufferOut = buffer;
    return commands;
}

// The frame: an upload, the program's blit, a readback; the program's
// texels must come back.
static void CheckFrame(mrhiDevice* device, mrhiTextureId texture, id<MTLCommandBuffer> commands)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId resource = {0};
    CHECK(mrhiDevice_BeginFrame(device, &frame) == mrhi_success &&
              mrhiImportTexture(device, texture, &resource) == mrhi_success,
          "a frame with the texture");
    mrhiAccess write = Whole(resource, mrhi_accessCopyDestination);
    mrhiAccess read = Whole(resource, mrhi_accessCopySource);
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.accessCount = 1;
    def.accesses = &write;
    mrhiPassId upload = {0};
    mrhiPassId native = {0};
    mrhiPassId back = {0};
    CHECK(mrhiAddPass(device, &def, &upload) == mrhi_success, "the upload");
    def.native = true;
    CHECK(mrhiAddPass(device, &def, &native) == mrhi_success, "the native pass");
    def.native = false;
    def.accesses = &read;
    CHECK(mrhiAddPass(device, &def, &back) == mrhi_success, "the readback");
    uint8_t texels[SIZE * SIZE * 4];
    memset(texels, 0x40, sizeof(texels));
    const mrhiTextureCopy whole = {.resource = resource};
    const mrhiTexelLayout layout = {.bytesPerRow = SIZE * 4, .rowsPerImage = SIZE};
    const mrhiExtent3d extent = {SIZE, SIZE, 1};
    mrhiRequestId bytes = {0};
    mrhiRequestId token = {0};
    void* buffer = (void*)commands;
    CHECK(mrhiDevice_CompileFrame(device) == mrhi_success &&
              mrhiPass_Begin(device, upload) == mrhi_success &&
              mrhiPass_WriteTexture(device, upload, &whole, texels, sizeof(texels), &layout,
                                    &extent) == mrhi_success &&
              mrhiPass_End(device, upload) == mrhi_success,
          "uploaded");
    CHECK(mrhiPass_Begin(device, native) == mrhi_success &&
              mrhiPass_SetMetalCommands(device, native, nullptr) == mrhi_errorInvalid &&
              mrhiPass_SetVulkanCommands(device, native, buffer) == mrhi_errorUnsupported &&
              mrhiPass_SetMetalCommands(device, native, buffer) == mrhi_success &&
              mrhiPass_SetMetalCommands(device, native, buffer) == mrhi_errorState &&
              mrhiPass_End(device, native) == mrhi_success,
          "the native pass given its command buffer");
    CHECK(mrhiPass_Begin(device, back) == mrhi_success &&
              mrhiPass_ReadTexture(device, back, &whole, &extent, &bytes) == mrhi_success &&
              mrhiPass_End(device, back) == mrhi_success &&
              mrhiDevice_SubmitFrame(device, &token) == mrhi_success &&
              mrhiRequest_WaitFrame(device, token, UINT64_C(10000000000)) == mrhi_success,
          "read back and finished");
    CHECK(commands.status == MTLCommandBufferStatusCompleted,
          "the program's command buffer done with the frame");
    mrhiDeviceNotification record;
    while (mrhiDevice_TakeNotification(device, &record) == mrhi_success)
    {
    }
    uint8_t taken[sizeof(texels)] = {0};
    size_t size = 0;
    bool copied =
        mrhiRequest_TakeReadback(device, bytes, taken, sizeof(taken), &size) == mrhi_success &&
        size == sizeof(taken);
    for (size_t i = 0; copied && i < sizeof(taken); i += 4)
    {
        copied = taken[i] == 0 && taken[i + 1] == 255 && taken[i + 2] == 0 && taken[i + 3] == 255;
    }
    CHECK(copied, "the program's texels, after the upload and before the readback");
}

// Fills every mip and layer of a new texture with ones through the
// device's queue, and waits: a depth texture by render passes clearing
// it to 1, which every one takes, any other by blits of ones bytes, a
// compressed one's rows of blocks at once.
static bool Dirty(id<MTLDevice> device, id<MTLCommandQueue> queue, id<MTLTexture> texture,
                  const mrhiTextureDef* def)
{
    bool volume = def->kind == mrhi_texture3d;
    NSUInteger bytes = (NSUInteger)def->width * def->height * def->depthOrLayers * 4;
    id<MTLBuffer> ones = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    if (ones == nil)
    {
        return false;
    }
    memset(ones.contents, 0xFF, bytes);
    id<MTLCommandBuffer> commands = [queue commandBuffer];
    for (uint32_t mip = 0; mip < def->mipLevels; ++mip)
    {
        uint32_t width = def->width >> mip > 0 ? def->width >> mip : 1;
        uint32_t height = def->height >> mip > 0 ? def->height >> mip : 1;
        uint32_t depth = volume ? def->depthOrLayers >> mip : 1;
        for (uint32_t layer = 0; layer < (volume ? 1 : def->depthOrLayers); ++layer)
        {
            if (def->format == mrhi_formatDepth32Float)
            {
                MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
                pass.depthAttachment.texture = texture;
                pass.depthAttachment.level = mip;
                pass.depthAttachment.slice = layer;
                pass.depthAttachment.loadAction = MTLLoadActionClear;
                pass.depthAttachment.storeAction = MTLStoreActionStore;
                pass.depthAttachment.clearDepth = 1.0;
                [[commands renderCommandEncoderWithDescriptor:pass] endEncoding];
                continue;
            }
            uint32_t block = 1;
            uint32_t blockBytes = 4;
            DirtyBlock(def->format, &block, &blockBytes);
            uint32_t rowBytes = (width + block - 1) / block * blockBytes;
            id<MTLBlitCommandEncoder> blit = [commands blitCommandEncoder];
            [blit copyFromBuffer:ones
                       sourceOffset:0
                  sourceBytesPerRow:rowBytes
                sourceBytesPerImage:volume ? rowBytes * height : 0
                         sourceSize:MTLSizeMake(width, height, depth)
                          toTexture:texture
                   destinationSlice:layer
                   destinationLevel:mip
                  destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit endEncoding];
        }
    }
    [commands commit];
    [commands waitUntilCompleted];
    bool done = commands.status == MTLCommandBufferStatusCompleted;
    [ones release];
    return done;
}

// A new texture, filled with ones before its first frame, reads as
// zeros in it.
static void CheckDirty(mrhiDevice* device, id<MTLDevice> native, id<MTLCommandQueue> queue,
                       const DirtyTexture* dirty)
{
    mrhiTextureId texture = {0};
    void* made = nullptr;
    @autoreleasepool
    {
        CHECK(mrhiCreateTexture(device, &dirty->def, &texture) == mrhi_success &&
                  mrhiTexture_GetMetalTexture(device, texture, &made) == mrhi_success &&
                  Dirty(native, queue, (id<MTLTexture>)made, &dirty->def),
              dirty->what);
    }
    CHECK(ReadsZerosFirst(device, texture, &dirty->def), dirty->what);
    CHECK(mrhiDestroyTexture(device, texture) == mrhi_success, "ended");
}

// Each new texture, compressed ones of the formats the device has among
// them, filled with ones before its first frame, reads as zeros in it.
static void CheckDirtyTextures(mrhiDevice* device, id<MTLDevice> native, id<MTLCommandQueue> queue,
                               const mrhiFeatures* features)
{
    DirtyTexture textures[DIRTY_TEXTURES];
    DirtyTextures(textures);
    for (int i = 0; i < DIRTY_TEXTURES; ++i)
    {
        CheckDirty(device, native, queue, &textures[i]);
    }
    DirtyTexture compressed[DIRTY_COMPRESSED];
    DirtyCompressedTextures(compressed);
    for (int i = 0; i < DIRTY_COMPRESSED; ++i)
    {
        if (!DirtyGranted(&compressed[i].def, features))
        {
            printf("skip: %s, without the feature\n", compressed[i].what);
            continue;
        }
        CheckDirty(device, native, queue, &compressed[i]);
    }
}

// One texture usage and the MTLTextureUsage the shader access it allows
// needs, which Metal's texture usage validation would check at run time.
typedef struct UsageCase
{
    mrhiTextureUsage usage;
    mrhiFormat format;
    mrhiFormat viewFormat;
    MTLTextureUsage needed;
    const char* what;
} UsageCase;

// Each texture the library makes carries the usage flags its accesses
// need: the core refuses a binding or attachment without the usage its
// kind takes, so these flags cover every access a frame can make. This
// holds where Metal's own texture usage check cannot run, on the macOS 26
// runner whose check reports a mismatch on every texture (#9).
static void CheckUsages(mrhiDevice* device)
{
    static const UsageCase cases[] = {
        {mrhi_textureSampled, mrhi_formatRgba8Unorm, mrhi_formatNone, MTLTextureUsageShaderRead,
         "sampled: shader reads"},
        {mrhi_textureStorage, mrhi_formatRgba8Unorm, mrhi_formatNone,
         MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite,
         "storage: shader reads and writes"},
        {mrhi_textureRenderTarget, mrhi_formatRgba8Unorm, mrhi_formatNone,
         MTLTextureUsageRenderTarget, "a render target"},
        {mrhi_textureSampled | mrhi_textureRenderTarget, mrhi_formatRgba8Unorm, mrhi_formatNone,
         MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget, "sampled and a render target"},
        {mrhi_textureSampled, mrhi_formatRgba8Unorm, mrhi_formatRgba8UnormSrgb,
         MTLTextureUsageShaderRead | MTLTextureUsagePixelFormatView, "a view of another format"},
        {mrhi_textureRenderTarget, mrhi_formatDepthStencil, mrhi_formatNone,
         MTLTextureUsageRenderTarget | MTLTextureUsagePixelFormatView,
         "depth and stencil, viewed by aspect"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        mrhiTextureDef def = mrhiDefaultTextureDef();
        def.format = cases[i].format;
        def.width = SIZE;
        def.height = SIZE;
        def.usage = cases[i].usage;
        def.viewFormats[0] = cases[i].viewFormat;
        mrhiTextureId texture = {0};
        void* made = nullptr;
        bool created = mrhiCreateTexture(device, &def, &texture) == mrhi_success &&
                       mrhiTexture_GetMetalTexture(device, texture, &made) == mrhi_success &&
                       made != nullptr;
        MTLTextureUsage usage = created ? ((id<MTLTexture>)made).usage : 0;
        CHECK(created && (usage & cases[i].needed) == cases[i].needed, cases[i].what);
        if (created)
        {
            CHECK(mrhiDestroyTexture(device, texture) == mrhi_success, "the texture ended");
        }
    }
}

static void CheckDevice(mrhiInstance* instance, mrhiAdapterId adapter)
{
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    // The compressed formats the adapter has, for the zeroing checks.
    mrhiFeatures granted = {0};
    CHECK(mrhiAdapter_GetFeatures(instance, adapter, &granted) == mrhi_success, "its features");
    deviceDef.features.textureCompressionBc = granted.textureCompressionBc;
    deviceDef.features.textureCompressionEtc2 = granted.textureCompressionEtc2;
    deviceDef.features.textureCompressionAstc = granted.textureCompressionAstc;
    mrhiDevice* device = nullptr;
    mrhiRequestId opened = {0};
    mrhiInstanceNotification record;
    CHECK(mrhiCreateDevice(instance, &deviceDef, &device, &opened) == mrhi_success &&
              mrhiInstance_TakeNotification(instance, &record) == mrhi_success &&
              record.outcome == mrhi_success,
          "a device");
    if (device == nullptr)
    {
        return;
    }
    CheckUsages(device);
    void* native = nullptr;
    void* queue = nullptr;
    void* unused[5] = {0};
    CHECK(mrhiDevice_GetMetalDevice(device, &native, &queue) == mrhi_success && native != nullptr &&
              queue != nullptr,
          "the device's native objects");
    CHECK(mrhiDevice_GetVulkanDevice(device, &unused[0], &unused[1], &unused[2], &unused[3],
                                     &unused[4]) == mrhi_errorUnsupported,
          "no Vulkan objects");
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = SIZE;
    textureDef.height = SIZE;
    textureDef.usage = mrhi_textureCopySource | mrhi_textureCopyDestination;
    mrhiTextureId texture = {0};
    void* made = nullptr;
    CHECK(mrhiCreateTexture(device, &textureDef, &texture) == mrhi_success &&
              mrhiTexture_GetMetalTexture(device, texture, &made) == mrhi_success &&
              made != nullptr,
          "the texture's MTLTexture");
    if (native != nullptr && queue != nullptr && made != nullptr)
    {
        @autoreleasepool
        {
            id<MTLBuffer> buffer = nil;
            id<MTLCommandBuffer> commands = Record(
                (id<MTLDevice>)native, (id<MTLCommandQueue>)queue, (id<MTLTexture>)made, &buffer);
            CheckFrame(device, texture, commands);
            [commands release];
            [buffer release];
        }
        CheckDirtyTextures(device, (id<MTLDevice>)native, (id<MTLCommandQueue>)queue,
                           &deviceDef.features);
    }
    CHECK(mrhiDestroyTexture(device, texture) == mrhi_success, "the texture ended");
    mrhiDestroyDevice(device);
}

int main(void)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "an instance");
    mrhiAdapterRequestDef request = mrhiDefaultAdapterRequestDef();
    request.allowSoftware = true;
    mrhiRequestId searched = {0};
    mrhiInstanceNotification record;
    mrhiAdapterId adapter = {0};
    size_t count = 0;
    bool found = instance != nullptr &&
                 mrhiInstance_RequestAdapters(instance, &request, &searched) == mrhi_success &&
                 mrhiInstance_TakeNotification(instance, &record) == mrhi_success &&
                 mrhiInstance_GetAdapters(instance, &adapter, 1, &count) == mrhi_success &&
                 count >= 1;
    if (found)
    {
        CheckDevice(instance, adapter);
    }
    else
    {
        const char* required = getenv("MAUL_RHI_REQUIRE_METAL");
        CHECK(required == nullptr || required[0] == '\0', "Metal where required");
        printf("skip: no Metal adapter\n");
    }
    mrhiDestroyInstance(instance);
    return s_failures == 0 ? 0 : 1;
}
