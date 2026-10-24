// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A native pass on D3D12 (maul-rhi/d3d12.h): a texture uploaded by the
// library, overwritten by the program's own command list copying in
// texels of its own, and read back by the library, the program's texels
// coming back; pipelines dispatching and drawing on both sides of a
// native pass, after which the library records into a list of its own; the device's
// native objects, and the refusals. Under the
// debug layer, whose errors fail the suite. Skipped where there is no
// D3D12 adapter, unless MAUL_RHI_REQUIRE_D3D12 is set.

// getenv is standard C; MSVC's runtime deprecates it for its own.
#define _CRT_SECURE_NO_WARNINGS
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS

#include "d3d12_debug.h"
#include "test_harness.h"

#include "maul-rhi/d3d12.h"
#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/vulkan.h"
#include "shaders/noop_container.h"

#include <directx/d3d12.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// The texture's side, and the rows' pitch D3D12 copies from a buffer at.
#define SIZE  16
#define PITCH 256

static mrhiAccess Whole(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
}

// The program's side: an upload buffer of green texels and a closed
// list copying them into the texture, which a native pass's copy
// destination finds in D3D12_RESOURCE_STATE_COPY_DEST.
typedef struct Native
{
    ID3D12CommandAllocator* allocator;
    ID3D12GraphicsCommandList* list;
    ID3D12Resource* upload;
} Native;

static bool Record(ID3D12Device* device, ID3D12Resource* texture, Native* native)
{
    const D3D12_HEAP_PROPERTIES heap = {.Type = D3D12_HEAP_TYPE_UPLOAD};
    const D3D12_RESOURCE_DESC desc = {
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Width = (UINT64)PITCH * SIZE,
        .Height = 1,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .SampleDesc = {.Count = 1},
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
    };
    if (FAILED(ID3D12Device_CreateCommittedResource(device, &heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    &IID_ID3D12Resource, (void**)&native->upload)))
    {
        return false;
    }
    uint8_t* bytes = nullptr;
    if (FAILED(ID3D12Resource_Map(native->upload, 0, nullptr, (void**)&bytes)))
    {
        return false;
    }
    for (uint32_t row = 0; row < SIZE; ++row)
    {
        for (uint32_t x = 0; x < SIZE; ++x)
        {
            uint8_t* texel = bytes + (size_t)row * PITCH + (size_t)x * 4;
            texel[0] = 0;
            texel[1] = 255;
            texel[2] = 0;
            texel[3] = 255;
        }
    }
    ID3D12Resource_Unmap(native->upload, 0, nullptr);
    if (FAILED(ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   &IID_ID3D12CommandAllocator,
                                                   (void**)&native->allocator)) ||
        FAILED(ID3D12Device_CreateCommandList(
            device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, native->allocator, nullptr,
            &IID_ID3D12GraphicsCommandList, (void**)&native->list)))
    {
        return false;
    }
    const D3D12_TEXTURE_COPY_LOCATION to = {
        .pResource = texture,
        .Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,
        .SubresourceIndex = 0,
    };
    const D3D12_TEXTURE_COPY_LOCATION from = {
        .pResource = native->upload,
        .Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT,
        .PlacedFootprint = {.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, SIZE, SIZE, 1, PITCH}},
    };
    ID3D12GraphicsCommandList_CopyTextureRegion(native->list, &to, 0, 0, 0, &from, nullptr);
    return SUCCEEDED(ID3D12GraphicsCommandList_Close(native->list));
}

static void Release(Native* native)
{
    if (native->list != nullptr)
    {
        ID3D12GraphicsCommandList_Release(native->list);
    }
    if (native->allocator != nullptr)
    {
        ID3D12CommandAllocator_Release(native->allocator);
    }
    if (native->upload != nullptr)
    {
        ID3D12Resource_Release(native->upload);
    }
}

