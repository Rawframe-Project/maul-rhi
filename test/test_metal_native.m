// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A native pass on Metal (maul-rhi/metal.h): a texture uploaded by the
// library, overwritten by the program's own command buffer blitting in
// texels of its own, and read back by the library, the program's texels
// coming back; the device's native objects, and the refusals. Skipped
// where there is no Metal adapter, unless MAUL_RHI_REQUIRE_METAL is set.

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
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
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
    CHECK(mrhiCompileFrame(device) == mrhi_success &&
              mrhiBeginPass(device, upload) == mrhi_success &&
              mrhiWriteTexture(device, upload, &whole, texels, sizeof(texels), &layout, &extent) ==
                  mrhi_success &&
              mrhiEndPass(device, upload) == mrhi_success,
          "uploaded");
    CHECK(mrhiBeginPass(device, native) == mrhi_success &&
              mrhiSetMetalPassCommands(device, native, nullptr) == mrhi_errorInvalid &&
              mrhiSetVulkanPassCommands(device, native, buffer) == mrhi_errorUnsupported &&
              mrhiSetMetalPassCommands(device, native, buffer) == mrhi_success &&
              mrhiSetMetalPassCommands(device, native, buffer) == mrhi_errorState &&
              mrhiEndPass(device, native) == mrhi_success,
          "the native pass given its command buffer");
    CHECK(mrhiBeginPass(device, back) == mrhi_success &&
              mrhiReadTexture(device, back, &whole, &extent, &bytes) == mrhi_success &&
              mrhiEndPass(device, back) == mrhi_success &&
              mrhiSubmitFrame(device, &token) == mrhi_success &&
              mrhiWaitFrame(device, token, UINT64_C(10000000000)) == mrhi_success,
          "read back and finished");
    CHECK(commands.status == MTLCommandBufferStatusCompleted,
          "the program's command buffer done with the frame");
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(device, &record) == mrhi_success)
    {
    }
    uint8_t taken[sizeof(texels)] = {0};
    size_t size = 0;
    bool copied = mrhiTakeReadback(device, bytes, taken, sizeof(taken), &size) == mrhi_success &&
                  size == sizeof(taken);
    for (size_t i = 0; copied && i < sizeof(taken); i += 4)
    {
        copied = taken[i] == 0 && taken[i + 1] == 255 && taken[i + 2] == 0 && taken[i + 3] == 255;
    }
    CHECK(copied, "the program's texels, after the upload and before the readback");
}

static void CheckDevice(mrhiInstance* instance, mrhiAdapterId adapter)
{
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    mrhiDevice* device = nullptr;
    mrhiRequestId opened = {0};
    mrhiInstanceNotification record;
    CHECK(mrhiCreateDevice(instance, &deviceDef, &device, &opened) == mrhi_success &&
              mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
              record.outcome == mrhi_success,
          "a device");
    if (device == nullptr)
    {
        return;
    }
    void* native = nullptr;
    void* queue = nullptr;
    void* unused[5] = {0};
    CHECK(mrhiGetMetalDevice(device, &native, &queue) == mrhi_success && native != nullptr &&
              queue != nullptr,
          "the device's native objects");
    CHECK(mrhiGetVulkanDevice(device, &unused[0], &unused[1], &unused[2], &unused[3], &unused[4]) ==
              mrhi_errorUnsupported,
          "no Vulkan objects");
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = SIZE;
    textureDef.height = SIZE;
    textureDef.usage = mrhi_textureCopySource | mrhi_textureCopyDestination;
    mrhiTextureId texture = {0};
    void* made = nullptr;
    CHECK(mrhiCreateTexture(device, &textureDef, &texture) == mrhi_success &&
              mrhiGetMetalTexture(device, texture, &made) == mrhi_success && made != nullptr,
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
                 mrhiRequestAdapters(instance, &request, &searched) == mrhi_success &&
                 mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
                 mrhiGetAdapters(instance, &adapter, 1, &count) == mrhi_success && count >= 1;
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
