// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// What a refused call leaves in its outputs: every call that gives a
// handle or an id sets it to NULL or the null id on any failure, first,
// before it checks its owner, so a failed call never leaves a value the
// caller could destroy, wait on or use. Each output starts as garbage, and
// each call is refused twice: without its owner, and for an argument.

#include "test_device_setup.h"

#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/heap.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/shader.h"
#include "maul-rhi/surface.h"

#include <string.h>

#define GARBAGE 0xA5

// Fills an output with garbage before a call.
#define SPOIL(out) memset(&(out), GARBAGE, sizeof(out))

// Whether an output holds only zero bytes: NULL or the null id.
#define CLEARED(out) IsZero(&(out), sizeof(out))

static bool IsZero(const void* at, size_t bytes)
{
    const unsigned char* byte = at;
    for (size_t i = 0; i < bytes; ++i)
    {
        if (byte[i] != 0)
        {
            return false;
        }
    }
    return true;
}

static mrhiDevice* s_device;

static void TestInstance(void)
{
    mrhiInstance* instance;
    SPOIL(instance);
    CHECK(mrhiCreateInstance(nullptr, &instance) == mrhi_errorInvalid && instance == nullptr,
          "an instance without a def: NULL");
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.cookie = 0;
    SPOIL(instance);
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_errorInvalid && instance == nullptr,
          "an instance of a bad def: NULL");
}

static void TestRequestAdapters(void)
{
    mrhiAdapterRequestDef def = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    SPOIL(request);
    CHECK(mrhiRequestAdapters(nullptr, &def, &request) == mrhi_errorInvalid && CLEARED(request),
          "a search without an instance: the null id");
    SPOIL(request);
    CHECK(mrhiRequestAdapters(s_instance, nullptr, &request) == mrhi_errorInvalid &&
              CLEARED(request),
          "a search without a def: the null id");
}

static void TestDevice(void)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    mrhiDevice* device;
    mrhiRequestId request;
    SPOIL(device);
    SPOIL(request);
    CHECK(mrhiCreateDevice(nullptr, &def, &device, &request) == mrhi_errorInvalid &&
              device == nullptr && CLEARED(request),
          "a device without an instance: NULL and the null id");
    SPOIL(device);
    SPOIL(request);
    CHECK(mrhiCreateDevice(s_instance, nullptr, &device, &request) == mrhi_errorInvalid &&
              device == nullptr && CLEARED(request),
          "a device without a def: NULL and the null id");
}

static void TestSurface(void)
{
    mrhiSurfaceDef def = mrhiDefaultSurfaceDef();
    mrhiSurfaceId surface;
    SPOIL(surface);
    CHECK(mrhiCreateSurface(nullptr, &def, &surface) == mrhi_errorInvalid && CLEARED(surface),
          "a surface without an instance: the null id");
    SPOIL(surface);
    CHECK(mrhiCreateSurface(s_instance, nullptr, &surface) == mrhi_errorInvalid && CLEARED(surface),
          "a surface without a def: the null id");
}

static void TestBuffer(void)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    mrhiBufferId buffer;
    SPOIL(buffer);
    CHECK(mrhiCreateBuffer(nullptr, &def, &buffer) == mrhi_errorInvalid && CLEARED(buffer),
          "a buffer without a device: the null id");
    SPOIL(buffer);
    CHECK(mrhiCreateBuffer(s_device, nullptr, &buffer) == mrhi_errorInvalid && CLEARED(buffer),
          "a buffer without a def: the null id");
}

static void TestTexture(void)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    mrhiTextureId texture;
    SPOIL(texture);
    CHECK(mrhiCreateTexture(nullptr, &def, &texture) == mrhi_errorInvalid && CLEARED(texture),
          "a texture without a device: the null id");
    SPOIL(texture);
    CHECK(mrhiCreateTexture(s_device, nullptr, &texture) == mrhi_errorInvalid && CLEARED(texture),
          "a texture without a def: the null id");
}

static void TestView(void)
{
    mrhiViewDef def = mrhiDefaultViewDef();
    mrhiViewId view;
    SPOIL(view);
    CHECK(mrhiCreateView(nullptr, &def, &view) == mrhi_errorInvalid && CLEARED(view),
          "a view without a device: the null id");
    SPOIL(view);
    CHECK(mrhiCreateView(s_device, nullptr, &view) == mrhi_errorInvalid && CLEARED(view),
          "a view without a def: the null id");
}