// The frame: an upload, the program's copy, a readback; the program's
// texels must come back.
static void CheckFrame(mrhiDevice* device, mrhiTextureId texture, ID3D12GraphicsCommandList* list)
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
    CHECK(mrhiCompileFrame(device) == mrhi_success &&
              mrhiBeginPass(device, upload) == mrhi_success &&
              mrhiWriteTexture(device, upload, &whole, texels, sizeof(texels), &layout, &extent) ==
                  mrhi_success &&
              mrhiEndPass(device, upload) == mrhi_success,
          "uploaded");
    CHECK(mrhiBeginPass(device, native) == mrhi_success &&
              mrhiSetD3d12PassCommands(device, native, nullptr) == mrhi_errorInvalid &&
              mrhiSetVulkanPassCommands(device, native, list) == mrhi_errorUnsupported &&
              mrhiSetD3d12PassCommands(device, native, list) == mrhi_success &&
              mrhiSetD3d12PassCommands(device, native, list) == mrhi_errorState &&
              mrhiEndPass(device, native) == mrhi_success,
          "the native pass given its list");
    CHECK(mrhiBeginPass(device, back) == mrhi_success &&
              mrhiReadTexture(device, back, &whole, &extent, &bytes) == mrhi_success &&
              mrhiEndPass(device, back) == mrhi_success &&
              mrhiSubmitFrame(device, &token) == mrhi_success &&
              mrhiWaitFrame(device, token, UINT64_C(10000000000)) == mrhi_success,
          "read back and finished");
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

// The noop kernel's compute pipeline and its draw's graphics pipeline,
// into an RGBA8 target.
static void MakePipelines(mrhiDevice* device, mrhiShaderId* shaderOut,
                          mrhiComputePipelineId* computeOut, mrhiGraphicsPipelineId* graphicsOut)
{
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_noopContainer;
    shaderDef.byteCount = sizeof(s_noopContainer);
    CHECK(mrhiCreateShader(device, &shaderDef, shaderOut) == mrhi_success, "the shader");
    mrhiComputePipelineDef compute = mrhiDefaultComputePipelineDef();
    compute.shader = *shaderOut;
    compute.entry = "cs";
    compute.entryLength = 2;
    mrhiGraphicsPipelineDef graphics = mrhiDefaultGraphicsPipelineDef();
    graphics.shader = *shaderOut;
    graphics.vertexEntry = "vs";
    graphics.vertexEntryLength = 2;
    graphics.fragmentEntry = "fs";
    graphics.fragmentEntryLength = 2;
    graphics.colorTargets[0].format = mrhi_formatRgba8Unorm;
    graphics.colorTargetCount = 1;
    mrhiRequestId request = {0};
    mrhiDeviceNotification record;
    CHECK(mrhiCreateComputePipeline(device, &compute, computeOut, &request) == mrhi_success &&
              mrhiNextDeviceNotification(device, &record) == mrhi_success &&
              record.kind == mrhi_devicePipelineReady && record.outcome == mrhi_success,
          "the compute pipeline");
    CHECK(mrhiCreateGraphicsPipeline(device, &graphics, graphicsOut, &request) == mrhi_success &&
              mrhiNextDeviceNotification(device, &record) == mrhi_success &&
              record.kind == mrhi_devicePipelineReady && record.outcome == mrhi_success,
          "the graphics pipeline");
}

// A frame dispatching and drawing before and after a native pass with no
// list of the program's: the passes after it record into a new list,
// which has no root signature until the library sets each again, as the
// debug layer checks.
static void CheckRoots(mrhiDevice* device)
{
    mrhiShaderId shader = {0};
    mrhiComputePipelineId compute = {0};
    mrhiGraphicsPipelineId graphics = {0};
    MakePipelines(device, &shader, &compute, &graphics);
    mrhiTextureDef targetDef = mrhiDefaultTextureDef();
    targetDef.format = mrhi_formatRgba8Unorm;
    targetDef.width = SIZE;
    targetDef.height = SIZE;
    targetDef.usage = mrhi_textureRenderTarget;
    mrhiTextureId texture = {0};
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId target = {0};
    CHECK(mrhiCreateTexture(device, &targetDef, &texture) == mrhi_success &&
              mrhiBeginFrame(device, &frame) == mrhi_success &&
              mrhiImportTexture(device, texture, &target) == mrhi_success,
          "a frame with a target");
    // A dispatch and a draw, the native pass, then a dispatch and a draw.
    mrhiPassId passes[5] = {{0}};
    for (uint32_t i = 0; i < 5; ++i)
    {
        mrhiPassDef def = mrhiDefaultPassDef();
        def.neverCull = true;
        def.native = i == 2;
        if (i % 3 == 1)
        {
            def.colorTargets[0] = (mrhiColorTarget){.resource = target, .load = mrhi_loadClear};
            def.colorTargetCount = 1;
        }
        CHECK(mrhiAddPass(device, &def, &passes[i]) == mrhi_success, "a pass");
    }
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    for (uint32_t i = 0; i < 5; ++i)
    {
        bool recorded = mrhiBeginPass(device, passes[i]) == mrhi_success;
        if (i % 3 == 0)
        {
            recorded = recorded &&
                       mrhiSetComputePipeline(device, passes[i], compute) == mrhi_success &&
                       mrhiDispatch(device, passes[i], 1, 1, 1) == mrhi_success;
        }
        else if (i % 3 == 1)
        {
            recorded = recorded &&
                       mrhiSetGraphicsPipeline(device, passes[i], graphics) == mrhi_success &&
                       mrhiDraw(device, passes[i], 3, 1, 0, 0) == mrhi_success;
        }
        CHECK(recorded && mrhiEndPass(device, passes[i]) == mrhi_success, "a pass recorded");
    }
    mrhiRequestId token = {0};
    CHECK(mrhiSubmitFrame(device, &token) == mrhi_success &&
              mrhiWaitFrame(device, token, UINT64_C(10000000000)) == mrhi_success,
          "submitted and finished");
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(device, &record) == mrhi_success)
    {
    }
    CHECK(mrhiDestroyTexture(device, texture) == mrhi_success &&
              mrhiDestroyGraphicsPipeline(device, graphics) == mrhi_success &&
              mrhiDestroyComputePipeline(device, compute) == mrhi_success &&
              mrhiDestroyShader(device, shader) == mrhi_success,
          "the target, pipelines and shader ended");
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
    CHECK(mrhiGetD3d12Device(device, &native, &queue) == mrhi_success && native != nullptr &&
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
    void* resource = nullptr;
    CHECK(mrhiCreateTexture(device, &textureDef, &texture) == mrhi_success &&
              mrhiGetD3d12Texture(device, texture, &resource) == mrhi_success &&
              resource != nullptr,
          "the texture's resource");
    Native recorded = {0};
    bool made = native != nullptr && resource != nullptr &&
                Record((ID3D12Device*)native, (ID3D12Resource*)resource, &recorded);
    CHECK(made, "the program's list");
    if (made)
    {
        CheckFrame(device, texture, recorded.list);
    }
    CheckRoots(device);
    Release(&recorded);
    CHECK(mrhiDestroyTexture(device, texture) == mrhi_success, "the texture ended");
    mrhiDestroyDevice(device);
}

int main(void)
{
    // Before the library opens a device, as the debug layer needs.
    mrhiTestWatchD3d12();
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
        const char* required = getenv("MAUL_RHI_REQUIRE_D3D12");
        CHECK(required == nullptr || required[0] == '\0', "D3D12 where required");
        printf("skip: no D3D12 adapter\n");
    }
    mrhiDestroyInstance(instance);
    CHECK(mrhiTestD3d12Errors() == 0, "no D3D12 debug layer error");
    return s_failures == 0 ? 0 : 1;
}