static void TestSampler(void)
{
    mrhiSamplerDef def = mrhiDefaultSamplerDef();
    mrhiSamplerId sampler;
    SPOIL(sampler);
    CHECK(mrhiCreateSampler(nullptr, &def, &sampler) == mrhi_errorInvalid && CLEARED(sampler),
          "a sampler without a device: the null id");
    SPOIL(sampler);
    CHECK(mrhiCreateSampler(s_device, nullptr, &sampler) == mrhi_errorInvalid && CLEARED(sampler),
          "a sampler without a def: the null id");
}

static void TestQuerySet(void)
{
    mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
    mrhiQuerySetId set;
    SPOIL(set);
    CHECK(mrhiCreateQuerySet(nullptr, &def, &set) == mrhi_errorInvalid && CLEARED(set),
          "a query set without a device: the null id");
    SPOIL(set);
    CHECK(mrhiCreateQuerySet(s_device, nullptr, &set) == mrhi_errorInvalid && CLEARED(set),
          "a query set without a def: the null id");
}

static void TestHeap(void)
{
    mrhiHeapDef def = mrhiDefaultHeapDef();
    mrhiHeapId heap;
    SPOIL(heap);
    CHECK(mrhiCreateHeap(nullptr, &def, &heap) == mrhi_errorInvalid && CLEARED(heap),
          "a heap without a device: the null id");
    SPOIL(heap);
    CHECK(mrhiCreateHeap(s_device, nullptr, &heap) == mrhi_errorInvalid && CLEARED(heap),
          "a heap without a def: the null id");
}

static void TestShader(void)
{
    mrhiShaderDef def = mrhiDefaultShaderDef();
    mrhiShaderId shader;
    SPOIL(shader);
    CHECK(mrhiCreateShader(nullptr, &def, &shader) == mrhi_errorInvalid && CLEARED(shader),
          "a shader without a device: the null id");
    SPOIL(shader);
    CHECK(mrhiCreateShader(s_device, nullptr, &shader) == mrhi_errorInvalid && CLEARED(shader),
          "a shader without a def: the null id");
}

static void TestGraphicsPipeline(void)
{
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    mrhiGraphicsPipelineId pipeline;
    mrhiRequestId request;
    SPOIL(pipeline);
    SPOIL(request);
    CHECK(mrhiCreateGraphicsPipeline(nullptr, &def, &pipeline, &request) == mrhi_errorInvalid &&
              CLEARED(pipeline) && CLEARED(request),
          "a graphics pipeline without a device: the null ids");
    SPOIL(pipeline);
    SPOIL(request);
    CHECK(mrhiCreateGraphicsPipeline(s_device, nullptr, &pipeline, &request) == mrhi_errorInvalid &&
              CLEARED(pipeline) && CLEARED(request),
          "a graphics pipeline without a def: the null ids");
}

static void TestComputePipeline(void)
{
    mrhiComputePipelineDef def = mrhiDefaultComputePipelineDef();
    mrhiComputePipelineId pipeline;
    mrhiRequestId request;
    SPOIL(pipeline);
    SPOIL(request);
    CHECK(mrhiCreateComputePipeline(nullptr, &def, &pipeline, &request) == mrhi_errorInvalid &&
              CLEARED(pipeline) && CLEARED(request),
          "a compute pipeline without a device: the null ids");
    SPOIL(pipeline);
    SPOIL(request);
    CHECK(mrhiCreateComputePipeline(s_device, nullptr, &pipeline, &request) == mrhi_errorInvalid &&
              CLEARED(pipeline) && CLEARED(request),
          "a compute pipeline without a def: the null ids");
}

static void TestDeclareBuffer(void)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    mrhiResourceId resource;
    SPOIL(resource);
    CHECK(mrhiDeclareBuffer(nullptr, &def, &resource) == mrhi_errorInvalid && CLEARED(resource),
          "a declared buffer without a device: the null id");
    SPOIL(resource);
    CHECK(mrhiDeclareBuffer(s_device, nullptr, &resource) == mrhi_errorInvalid && CLEARED(resource),
          "a declared buffer without a def: the null id");
}

static void TestDeclareTexture(void)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    mrhiResourceId resource;
    SPOIL(resource);
    CHECK(mrhiDeclareTexture(nullptr, &def, &resource) == mrhi_errorInvalid && CLEARED(resource),
          "a declared texture without a device: the null id");
    SPOIL(resource);
    CHECK(mrhiDeclareTexture(s_device, nullptr, &resource) == mrhi_errorInvalid &&
              CLEARED(resource),
          "a declared texture without a def: the null id");
}

static void TestImportBuffer(void)
{
    mrhiResourceId resource;
    SPOIL(resource);
    CHECK(mrhiImportBuffer(nullptr, (mrhiBufferId){0}, &resource) == mrhi_errorInvalid &&
              CLEARED(resource),
          "an imported buffer without a device: the null id");
    SPOIL(resource);
    CHECK(mrhiImportBuffer(s_device, (mrhiBufferId){0}, &resource) != mrhi_success &&
              CLEARED(resource),
          "an imported buffer that does not exist: the null id");
}

static void TestImportTexture(void)
{
    mrhiResourceId resource;
    SPOIL(resource);
    CHECK(mrhiImportTexture(nullptr, (mrhiTextureId){0}, &resource) == mrhi_errorInvalid &&
              CLEARED(resource),
          "an imported texture without a device: the null id");
    SPOIL(resource);
    CHECK(mrhiImportTexture(s_device, (mrhiTextureId){0}, &resource) != mrhi_success &&
              CLEARED(resource),
          "an imported texture that does not exist: the null id");
}

static void TestAcquireSurfaceImage(void)
{
    mrhiResourceId image;
    SPOIL(image);
    CHECK(mrhiAcquireSurfaceImage(nullptr, (mrhiSurfaceId){0}, &image) == mrhi_errorInvalid &&
              CLEARED(image),
          "an image without a device: the null id");
    SPOIL(image);
    CHECK(mrhiAcquireSurfaceImage(s_device, (mrhiSurfaceId){0}, &image) != mrhi_success &&
              CLEARED(image),
          "an image of a surface that does not exist: the null id");
}

static void TestAddPass(void)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    mrhiPassId pass;
    SPOIL(pass);
    CHECK(mrhiAddPass(nullptr, &def, &pass) == mrhi_errorInvalid && CLEARED(pass),
          "a pass without a device: the null id");
    SPOIL(pass);
    CHECK(mrhiAddPass(s_device, nullptr, &pass) == mrhi_errorInvalid && CLEARED(pass),
          "a pass without a def: the null id");
}

static void TestReadBuffer(void)
{
    mrhiRequestId request;
    SPOIL(request);
    CHECK(mrhiReadBuffer(nullptr, (mrhiPassId){0}, (mrhiResourceId){0}, 0, 4, &request) ==
                  mrhi_errorInvalid &&
              CLEARED(request),
          "a buffer read without a device: the null id");
    SPOIL(request);
    CHECK(mrhiReadBuffer(s_device, (mrhiPassId){0}, (mrhiResourceId){0}, 0, 4, &request) !=
                  mrhi_success &&
              CLEARED(request),
          "a buffer read outside a pass: the null id");
}

static void TestReadTexture(void)
{
    mrhiTextureCopy source = {0};
    mrhiExtent3d size = {1, 1, 1};
    mrhiRequestId request;
    SPOIL(request);
    CHECK(mrhiReadTexture(nullptr, (mrhiPassId){0}, &source, &size, &request) ==
                  mrhi_errorInvalid &&
              CLEARED(request),
          "a texture read without a device: the null id");
    SPOIL(request);
    CHECK(mrhiReadTexture(s_device, (mrhiPassId){0}, nullptr, &size, &request) ==
                  mrhi_errorInvalid &&
              CLEARED(request),
          "a texture read without a source: the null id");
}

static void TestSubmitFrame(void)
{
    mrhiRequestId token;
    SPOIL(token);
    CHECK(mrhiSubmitFrame(nullptr, &token) == mrhi_errorInvalid && CLEARED(token),
          "a submit without a device: the null id");
    SPOIL(token);
    CHECK(mrhiSubmitFrame(s_device, &token) != mrhi_success && CLEARED(token),
          "a submit without a frame: the null id");
}

int main(void)
{
    ResetAdapter();
    s_device = OpenWith(mrhiDefaultDeviceDef(), true);
    TestInstance();
    TestRequestAdapters();
    TestDevice();
    TestSurface();
    TestBuffer();
    TestTexture();
    TestView();
    TestSampler();
    TestQuerySet();
    TestHeap();
    TestShader();
    TestGraphicsPipeline();
    TestComputePipeline();
    TestDeclareBuffer();
    TestDeclareTexture();
    TestImportBuffer();
    TestImportTexture();
    TestAcquireSurfaceImage();
    TestAddPass();
    TestReadBuffer();
    TestReadTexture();
    TestSubmitFrame();
    Close(s_device);
    return s_failures == 0 ? 0 : 1;
}
