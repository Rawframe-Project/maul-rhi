// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The conformance suite (mrhi-0003): the same checks, through the public API
// only, on every driver: the test driver, and each adapter of the
// build's native driver. A host without native adapters skips them,
// unless MAUL_RHI_REQUIRE_VULKAN (or _WEBGPU, or _METAL, for the build's
// driver) is set and not empty.

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "test_harness.h"

#include "maul-rhi/capabilities.h"
#include "maul-rhi/device.h"
#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/heap.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/shader.h"
#include "maul-rhi/surface.h"
#include "maul-rhi/test.h"
#include "shaders/bindless_container.h"
#include "shaders/conformance_container.h"

#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#endif

#ifdef MRHI_TEST_XCB
#include <xcb/xcb.h>
#endif

// A label from a string literal, for a def's label and labelLength.
#define LABEL(def, text) ((def).label = (text), (def).labelLength = sizeof(text) - 1)

// The limits every adapter reaches at least, as the floor has them.
#define AT_LEAST(X)                                                                                \
    X(textureDimension2d)                                                                          \
    X(textureDimension3d)                                                                          \
    X(textureArrayLayers)                                                                          \
    X(bindingTables)                                                                               \
    X(bindingsPerTable)                                                                            \
    X(sampledTexturesPerStage)                                                                     \
    X(samplersPerStage)                                                                            \
    X(storageBuffersPerStage)                                                                      \
    X(storageTexturesPerStage)                                                                     \
    X(uniformBuffersPerStage)                                                                      \
    X(uniformBindingBytes)                                                                         \
    X(storageBindingBytes)                                                                         \
    X(vertexBuffers)                                                                               \
    X(tablesPlusVertexBuffers)                                                                     \
    X(bufferBytes)                                                                                 \
    X(vertexAttributes)                                                                            \
    X(vertexStride)                                                                                \
    X(interStageVariables)                                                                         \
    X(colorAttachments)                                                                            \
    X(colorBytesPerSample)                                                                         \
    X(workgroupStorageBytes)                                                                       \
    X(workgroupInvocations)                                                                        \
    X(workgroupSizeX)                                                                              \
    X(workgroupSizeY)                                                                              \
    X(workgroupSizeZ)                                                                              \
    X(workgroupsPerDimension)                                                                      \
    X(rootBlockBytes)                                                                              \
    X(framesInFlight)

#define CHECK_AT_LEAST(field) CHECK(limits.field >= floor.field, "the floor's " #field);

// The next instance notification. Native drivers answer at the next
// poll; a browser settles its promises only when the page runs its event
// loop, so on the web the suite sleeps (JSPI) until one arrives.
static mrhiResult NextInstance(mrhiInstance* instance, mrhiInstanceNotification* recordOut)
{
    mrhiResult status = mrhiNextInstanceNotification(instance, recordOut);
#ifdef __EMSCRIPTEN__
    for (int slept = 0; status == mrhi_empty && slept < 10000; ++slept)
    {
        emscripten_sleep(1);
        status = mrhiNextInstanceNotification(instance, recordOut);
    }
#endif
    return status;
}

// The next device notification, waited for on the web as NextInstance
// waits.
static mrhiResult NextDevice(mrhiDevice* device, mrhiDeviceNotification* recordOut)
{
    mrhiResult status = mrhiNextDeviceNotification(device, recordOut);
#ifdef __EMSCRIPTEN__
    for (int slept = 0; status == mrhi_empty && slept < 10000; ++slept)
    {
        emscripten_sleep(1);
        status = mrhiNextDeviceNotification(device, recordOut);
    }
#endif
    return status;
}

// Waits for a frame; a browser never blocks, so on the web the suite
// sleeps until it has finished.
static mrhiResult WaitFor(mrhiDevice* device, mrhiRequestId token)
{
    mrhiResult status = mrhiWaitFrame(device, token, UINT64_C(10000000000));
#ifdef __EMSCRIPTEN__
    for (int slept = 0; status == mrhi_timeout && slept < 10000; ++slept)
    {
        emscripten_sleep(1);
        status = mrhiWaitFrame(device, token, 0);
    }
#endif
    return status;
}

static mrhiInstance* Create(const mrhiChain* driver)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = driver;
    mrhiInstance* instance = nullptr;
    return mrhiCreateInstance(&def, &instance) == mrhi_success ? instance : nullptr;
}

// Searches for adapters, software ones included, and lists them.
static size_t Search(mrhiInstance* instance, mrhiAdapterId* ids, size_t capacity)
{
    mrhiAdapterRequestDef request = mrhiDefaultAdapterRequestDef();
    request.allowSoftware = true;
    mrhiRequestId id;
    CHECK(mrhiRequestAdapters(instance, &request, &id) == mrhi_success, "a search");
    mrhiInstanceNotification record;
    CHECK(NextInstance(instance, &record) == mrhi_success &&
              record.kind == mrhi_instanceAdaptersFound && record.outcome == mrhi_success,
          "answered at the next poll");
    size_t count = 0;
    CHECK(mrhiGetAdapters(instance, ids, capacity, &count) == mrhi_success && count <= capacity,
          "listed");
    return count;
}

static void CheckLimits(mrhiInstance* instance, mrhiAdapterId adapter)
{
    mrhiLimits limits;
    CHECK(mrhiGetAdapterLimits(instance, adapter, &limits) == mrhi_success, "limits");
    mrhiLimits floor = mrhiDefaultLimits();
    AT_LEAST(CHECK_AT_LEAST)
    CHECK(limits.uniformOffsetAlignment <= floor.uniformOffsetAlignment &&
              limits.storageOffsetAlignment <= floor.storageOffsetAlignment,
          "the floor's alignments");
}

// Whether a format belongs to the family from first to last.
static bool In(mrhiFormat format, mrhiFormat first, mrhiFormat last)
{
    return format >= first && format <= last;
}

// Each format's caps hang together and agree with the features: a
// granted compressed family samples and filters, and one not granted
// does nothing.
static void CheckFormats(mrhiInstance* instance, mrhiAdapterId adapter)
{
    mrhiFeatures features;
    CHECK(mrhiGetAdapterFeatures(instance, adapter, &features) == mrhi_success, "features");
    for (mrhiFormat format = mrhi_formatRgba8Unorm; format <= mrhi_formatAstc12x12UnormSrgb;
         ++format)
    {
        mrhiFormatCaps caps;
        CHECK(mrhiGetFormatCaps(instance, adapter, format, &caps) == mrhi_success, "caps");
        CHECK(!caps.filtering || caps.sampling, "filtering samples");
        CHECK(!caps.blending || caps.rendering, "blending renders");
        CHECK(!caps.rendering || (caps.sampleCounts & 1) != 0, "a target has one sample");
        bool granted = true;
        if (In(format, mrhi_formatBc1RgbaUnorm, mrhi_formatBc7RgbaUnormSrgb))
        {
            granted = features.textureCompressionBc;
        }
        else if (In(format, mrhi_formatEtc2Rgb8Unorm, mrhi_formatEacRg11Snorm))
        {
            granted = features.textureCompressionEtc2;
        }
        else if (In(format, mrhi_formatAstc4x4Unorm, mrhi_formatAstc12x12UnormSrgb))
        {
            granted = features.textureCompressionAstc;
        }
        else
        {
            continue;
        }
        CHECK(granted == (caps.sampling && caps.filtering), "a family as granted");
    }
    mrhiFormatCaps caps;
    CHECK(mrhiGetFormatCaps(instance, adapter, mrhi_formatR32Float, &caps) == mrhi_success &&
              (!features.float32Filterable || caps.filtering),
          "float32Filterable filters");
    CHECK(mrhiGetFormatCaps(instance, adapter, mrhi_formatRg11b10Ufloat, &caps) == mrhi_success &&
              (!features.rg11b10Renderable || caps.rendering),
          "rg11b10Renderable renders");
}

static mrhiBufferId MakeBuffer(mrhiDevice* device, uint64_t size)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    def.usage = mrhi_bufferStorage | mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    mrhiBufferId buffer = {0};
    CHECK(mrhiCreateBuffer(device, &def, &buffer) == mrhi_success, "a buffer");
    return buffer;
}

static mrhiTextureId MakeTexture(mrhiDevice* device, mrhiTextureKind kind, mrhiFormat format,
                                 uint32_t layers)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.kind = kind;
    def.format = format;
    def.width = 64;
    def.height = 64;
    def.depthOrLayers = layers;
    def.mipLevels = 7;
    def.usage = mrhi_textureSampled | mrhi_textureRenderTarget | mrhi_textureCopyDestination;
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(device, &def, &texture) == mrhi_success, "a texture");
    return texture;
}

static mrhiViewId MakeView(mrhiDevice* device, mrhiTextureId texture, mrhiTextureKind kind,
                           mrhiTextureAspect aspect)
{
    mrhiViewDef def = mrhiDefaultViewDef();
    def.texture = texture;
    def.kind = kind;
    def.aspect = aspect;
    LABEL(def, "view");
    mrhiViewId view = {0};
    CHECK(mrhiCreateView(device, &def, &view) == mrhi_success, "a view");
    return view;
}

// A 64 by 64 by 4 volume, sampled and copied to.
static mrhiTextureId MakeVolume(mrhiDevice* device)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.kind = mrhi_texture3d;
    def.format = mrhi_formatRgba8Unorm;
    def.width = 64;
    def.height = 64;
    def.depthOrLayers = 4;
    def.mipLevels = 3;
    def.usage = mrhi_textureSampled | mrhi_textureCopyDestination;
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(device, &def, &texture) == mrhi_success, "a volume");
    return texture;
}

static mrhiQuerySetId MakeQuerySet(mrhiDevice* device, mrhiQueryType type, uint32_t count);

// Objects of every kind: many buffers over more than one block, one
// larger than half a block, textures and views of each shape, a
// comparing anisotropic sampler, query sets (timestamps where granted);
// some destroyed, the rest left for the device's end.
static void CheckObjects(mrhiDevice* device, bool timestamps)
{
    mrhiBufferId buffers[80];
    for (int i = 0; i < 80; ++i)
    {
        buffers[i] = MakeBuffer(device, 1u << 20);
    }
    mrhiBufferId large = MakeBuffer(device, 40u << 20);
    // Sizes off every alignment, so each next offset must be aligned.
    for (uint64_t size = 4; size < 4096; size = size * 3 + 4)
    {
        (void)MakeBuffer(device, size);
    }
    for (int i = 0; i < 80; i += 2)
    {
        CHECK(mrhiDestroyBuffer(device, buffers[i]) == mrhi_success, "a buffer destroyed");
    }
    CHECK(mrhiDestroyBuffer(device, large) == mrhi_success, "the large one destroyed");
    mrhiTextureId color = MakeTexture(device, mrhi_texture2d, mrhi_formatRgba8Unorm, 1);
    mrhiTextureId depth = MakeTexture(device, mrhi_texture2d, mrhi_formatDepthStencil, 1);
    mrhiTextureId cube = MakeTexture(device, mrhi_textureCube, mrhi_formatRgba16Float, 6);
    (void)MakeView(device, color, mrhi_texture2d, mrhi_aspectAll);
    mrhiViewId depthView = MakeView(device, depth, mrhi_texture2d, mrhi_aspectDepthOnly);
    (void)MakeView(device, depth, mrhi_texture2d, mrhi_aspectAll);
    (void)MakeView(device, cube, mrhi_textureCube, mrhi_aspectAll);
    mrhiTextureId array = MakeTexture(device, mrhi_texture2dArray, mrhi_formatRgba8Unorm, 4);
    (void)MakeView(device, array, mrhi_texture2dArray, mrhi_aspectAll);
    mrhiTextureId cubes = MakeTexture(device, mrhi_textureCubeArray, mrhi_formatRgba8Unorm, 12);
    (void)MakeView(device, cubes, mrhi_textureCubeArray, mrhi_aspectAll);
    (void)MakeView(device, MakeVolume(device), mrhi_texture3d, mrhi_aspectAll);
    mrhiQuerySetId occlusion = MakeQuerySet(device, mrhi_queryOcclusion, 8);
    CHECK(mrhiDestroyQuerySet(device, occlusion) == mrhi_success, "a query set destroyed");
    if (timestamps)
    {
        (void)MakeQuerySet(device, mrhi_queryTimestamp, 8);
    }
    CHECK(mrhiDestroyView(device, depthView) == mrhi_success, "a view destroyed");
    CHECK(mrhiDestroyTexture(device, depth) == mrhi_success, "a texture destroyed");
    mrhiSamplerDef def = mrhiDefaultSamplerDef();
    def.magFilter = mrhi_filterLinear;
    def.minFilter = mrhi_filterLinear;
    def.mipFilter = mrhi_filterLinear;
    def.maxAnisotropy = 16;
    def.compare = mrhi_compareLess;
    mrhiSamplerId sampler = {0};
    CHECK(mrhiCreateSampler(device, &def, &sampler) == mrhi_success, "a sampler");
}

// A frame whose transients take memory the driver measures: a target
// and two buffers, in a pass never culled.
static void CheckFrameMemory(mrhiDevice* device)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success, "a frame");
    mrhiTextureDef texture = mrhiDefaultTextureDef();
    texture.format = mrhi_formatRgba8Unorm;
    texture.width = 64;
    texture.height = 64;
    mrhiResourceId target = {0};
    CHECK(mrhiDeclareTexture(device, &texture, &target) == mrhi_success, "a target");
    mrhiBufferDef buffer = mrhiDefaultBufferDef();
    buffer.size = 1000;
    mrhiResourceId buffers[2] = {{0}};
    CHECK(mrhiDeclareBuffer(device, &buffer, &buffers[0]) == mrhi_success &&
              mrhiDeclareBuffer(device, &buffer, &buffers[1]) == mrhi_success,
          "two buffers");
    mrhiAccess writes[2];
    for (int i = 0; i < 2; ++i)
    {
        writes[i] = (mrhiAccess){.resource = buffers[i], .kind = mrhi_accessStorageWrite};
    }
    mrhiPassDef pass = mrhiDefaultPassDef();
    pass.colorTargets[0] =
        (mrhiColorTarget){.resource = target, .load = mrhi_loadClear, .store = mrhi_storeKeep};
    pass.colorTargetCount = 1;
    pass.accesses = writes;
    pass.accessCount = 2;
    pass.neverCull = true;
    mrhiPassId id = {0};
    CHECK(mrhiAddPass(device, &pass, &id) == mrhi_success, "a pass");
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    uint64_t bytes = 0;
    // The target is transient, which a tile GPU keeps on chip.
    CHECK(mrhiGetFrameMemory(device, &bytes) == mrhi_success && bytes >= 2000,
          "the transients' memory");
    CHECK(mrhiDropFrame(device) == mrhi_success, "dropped");
}

static mrhiPassId CopyPass(mrhiDevice* device, const mrhiAccess* accesses, uint32_t count)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.accesses = accesses;
    def.accessCount = count;
    def.neverCull = true;
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(device, &def, &pass) == mrhi_success, "a pass");
    return pass;
}

static mrhiAccess Whole(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
}

// Submits the open frame, waits for it, and takes the device's
// notifications: the frame done and its readbacks answered.
static void Finish(mrhiDevice* device, uint32_t readbacks)
{
    mrhiRequestId token = {0};
    CHECK(mrhiSubmitFrame(device, &token) == mrhi_success, "submitted");
    CHECK(WaitFor(device, token) == mrhi_success, "finished");
    mrhiDeviceNotification record;
    uint32_t done = 0;
    uint32_t answered = 0;
    while (mrhiNextDeviceNotification(device, &record) == mrhi_success)
    {
        CHECK(record.outcome == mrhi_success, "a success");
        done += record.kind == mrhi_deviceFrameDone ? 1 : 0;
        answered += record.kind == mrhi_deviceReadbackReady ? 1 : 0;
    }
    CHECK(done == 1 && answered == readbacks, "the frame and its readbacks answered");
}

// Whether the device's driver runs work: the test driver moves no
// bytes, so only its answers are checked.
static bool s_runs;

// Whether the driver runs no frames: the Metal driver, whose frames are
// not made yet (mrhi-0003).
static bool s_noFrames;

static bool Taken(mrhiDevice* device, mrhiRequestId request, const uint8_t* expected, size_t size)
{
    static uint8_t bytes[4096];
    size_t taken = 0;
    return mrhiTakeReadback(device, request, bytes, sizeof(bytes), &taken) == mrhi_success &&
           taken == size && (!s_runs || memcmp(bytes, expected, size) == 0);
}

// Bytes and texels uploaded to device objects, copied through a
// transient buffer and read back; then read again in a frame that
// destroys the buffer after recording, which it must keep until the
// frame finishes.
static void CheckRoundTrip(mrhiDevice* device)
{
    uint8_t pattern[1024];
    for (size_t i = 0; i < sizeof(pattern); ++i)
    {
        pattern[i] = (uint8_t)(i * 7 + 3);
    }
    mrhiBufferId buffer = MakeBuffer(device, sizeof(pattern));
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = 16;
    textureDef.height = 16;
    textureDef.usage = mrhi_textureCopySource | mrhi_textureCopyDestination;
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(device, &textureDef, &texture) == mrhi_success, "a texture");
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success, "a frame");
    mrhiResourceId b = {0};
    mrhiResourceId t = {0};
    mrhiResourceId x = {0};
    mrhiResourceId y = {0};
    mrhiBufferDef transient = mrhiDefaultBufferDef();
    transient.size = 256;
    CHECK(mrhiImportBuffer(device, buffer, &b) == mrhi_success &&
              mrhiImportTexture(device, texture, &t) == mrhi_success &&
              mrhiDeclareBuffer(device, &transient, &x) == mrhi_success &&
              mrhiDeclareBuffer(device, &transient, &y) == mrhi_success,
          "the resources");
    mrhiAccess writes[2] = {Whole(b, mrhi_accessCopyDestination),
                            Whole(t, mrhi_accessCopyDestination)};
    mrhiPassId upload = CopyPass(device, writes, 2);
    mrhiAccess moves[3] = {Whole(b, mrhi_accessCopySource), Whole(x, mrhi_accessCopyDestination),
                           Whole(y, mrhi_accessCopyDestination)};
    mrhiPassId move = CopyPass(device, moves, 3);
    mrhiAccess reads[3] = {Whole(x, mrhi_accessCopySource), Whole(y, mrhi_accessCopySource),
                           Whole(t, mrhi_accessCopySource)};
    mrhiPassId read = CopyPass(device, reads, 3);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    const mrhiTextureCopy texels = {.resource = t};
    const mrhiTexelLayout layout = {.bytesPerRow = 64, .rowsPerImage = 16};
    const mrhiExtent3d extent = {16, 16, 1};
    mrhiRequestId fromX = {0};
    mrhiRequestId fromY = {0};
    mrhiRequestId fromT = {0};
    CHECK(mrhiBeginPass(device, upload) == mrhi_success &&
              mrhiWriteBuffer(device, upload, b, 0, pattern, sizeof(pattern)) == mrhi_success &&
              mrhiWriteTexture(device, upload, &texels, pattern, sizeof(pattern), &layout,
                               &extent) == mrhi_success &&
              mrhiEndPass(device, upload) == mrhi_success,
          "uploaded");
    CHECK(mrhiBeginPass(device, move) == mrhi_success &&
              mrhiCopyBuffer(device, move, b, 256, x, 0, 256) == mrhi_success &&
              mrhiCopyBuffer(device, move, b, 768, y, 0, 256) == mrhi_success &&
              mrhiEndPass(device, move) == mrhi_success,
          "copied");
    CHECK(mrhiBeginPass(device, read) == mrhi_success &&
              mrhiReadBuffer(device, read, x, 0, 256, &fromX) == mrhi_success &&
              mrhiReadBuffer(device, read, y, 0, 256, &fromY) == mrhi_success &&
              mrhiReadTexture(device, read, &texels, &extent, &fromT) == mrhi_success &&
              mrhiEndPass(device, read) == mrhi_success,
          "read");
    Finish(device, 3);
    CHECK(Taken(device, fromX, pattern + 256, 256), "the buffer's bytes, through a transient");
    CHECK(Taken(device, fromY, pattern + 768, 256), "a second transient apart from the first");
    CHECK(Taken(device, fromT, pattern, sizeof(pattern)), "the texture's texels");
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success, "another frame");
    CHECK(mrhiImportBuffer(device, buffer, &b) == mrhi_success, "imported again");
    mrhiAccess again = Whole(b, mrhi_accessCopySource);
    read = CopyPass(device, &again, 1);
    CHECK(mrhiCompileFrame(device) == mrhi_success && mrhiBeginPass(device, read) == mrhi_success &&
              mrhiReadBuffer(device, read, b, 512, 512, &fromX) == mrhi_success &&
              mrhiEndPass(device, read) == mrhi_success,
          "read again");
    CHECK(mrhiDestroyBuffer(device, buffer) == mrhi_success, "destroyed while recorded");
    Finish(device, 1);
    CHECK(Taken(device, fromX, pattern + 512, 512), "kept across frames and until the end");
}

// A device allowed one buffer replaces it every frame: a destroyed
// buffer retires when the next frame finishes, freeing room.
static void CheckRetirement(mrhiInstance* instance, mrhiAdapterId adapter)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    def.deviceLimits.buffers = 1;
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success, "a device");
    mrhiInstanceNotification record;
    CHECK(NextInstance(instance, &record) == mrhi_success, "ready");
    for (int i = 0; i < 4; ++i)
    {
        mrhiBufferId buffer = MakeBuffer(device, 256);
        CHECK(mrhiDestroyBuffer(device, buffer) == mrhi_success, "destroyed");
        mrhiFrameDef frame = mrhiDefaultFrameDef();
        CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
                  mrhiCompileFrame(device) == mrhi_success,
              "an empty frame");
        Finish(device, 0);
    }
    mrhiDestroyDevice(device);
}

// A pipeline cache a device exported, for the next device to import.
static uint8_t s_cache[1u << 20];
static size_t s_cacheBytes;

// Takes the device's pipeline answers: ready ones, and stale ones for
// pipelines destroyed before their answer.
static void AwaitPipelines(mrhiDevice* device, uint32_t ready, uint32_t stale)
{
    mrhiDeviceNotification record;
    uint32_t counts[2] = {0};
    // The answers expected, waited for on the web; then any more.
    while (counts[0] + counts[1] < ready + stale && NextDevice(device, &record) == mrhi_success)
    {
        CHECK(record.kind == mrhi_devicePipelineReady &&
                  (record.outcome == mrhi_success || record.outcome == mrhi_errorStale),
              "a pipeline answered");
        ++counts[record.outcome == mrhi_success ? 0 : 1];
    }
    while (mrhiNextDeviceNotification(device, &record) == mrhi_success)
    {
        CHECK(record.kind == mrhi_devicePipelineReady &&
                  (record.outcome == mrhi_success || record.outcome == mrhi_errorStale),
              "a pipeline answered");
        ++counts[record.outcome == mrhi_success ? 0 : 1];
    }
    CHECK(counts[0] == ready && counts[1] == stale, "every pipeline answered once");
}

static mrhiGraphicsPipelineDef GraphicsDef(mrhiShaderId shader)
{
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    def.colorTargetCount = 1;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    return def;
}

// A shader from the conformance container, a specialized compute
// pipeline, graphics pipelines of two shapes, and one destroyed before
// its answer, which the core answers stale; then the device's pipeline
// cache.
static void CheckPipelines(mrhiDevice* device)
{
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_conformanceContainer;
    shaderDef.byteCount = sizeof(s_conformanceContainer);
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(device, &shaderDef, &shader) == mrhi_success, "a shader");
    mrhiComputePipelineDef compute = mrhiDefaultComputePipelineDef();
    compute.shader = shader;
    compute.entry = "cs";
    compute.entryLength = 2;
    const mrhiConstantValue scale = {.id = 0, .value = 3.0};
    compute.constants = &scale;
    compute.constantCount = 1;
    mrhiComputePipelineId computeId = {0};
    mrhiRequestId request = {0};
    CHECK(mrhiCreateComputePipeline(device, &compute, &computeId, &request) == mrhi_success,
          "a compute pipeline");
    mrhiGraphicsPipelineDef blended = GraphicsDef(shader);
    blended.colorTargets[0].blend = true;
    blended.colorTargets[0].color = (mrhiBlendComponent){
        .srcFactor = mrhi_blendSrcAlpha,
        .dstFactor = mrhi_blendOneMinusSrcAlpha,
        .operation = mrhi_blendAdd,
    };
    blended.depthStencilFormat = mrhi_formatDepth32Float;
    blended.depthWrite = true;
    blended.depthCompare = mrhi_compareLess;
    blended.cullMode = mrhi_cullBack;
    mrhiGraphicsPipelineId graphics[3];
    CHECK(mrhiCreateGraphicsPipeline(device, &blended, &graphics[0], &request) == mrhi_success,
          "a blended pipeline with depth");
    mrhiGraphicsPipelineDef stenciled = GraphicsDef(shader);
    stenciled.topology = mrhi_topologyTriangleStrip;
    stenciled.stripIndexFormat = mrhi_indexUint32;
    stenciled.sampleCount = 4;
    stenciled.alphaToCoverage = true;
    stenciled.depthStencilFormat = mrhi_formatDepthStencil;
    stenciled.depthCompare = mrhi_compareGreaterEqual;
    stenciled.stencilFront.compare = mrhi_compareEqual;
    stenciled.stencilFront.passOp = mrhi_stencilIncrementWrap;
    stenciled.depthBias = 2;
    stenciled.depthBiasSlopeScale = 1.5f;
    CHECK(mrhiCreateGraphicsPipeline(device, &stenciled, &graphics[1], &request) == mrhi_success,
          "a stenciled multisampled strip");
    CHECK(mrhiCreateGraphicsPipeline(device, &blended, &graphics[2], &request) == mrhi_success,
          "one more");
    CHECK(mrhiDestroyGraphicsPipeline(device, graphics[2]) == mrhi_success,
          "destroyed while pending");
    AwaitPipelines(device, 3, 1);
    CHECK(mrhiDestroyShader(device, shader) == mrhi_success, "the shader destroyed");
    CHECK(mrhiDestroyComputePipeline(device, computeId) == mrhi_success, "outlived its shader");
    CHECK(mrhiGetPipelineCache(device, s_cache, sizeof(s_cache), &s_cacheBytes) == mrhi_success,
          "the cache exported");
}

// What the drawing checks use: the conformance shader's pipelines, a
// uniform color, a white texel, a sampler, an 8 by 8 target and a
// storage buffer. The placed pipelines draw vertices from a buffer in
// the root block's color with depth and stencil: one writes both, the
// other tests them and blends with the blend constant.
typedef struct Scene
{
    mrhiDevice* device;
    mrhiGraphicsPipelineId draw;
    mrhiGraphicsPipelineId culled;
    mrhiComputePipelineId compute;
    mrhiGraphicsPipelineId placed;
    mrhiGraphicsPipelineId tested;
    mrhiComputePipelineId adding;
    mrhiBufferId uniform;
    mrhiTextureId white;
    mrhiSamplerId sampler;
    mrhiTextureId target;
    mrhiBufferId data;
    // The same objects imported into the open frame.
    mrhiResourceId u;
    mrhiResourceId w;
    mrhiResourceId t;
    mrhiResourceId d;
} Scene;

static const float kColor[4] = {1.0f, 0.2f, 0.6f, 1.0f};

static mrhiTextureId MakeImage(mrhiDevice* device, uint32_t size, mrhiTextureUsage usage)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = size;
    def.height = size;
    def.usage = usage;
    LABEL(def, "image");
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(device, &def, &texture) == mrhi_success, "a texture");
    return texture;
}

// Where the heap check's objects sit in its heap: the clamping sampler
// it reads, and a repeating one it must not.
enum
{
    HEAP_TEXTURE = 3,
    HEAP_SAMPLER = 2,
    HEAP_OTHER_SAMPLER = 0,
    HEAP_BUFFER = 7,
};

// Makes the heap check's heap with its entries: the texture's view, the
// buffer and two samplers.
static mrhiHeapId MakeHeap(mrhiDevice* device, mrhiTextureId texture, mrhiBufferId buffer)
{
    mrhiViewDef viewDef = mrhiDefaultViewDef();
    viewDef.texture = texture;
    mrhiViewId view = {0};
    mrhiSamplerDef repeatDef = mrhiDefaultSamplerDef();
    repeatDef.addressU = mrhi_addressRepeat;
    mrhiSamplerId repeat = {0};
    mrhiSamplerDef clampDef = mrhiDefaultSamplerDef();
    mrhiSamplerId clamp = {0};
    mrhiHeapDef heapDef = mrhiDefaultHeapDef();
    heapDef.entries = 16;
    heapDef.samplers = 4;
    LABEL(heapDef, "heap");
    mrhiHeapId heap = {0};
    CHECK(mrhiCreateView(device, &viewDef, &view) == mrhi_success &&
              mrhiCreateSampler(device, &repeatDef, &repeat) == mrhi_success &&
              mrhiCreateSampler(device, &clampDef, &clamp) == mrhi_success &&
              mrhiCreateHeap(device, &heapDef, &heap) == mrhi_success,
          "a view, two samplers and a heap");
    const mrhiHeapEntry sampled = {.kind = mrhi_heapSampledTexture, .view = view};
    const mrhiHeapEntry storage = {
        .kind = mrhi_heapStorageBuffer,
        .buffer = buffer,
        .size = MRHI_WHOLE_SIZE,
        .writable = true,
    };
    CHECK(mrhiSetHeapEntry(device, heap, HEAP_TEXTURE, &sampled) == mrhi_success &&
              mrhiSetHeapEntry(device, heap, HEAP_BUFFER, &storage) == mrhi_success &&
              mrhiSetHeapSampler(device, heap, HEAP_SAMPLER, clamp) == mrhi_success &&
              mrhiSetHeapSampler(device, heap, HEAP_OTHER_SAMPLER, repeat) == mrhi_success,
          "the entries");
    return heap;
}

// Texels uploaded to a texture sealed in the same frame, then sampled
// past its right edge through a heap with a clamping sampler from it by
// a compute pipeline that writes the texel to a buffer from the heap,
// all named by index: the sealed texture undeclared, the buffer
// declared. Needs heterogeneous heaps, which MAUL_RHI_REQUIRE_BINDLESS
// requires of every native adapter.
static void CheckHeaps(mrhiInstance* instance, mrhiAdapterId adapter, bool native)
{
    mrhiFeatures features;
    CHECK(mrhiGetAdapterFeatures(instance, adapter, &features) == mrhi_success, "features");
    if (!features.bindlessHeterogeneous)
    {
        const char* required = getenv("MAUL_RHI_REQUIRE_BINDLESS");
        CHECK(!native || required == nullptr || required[0] == '\0', "bindless heaps, required");
        return;
    }
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    def.features.bindlessSampling = true;
    def.features.bindlessHeterogeneous = true;
    def.limits.heapSize = 16;
    def.limits.samplerHeapSize = 4;
    LABEL(def, "heaps");
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success, "a device");
    mrhiInstanceNotification record;
    CHECK(NextInstance(instance, &record) == mrhi_success && record.outcome == mrhi_success,
          "ready");
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_bindlessContainer;
    shaderDef.byteCount = sizeof(s_bindlessContainer);
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(device, &shaderDef, &shader) == mrhi_success, "the bindless shader");
    mrhiComputePipelineDef computeDef = mrhiDefaultComputePipelineDef();
    computeDef.shader = shader;
    computeDef.entry = "cs";
    computeDef.entryLength = 2;
    mrhiComputePipelineId pipeline = {0};
    CHECK(mrhiCreateComputePipeline(device, &computeDef, &pipeline, &request) == mrhi_success,
          "a pipeline reading the heap");
    AwaitPipelines(device, 1, 0);
    mrhiTextureId texture = MakeImage(device, 2, mrhi_textureSampled | mrhi_textureCopyDestination);
    mrhiBufferId buffer = MakeBuffer(device, 256);
    mrhiHeapId heap = MakeHeap(device, texture, buffer);
    // Each row the texel a repeating sampler would read, then the one the
    // clamping sampler reads.
    static const uint8_t pixels[16] = {10, 20, 30, 255, 255, 51, 153, 255,
                                       10, 20, 30, 255, 255, 51, 153, 255};
    const uint8_t* clamped = pixels + 4;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId t = {0};
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
              mrhiImportTexture(device, texture, &t) == mrhi_success,
          "an upload frame");
    mrhiAccess upload = Whole(t, mrhi_accessCopyDestination);
    mrhiPassId copy = CopyPass(device, &upload, 1);
    const mrhiTextureCopy texels = {.resource = t};
    const mrhiTexelLayout layout = {.bytesPerRow = 8, .rowsPerImage = 2};
    const mrhiExtent3d extent = {2, 2, 1};
    CHECK(mrhiSealResource(device, t) == mrhi_success && mrhiCompileFrame(device) == mrhi_success &&
              mrhiBeginPass(device, copy) == mrhi_success &&
              mrhiWriteTexture(device, copy, &texels, pixels, sizeof(pixels), &layout, &extent) ==
                  mrhi_success &&
              mrhiEndPass(device, copy) == mrhi_success,
          "uploaded and sealed");
    Finish(device, 0);
    mrhiResourceId b = {0};
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
              mrhiImportBuffer(device, buffer, &b) == mrhi_success,
          "a frame reading the heap");
    mrhiPassDef passDef = mrhiDefaultPassDef();
    mrhiAccess write = Whole(b, mrhi_accessStorageWrite);
    passDef.accesses = &write;
    passDef.accessCount = 1;
    passDef.heap = heap;
    LABEL(passDef, "bindless");
    mrhiPassId dispatch = {0};
    CHECK(mrhiAddPass(device, &passDef, &dispatch) == mrhi_success, "a pass naming the heap");
    mrhiAccess read = Whole(b, mrhi_accessCopySource);
    mrhiPassId readPass = CopyPass(device, &read, 1);
    const uint32_t indices[4] = {HEAP_TEXTURE, HEAP_SAMPLER, HEAP_BUFFER, 0};
    mrhiRequestId written = {0};
    CHECK(mrhiCompileFrame(device) == mrhi_success &&
              mrhiBeginPass(device, dispatch) == mrhi_success &&
              mrhiSetComputePipeline(device, dispatch, pipeline) == mrhi_success &&
              mrhiSetRootBlock(device, dispatch, 0, indices, sizeof(indices)) == mrhi_success &&
              mrhiDispatch(device, dispatch, 1, 1, 1) == mrhi_success &&
              mrhiEndPass(device, dispatch) == mrhi_success &&
              mrhiBeginPass(device, readPass) == mrhi_success &&
              mrhiReadBuffer(device, readPass, b, 0, 4, &written) == mrhi_success &&
              mrhiEndPass(device, readPass) == mrhi_success,
          "dispatched and read");
    Finish(device, 1);
    CHECK(Taken(device, written, clamped, 4), "the clamped texel, through the heaps");
    CHECK(mrhiDestroyHeap(device, heap) == mrhi_success, "the heap destroyed");
    mrhiDestroyDevice(device);
}

// The placed pipelines: vertices of four floats each, depth and stencil
// written, then tested.
static void MakePlaced(Scene* scene, mrhiShaderId shader)
{
    static const mrhiVertexBufferLayout buffer = {.stride = 16};
    static const mrhiVertexAttribute position = {.format = mrhi_vertexFloat32x4};
    mrhiGraphicsPipelineDef def = GraphicsDef(shader);
    def.vertexEntry = "vp";
    def.fragmentEntry = "fr";
    def.vertexBuffers = &buffer;
    def.vertexBufferCount = 1;
    def.vertexAttributes = &position;
    def.vertexAttributeCount = 1;
    def.depthStencilFormat = mrhi_formatDepthStencil;
    def.depthWrite = true;
    def.depthCompare = mrhi_compareLess;
    def.stencilFront =
        (mrhiStencilFace){.compare = mrhi_compareAlways, .passOp = mrhi_stencilReplace};
    def.stencilBack = def.stencilFront;
    LABEL(def, "placed");
    mrhiRequestId request = {0};
    CHECK(mrhiCreateGraphicsPipeline(scene->device, &def, &scene->placed, &request) == mrhi_success,
          "a pipeline writing depth and stencil");
    def.depthWrite = false;
    def.stencilFront = (mrhiStencilFace){.compare = mrhi_compareEqual};
    def.stencilBack = def.stencilFront;
    def.colorTargets[0].blend = true;
    def.colorTargets[0].color = (mrhiBlendComponent){
        .srcFactor = mrhi_blendConstant,
        .dstFactor = mrhi_blendZero,
        .operation = mrhi_blendAdd,
    };
    LABEL(def, "tested");
    CHECK(mrhiCreateGraphicsPipeline(scene->device, &def, &scene->tested, &request) == mrhi_success,
          "a pipeline testing depth and stencil");
}

static void MakeScene(Scene* scene)
{
    mrhiDevice* device = scene->device;
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_conformanceContainer;
    shaderDef.byteCount = sizeof(s_conformanceContainer);
    LABEL(shaderDef, "conformance");
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(device, &shaderDef, &shader) == mrhi_success, "a shader");
    mrhiRequestId request = {0};
    mrhiGraphicsPipelineDef draw = GraphicsDef(shader);
    LABEL(draw, "triangle");
    CHECK(mrhiCreateGraphicsPipeline(device, &draw, &scene->draw, &request) == mrhi_success,
          "a pipeline");
    draw.cullMode = mrhi_cullBack;
    draw.frontFace = mrhi_frontClockwise;
    CHECK(mrhiCreateGraphicsPipeline(device, &draw, &scene->culled, &request) == mrhi_success,
          "a culling pipeline");
    mrhiComputePipelineDef compute = mrhiDefaultComputePipelineDef();
    compute.shader = shader;
    compute.entry = "cs";
    compute.entryLength = 2;
    LABEL(compute, "scale");
    const mrhiConstantValue scale = {.id = 0, .value = 3.0};
    compute.constants = &scale;
    compute.constantCount = 1;
    CHECK(mrhiCreateComputePipeline(device, &compute, &scene->compute, &request) == mrhi_success,
          "a compute pipeline");
    compute.entry = "ca";
    compute.constantCount = 0;
    CHECK(mrhiCreateComputePipeline(device, &compute, &scene->adding, &request) == mrhi_success,
          "an adding pipeline");
    MakePlaced(scene, shader);
    AwaitPipelines(device, 6, 0);
    CHECK(mrhiDestroyShader(device, shader) == mrhi_success, "the shader destroyed");
    mrhiBufferDef uniform = mrhiDefaultBufferDef();
    uniform.size = 16;
    uniform.usage = mrhi_bufferUniform | mrhi_bufferCopyDestination;
    LABEL(uniform, "scene color");
    CHECK(mrhiCreateBuffer(device, &uniform, &scene->uniform) == mrhi_success, "a uniform");
    scene->white = MakeImage(device, 1, mrhi_textureSampled | mrhi_textureCopyDestination);
    scene->target = MakeImage(device, 8, mrhi_textureRenderTarget | mrhi_textureCopySource);
    mrhiSamplerDef sampler = mrhiDefaultSamplerDef();
    sampler.magFilter = mrhi_filterLinear;
    LABEL(sampler, "linear");
    CHECK(mrhiCreateSampler(device, &sampler, &scene->sampler) == mrhi_success, "a sampler");
    scene->data = MakeBuffer(device, 32);
}

// Begins a frame with the scene's objects imported.
static void BeginScene(Scene* scene)
{
    mrhiDevice* device = scene->device;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
              mrhiImportBuffer(device, scene->uniform, &scene->u) == mrhi_success &&
              mrhiImportTexture(device, scene->white, &scene->w) == mrhi_success &&
              mrhiImportTexture(device, scene->target, &scene->t) == mrhi_success &&
              mrhiImportBuffer(device, scene->data, &scene->d) == mrhi_success,
          "a frame of the scene");
}

// A pass drawing the target, cleared to black, with the scene's
// accesses: table 0 holds the uniform and the storage buffer.
static mrhiPassDef DrawDef(const Scene* scene, const mrhiAccess* accesses)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){
        .resource = scene->t,
        .load = mrhi_loadClear,
        .store = mrhi_storeKeep,
        .clear = {0.0f, 0.0f, 0.0f, 1.0f},
    };
    def.colorTargetCount = 1;
    def.accesses = accesses;
    def.accessCount = 3;
    return def;
}

static mrhiPassId DrawPass(const Scene* scene, const mrhiAccess* accesses)
{
    mrhiPassDef def = DrawDef(scene, accesses);
    LABEL(def, "draw");
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(scene->device, &def, &pass) == mrhi_success, "a drawing pass");
    return pass;
}

// Binds both of the container's tables, which every pass using one of
// its pipelines sets.
static void BindScene(const Scene* scene, mrhiPassId pass)
{
    const mrhiBinding table0[2] = {
        {.slot = 0, .resource = scene->u, .size = MRHI_WHOLE_SIZE},
        {.slot = 1, .resource = scene->d, .size = MRHI_WHOLE_SIZE},
    };
    CHECK(mrhiSetBindings(scene->device, pass, 0, table0, 2) == mrhi_success, "table 0");
    const mrhiBinding table1[2] = {
        {.slot = 0,
         .resource = scene->w,
         .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING}},
        {.slot = 1, .sampler = scene->sampler},
    };
    CHECK(mrhiSetBindings(scene->device, pass, 1, table1, 2) == mrhi_success, "table 1");
}

// Reads the target back: its pixels, 4 bytes each, rows of 8.
static mrhiRequestId ReadTarget(const Scene* scene, mrhiPassId pass)
{
    const mrhiTextureCopy source = {.resource = scene->t};
    const mrhiExtent3d extent = {8, 8, 1};
    mrhiRequestId request = {0};
    CHECK(mrhiReadTexture(scene->device, pass, &source, &extent, &request) == mrhi_success,
          "a target read");
    return request;
}

// Whether a pixel is a color, within one step.
static bool IsNear(const uint8_t* pixel, const uint8_t* expected)
{
    for (int i = 0; i < 4; ++i)
    {
        int difference = pixel[i] - expected[i];
        if (difference < -1 || difference > 1)
        {
            return false;
        }
    }
    return true;
}

static bool IsColor(const uint8_t* pixel)
{
    static const uint8_t kExpected[4] = {255, 51, 153, 255};
    return IsNear(pixel, kExpected);
}

static bool IsBlack(const uint8_t* pixel)
{
    return pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255;
}

// One frame uploads the scene, draws the upper left half of the target
// (vertices 2 to 4, the triangle y > x with +Y up), scales the storage buffer in a
// compute pass and reads both back.
static void CheckDrawFrame(Scene* scene)
{
    mrhiDevice* device = scene->device;
    BeginScene(scene);
    mrhiAccess uploads[3] = {Whole(scene->u, mrhi_accessCopyDestination),
                             Whole(scene->w, mrhi_accessCopyDestination),
                             Whole(scene->d, mrhi_accessCopyDestination)};
    mrhiPassId upload = CopyPass(device, uploads, 3);
    mrhiAccess draws[3] = {Whole(scene->u, mrhi_accessUniform),
                           Whole(scene->d, mrhi_accessStorageReadWrite),
                           Whole(scene->w, mrhi_accessSampled)};
    mrhiPassId draw = DrawPass(scene, draws);
    mrhiPassId compute = CopyPass(device, draws, 3);
    mrhiAccess reads[2] = {Whole(scene->t, mrhi_accessCopySource),
                           Whole(scene->d, mrhi_accessCopySource)};
    mrhiPassId read = CopyPass(device, reads, 2);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    const uint8_t white[4] = {255, 255, 255, 255};
    uint32_t values[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const mrhiTextureCopy texel = {.resource = scene->w};
    const mrhiTexelLayout layout = {.bytesPerRow = 4, .rowsPerImage = 1};
    const mrhiExtent3d one = {1, 1, 1};
    CHECK(
        mrhiBeginPass(device, upload) == mrhi_success &&
            mrhiWriteBuffer(device, upload, scene->u, 0, kColor, sizeof(kColor)) == mrhi_success &&
            mrhiWriteTexture(device, upload, &texel, white, 4, &layout, &one) == mrhi_success &&
            mrhiWriteBuffer(device, upload, scene->d, 0, values, sizeof(values)) == mrhi_success &&
            mrhiEndPass(device, upload) == mrhi_success,
        "uploaded");
    const float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    CHECK(mrhiBeginPass(device, draw) == mrhi_success &&
              mrhiSetGraphicsPipeline(device, draw, scene->draw) == mrhi_success &&
              mrhiSetRootBlock(device, draw, 0, tint, sizeof(tint)) == mrhi_success,
          "a pipeline set");
    BindScene(scene, draw);
    CHECK(mrhiPushDebugGroup(device, draw, "half", 4) == mrhi_success &&
              mrhiInsertDebugMarker(device, draw, "upper left", 10) == mrhi_success &&
              mrhiDraw(device, draw, 3, 1, 2, 0) == mrhi_success &&
              mrhiPopDebugGroup(device, draw) == mrhi_success &&
              mrhiEndPass(device, draw) == mrhi_success,
          "drawn");
    CHECK(mrhiBeginPass(device, compute) == mrhi_success &&
              mrhiSetComputePipeline(device, compute, scene->compute) == mrhi_success,
          "a compute pipeline set");
    BindScene(scene, compute);
    CHECK(mrhiPushDebugGroup(device, compute, "scale", 5) == mrhi_success &&
              mrhiDispatch(device, compute, 1, 1, 1) == mrhi_success &&
              mrhiPopDebugGroup(device, compute) == mrhi_success &&
              mrhiEndPass(device, compute) == mrhi_success,
          "dispatched");
    mrhiRequestId pixels = {0};
    mrhiRequestId scaled = {0};
    CHECK(mrhiBeginPass(device, read) == mrhi_success, "reading");
    pixels = ReadTarget(scene, read);
    CHECK(mrhiReadBuffer(device, read, scene->d, 0, sizeof(values), &scaled) == mrhi_success &&
              mrhiEndPass(device, read) == mrhi_success,
          "read");
    Finish(device, 2);
    uint8_t image[256];
    size_t size = 0;
    CHECK(mrhiTakeReadback(device, pixels, image, sizeof(image), &size) == mrhi_success &&
              size == sizeof(image),
          "the pixels");
    // Pixel (1, 0) lies above y = x with +Y up, and (6, 7) below it.
    CHECK(!s_runs || (IsColor(&image[4]) && IsBlack(&image[(7 * 8 + 6) * 4])),
          "the triangle drawn with +Y up");
    for (int i = 0; i < 8; ++i)
    {
        values[i] *= 3;
    }
    CHECK(Taken(device, scaled, (const uint8_t*)values, sizeof(values)), "scaled by 3");
}

// A counter-clockwise triangle with clockwise front faces and back
// faces culled draws nothing.
static void CheckCulling(Scene* scene)
{
    mrhiDevice* device = scene->device;
    BeginScene(scene);
    mrhiAccess draws[3] = {Whole(scene->u, mrhi_accessUniform),
                           Whole(scene->d, mrhi_accessStorageReadWrite),
                           Whole(scene->w, mrhi_accessSampled)};
    mrhiPassId draw = DrawPass(scene, draws);
    mrhiAccess reads = Whole(scene->t, mrhi_accessCopySource);
    mrhiPassId read = CopyPass(device, &reads, 1);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    const float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    CHECK(mrhiBeginPass(device, draw) == mrhi_success &&
              mrhiSetGraphicsPipeline(device, draw, scene->culled) == mrhi_success &&
              mrhiSetRootBlock(device, draw, 0, tint, sizeof(tint)) == mrhi_success,
          "a pipeline set");
    BindScene(scene, draw);
    CHECK(mrhiDraw(device, draw, 3, 1, 0, 0) == mrhi_success &&
              mrhiEndPass(device, draw) == mrhi_success &&
              mrhiBeginPass(device, read) == mrhi_success,
          "drawn");
    mrhiRequestId pixels = ReadTarget(scene, read);
    CHECK(mrhiEndPass(device, read) == mrhi_success, "read");
    Finish(device, 1);
    uint8_t image[256];
    size_t size = 0;
    CHECK(mrhiTakeReadback(device, pixels, image, sizeof(image), &size) == mrhi_success,
          "the pixels");
    bool black = true;
    for (int i = 0; i < 64; ++i)
    {
        black = black && IsBlack(&image[i * 4]);
    }
    CHECK(!s_runs || black, "a counter-clockwise triangle culled as a back face");
}

static mrhiQuerySetId MakeQuerySet(mrhiDevice* device, mrhiQueryType type, uint32_t count)
{
    mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
    LABEL(def, "queries");
    def.type = type;
    def.count = count;
    mrhiQuerySetId set = {0};
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_success, "a query set");
    return set;
}

// Draws a triangle from firstVertex with a pipeline of the scene, its
// root block and tables set.
static void DrawWith(const Scene* scene, mrhiPassId pass, mrhiGraphicsPipelineId pipeline,
                     uint32_t firstVertex)
{
    const float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    CHECK(mrhiSetGraphicsPipeline(scene->device, pass, pipeline) == mrhi_success &&
              mrhiSetRootBlock(scene->device, pass, 0, tint, sizeof(tint)) == mrhi_success,
          "a pipeline set");
    BindScene(scene, pass);
    CHECK(mrhiDraw(scene->device, pass, 3, 1, firstVertex, 0) == mrhi_success, "drawn");
}

// A frame that writes none of a set's queries resolves them all to 0,
// whatever an earlier frame wrote.
static void CheckUnwritten(Scene* scene, mrhiQuerySetId occlusion, mrhiBufferId results)
{
    mrhiDevice* device = scene->device;
    BeginScene(scene);
    mrhiResourceId r = {0};
    CHECK(mrhiImportBuffer(device, results, &r) == mrhi_success, "imported");
    mrhiAccess fill = Whole(r, mrhi_accessCopyDestination);
    mrhiPassId filled = CopyPass(device, &fill, 1);
    mrhiAccess resolves = Whole(r, mrhi_accessQueryResolve);
    mrhiPassId resolve = CopyPass(device, &resolves, 1);
    mrhiAccess reads = Whole(r, mrhi_accessCopySource);
    mrhiPassId read = CopyPass(device, &reads, 1);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    uint8_t ones[24];
    memset(ones, 0xFF, sizeof(ones));
    mrhiRequestId request = {0};
    CHECK(mrhiBeginPass(device, filled) == mrhi_success &&
              mrhiWriteBuffer(device, filled, r, 0, ones, sizeof(ones)) == mrhi_success &&
              mrhiEndPass(device, filled) == mrhi_success &&
              mrhiBeginPass(device, resolve) == mrhi_success &&
              mrhiResolveQueries(device, resolve, occlusion, 0, 3, r, 0) == mrhi_success &&
              mrhiEndPass(device, resolve) == mrhi_success &&
              mrhiBeginPass(device, read) == mrhi_success &&
              mrhiReadBuffer(device, read, r, 0, sizeof(ones), &request) == mrhi_success &&
              mrhiEndPass(device, read) == mrhi_success,
          "resolved");
    Finish(device, 1);
    const uint8_t zeros[24] = {0};
    CHECK(Taken(device, request, zeros, sizeof(zeros)), "every query 0");
}

// Occlusion queries around a draw that covers pixels and one culled
// (the counter-clockwise triangle, with clockwise front faces),
// a third query left unwritten, and the pass's timestamps when the
// device has them; resolved over bytes set to 0xFF, so that the query
// left unwritten shows it resolves to 0.
static void CheckQueries(Scene* scene, bool timestamps)
{
    mrhiDevice* device = scene->device;
    mrhiQuerySetId occlusion = MakeQuerySet(device, mrhi_queryOcclusion, 3);
    mrhiQuerySetId times = {0};
    if (timestamps)
    {
        times = MakeQuerySet(device, mrhi_queryTimestamp, 2);
    }
    mrhiBufferDef resultsDef = mrhiDefaultBufferDef();
    resultsDef.size = 512;
    resultsDef.usage = mrhi_bufferQueryResolve | mrhi_bufferCopySource | mrhi_bufferCopyDestination;
    mrhiBufferId results = {0};
    CHECK(mrhiCreateBuffer(device, &resultsDef, &results) == mrhi_success, "a results buffer");
    BeginScene(scene);
    mrhiResourceId r = {0};
    CHECK(mrhiImportBuffer(device, results, &r) == mrhi_success, "imported");
    mrhiAccess fill = Whole(r, mrhi_accessCopyDestination);
    mrhiPassId filled = CopyPass(device, &fill, 1);
    mrhiAccess draws[3] = {Whole(scene->u, mrhi_accessUniform),
                           Whole(scene->d, mrhi_accessStorageReadWrite),
                           Whole(scene->w, mrhi_accessSampled)};
    mrhiPassDef def = DrawDef(scene, draws);
    def.occlusionQuerySet = occlusion;
    if (timestamps)
    {
        def.timestampQuerySet = times;
        def.timestampBegin = 0;
        def.timestampEnd = 1;
    }
    mrhiPassId draw = {0};
    CHECK(mrhiAddPass(device, &def, &draw) == mrhi_success, "a pass with queries");
    mrhiAccess resolves = Whole(r, mrhi_accessQueryResolve);
    mrhiPassId resolve = CopyPass(device, &resolves, 1);
    mrhiAccess reads = Whole(r, mrhi_accessCopySource);
    mrhiPassId read = CopyPass(device, &reads, 1);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    uint8_t ones[512];
    memset(ones, 0xFF, sizeof(ones));
    CHECK(mrhiBeginPass(device, filled) == mrhi_success &&
              mrhiWriteBuffer(device, filled, r, 0, ones, sizeof(ones)) == mrhi_success &&
              mrhiEndPass(device, filled) == mrhi_success,
          "filled");
    CHECK(mrhiBeginPass(device, draw) == mrhi_success &&
              mrhiBeginOcclusionQuery(device, draw, 0) == mrhi_success,
          "query 0 begun");
    DrawWith(scene, draw, scene->draw, 2);
    CHECK(mrhiEndOcclusionQuery(device, draw) == mrhi_success &&
              mrhiBeginOcclusionQuery(device, draw, 1) == mrhi_success,
          "query 1 begun");
    DrawWith(scene, draw, scene->culled, 0);
    CHECK(mrhiEndOcclusionQuery(device, draw) == mrhi_success &&
              mrhiEndPass(device, draw) == mrhi_success,
          "queried");
    CHECK(mrhiBeginPass(device, resolve) == mrhi_success &&
              mrhiResolveQueries(device, resolve, occlusion, 0, 3, r, 0) == mrhi_success &&
              (!timestamps ||
               mrhiResolveQueries(device, resolve, times, 0, 2, r, 256) == mrhi_success) &&
              mrhiEndPass(device, resolve) == mrhi_success,
          "resolved");
    mrhiRequestId request = {0};
    CHECK(mrhiBeginPass(device, read) == mrhi_success &&
              mrhiReadBuffer(device, read, r, 0, sizeof(ones), &request) == mrhi_success &&
              mrhiEndPass(device, read) == mrhi_success,
          "read");
    Finish(device, 1);
    uint64_t values[64];
    size_t size = 0;
    CHECK(mrhiTakeReadback(device, request, values, sizeof(values), &size) == mrhi_success &&
              size == sizeof(values),
          "the values");
    CHECK(!s_runs || (values[0] != 0 && values[1] == 0 && values[2] == 0),
          "samples passed only for the drawn triangle, and 0 for the query left unwritten");
    CHECK(!s_runs || !timestamps || (values[32] != 0 && values[33] >= values[32]),
          "the pass ends after it starts");
    CheckUnwritten(scene, occlusion, results);
    CHECK(mrhiDestroyQuerySet(device, occlusion) == mrhi_success &&
              (!timestamps || mrhiDestroyQuerySet(device, times) == mrhi_success) &&
              mrhiDestroyBuffer(device, results) == mrhi_success,
          "destroyed");
}

// The placed checks' vertices: a spare, a quad at depth 0.5 drawn
// indexed from vertex 1, and triangles over the target at 0.75 and 0.4.
static const float kPlaced[11][4] = {
    {0.0f, 0.0f, 0.0f, 1.0f},   {-1.0f, -1.0f, 0.5f, 1.0f}, {1.0f, -1.0f, 0.5f, 1.0f},
    {-1.0f, 1.0f, 0.5f, 1.0f},  {1.0f, 1.0f, 0.5f, 1.0f},   {-1.0f, -1.0f, 0.75f, 1.0f},
    {3.0f, -1.0f, 0.75f, 1.0f}, {-1.0f, 3.0f, 0.75f, 1.0f}, {-1.0f, -1.0f, 0.4f, 1.0f},
    {3.0f, -1.0f, 0.4f, 1.0f},  {-1.0f, 3.0f, 0.4f, 1.0f},
};

static mrhiResourceId Declared(mrhiDevice* device, uint64_t size)
{
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = size;
    mrhiResourceId buffer = {0};
    CHECK(mrhiDeclareBuffer(device, &def, &buffer) == mrhi_success, "a declared buffer");
    return buffer;
}

// A pass drawing the target and a declared depth and stencil texture,
// loading both or clearing them, with the scene's accesses and a vertex
// buffer and one other.
static mrhiPassId PlacedPass(const Scene* scene, mrhiResourceId depth, bool load,
                             mrhiAccess vertices, mrhiAccess other)
{
    const mrhiAccess accesses[5] = {Whole(scene->u, mrhi_accessUniform),
                                    Whole(scene->d, mrhi_accessStorageReadWrite),
                                    Whole(scene->w, mrhi_accessSampled), vertices, other};
    mrhiPassDef def = mrhiDefaultPassDef();
    mrhiLoadOp op = load ? mrhi_loadKeep : mrhi_loadClear;
    def.colorTargets[0] = (mrhiColorTarget){
        .resource = scene->t,
        .load = op,
        .clear = {0.0f, 0.0f, 0.0f, 1.0f},
    };
    def.colorTargetCount = 1;
    def.depthTarget = (mrhiDepthTarget){
        .resource = depth,
        .depthLoad = op,
        .clearDepth = 1.0f,
        .stencilLoad = op,
    };
    def.accesses = accesses;
    def.accessCount = 5;
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(scene->device, &def, &pass) == mrhi_success, "a placed pass");
    return pass;
}

// The first placed pass: red where the indexed quad covers the left half
// through the viewport, stencil 1 there; then green over the upper half
// through the scissor, farther, so only the upper right passes the
// depth test and takes stencil 2.
static void DrawPlaced(const Scene* scene, mrhiPassId pass, mrhiResourceId vertices,
                       mrhiResourceId indices)
{
    mrhiDevice* device = scene->device;
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    const float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    const mrhiViewport left = {0.0f, 0.0f, 4.0f, 8.0f, 0.0f, 1.0f};
    const mrhiViewport whole = {0.0f, 0.0f, 8.0f, 8.0f, 0.0f, 1.0f};
    const mrhiScissorRect upper = {0, 0, 8, 4};
    CHECK(mrhiBeginPass(device, pass) == mrhi_success &&
              mrhiSetGraphicsPipeline(device, pass, scene->placed) == mrhi_success,
          "a placed pipeline");
    BindScene(scene, pass);
    CHECK(mrhiSetVertexBuffer(device, pass, 0, vertices, 0, sizeof(kPlaced)) == mrhi_success &&
              mrhiSetIndexBuffer(device, pass, indices, mrhi_indexUint16, 0, 12) == mrhi_success &&
              mrhiSetViewport(device, pass, &left) == mrhi_success &&
              mrhiSetStencilReference(device, pass, 1) == mrhi_success &&
              mrhiSetRootBlock(device, pass, 0, red, sizeof(red)) == mrhi_success &&
              mrhiDrawIndexed(device, pass, 6, 1, 0, 1, 0) == mrhi_success &&
              mrhiSetViewport(device, pass, &whole) == mrhi_success &&
              mrhiSetScissor(device, pass, &upper) == mrhi_success &&
              mrhiSetStencilReference(device, pass, 2) == mrhi_success &&
              mrhiSetRootBlock(device, pass, 0, green, sizeof(green)) == mrhi_success &&
              mrhiDraw(device, pass, 3, 1, 5, 0) == mrhi_success &&
              mrhiEndPass(device, pass) == mrhi_success,
          "placed");
}

// Render state on the target: a first pass writes depth and stencil,
// kept for a second that loads them and draws, from indirect arguments
// past a draw of nothing, blue scaled by the blend constant only where
// both tests pass, which is the upper right: the left half stays red,
// the lower right black.
static void CheckRenderState(Scene* scene)
{
    mrhiDevice* device = scene->device;
    BeginScene(scene);
    const uint16_t quad[6] = {0, 1, 2, 2, 1, 3};
    const uint32_t arguments[8] = {0, 0, 0, 0, 3, 1, 8, 0};
    mrhiResourceId v = Declared(device, sizeof(kPlaced));
    mrhiResourceId i = Declared(device, sizeof(quad));
    mrhiResourceId a = Declared(device, sizeof(arguments));
    mrhiTextureDef depthDef = mrhiDefaultTextureDef();
    depthDef.format = mrhi_formatDepthStencil;
    depthDef.width = 8;
    depthDef.height = 8;
    mrhiResourceId z = {0};
    CHECK(mrhiDeclareTexture(device, &depthDef, &z) == mrhi_success, "a depth target");
    mrhiAccess writes[3] = {Whole(v, mrhi_accessCopyDestination),
                            Whole(i, mrhi_accessCopyDestination),
                            Whole(a, mrhi_accessCopyDestination)};
    mrhiPassId upload = CopyPass(device, writes, 3);
    mrhiPassId placed =
        PlacedPass(scene, z, false, Whole(v, mrhi_accessVertex), Whole(i, mrhi_accessIndex));
    mrhiPassId tested =
        PlacedPass(scene, z, true, Whole(v, mrhi_accessVertex), Whole(a, mrhi_accessIndirect));
    mrhiAccess read = Whole(scene->t, mrhi_accessCopySource);
    mrhiPassId reading = CopyPass(device, &read, 1);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    CHECK(mrhiBeginPass(device, upload) == mrhi_success &&
              mrhiWriteBuffer(device, upload, v, 0, kPlaced, sizeof(kPlaced)) == mrhi_success &&
              mrhiWriteBuffer(device, upload, i, 0, quad, sizeof(quad)) == mrhi_success &&
              mrhiWriteBuffer(device, upload, a, 0, arguments, sizeof(arguments)) == mrhi_success &&
              mrhiEndPass(device, upload) == mrhi_success,
          "uploaded");
    DrawPlaced(scene, placed, v, i);
    const float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    const mrhiClearColor constant = {1.0f, 1.0f, 0.6f, 1.0f};
    CHECK(mrhiBeginPass(device, tested) == mrhi_success &&
              mrhiSetGraphicsPipeline(device, tested, scene->tested) == mrhi_success,
          "a tested pipeline");
    BindScene(scene, tested);
    CHECK(mrhiSetVertexBuffer(device, tested, 0, v, 0, sizeof(kPlaced)) == mrhi_success &&
              mrhiSetStencilReference(device, tested, 2) == mrhi_success &&
              mrhiSetBlendConstant(device, tested, &constant) == mrhi_success &&
              mrhiSetRootBlock(device, tested, 0, blue, sizeof(blue)) == mrhi_success &&
              mrhiDrawIndirect(device, tested, a, 16) == mrhi_success &&
              mrhiEndPass(device, tested) == mrhi_success,
          "tested");
    CHECK(mrhiBeginPass(device, reading) == mrhi_success, "reading");
    mrhiRequestId pixels = ReadTarget(scene, reading);
    CHECK(mrhiEndPass(device, reading) == mrhi_success, "read");
    Finish(device, 1);
    uint8_t image[256];
    size_t size = 0;
    CHECK(mrhiTakeReadback(device, pixels, image, sizeof(image), &size) == mrhi_success &&
              size == sizeof(image),
          "the pixels");
    static const uint8_t kRed[4] = {255, 0, 0, 255};
    static const uint8_t kBlue[4] = {0, 0, 153, 255};
    bool right = true;
    for (int y = 0; y < 8 && s_runs; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            const uint8_t* pixel = &image[(y * 8 + x) * 4];
            right = right &&
                    (x < 4 ? IsNear(pixel, kRed) : (y < 4 ? IsNear(pixel, kBlue) : IsBlack(pixel)));
        }
    }
    CHECK(right, "red left, blended blue upper right, black lower right");
}

// A compute pass that leaves its GPU compute pass for an upload between
// two dispatches adding the root block's 5, and must set its pipeline,
// tables and root block again after; its timestamps, when the device
// has them, span both.
static void CheckComputeSplit(Scene* scene, bool timestamps)
{
    mrhiDevice* device = scene->device;
    mrhiQuerySetId times = {0};
    if (timestamps)
    {
        times = MakeQuerySet(device, mrhi_queryTimestamp, 2);
    }
    BeginScene(scene);
    mrhiBufferDef def = mrhiDefaultBufferDef();
    def.size = 256;
    mrhiResourceId e = {0};
    mrhiResourceId r = {0};
    CHECK(mrhiDeclareBuffer(device, &def, &e) == mrhi_success &&
              mrhiDeclareBuffer(device, &def, &r) == mrhi_success,
          "declared buffers");
    mrhiAccess fill = Whole(scene->d, mrhi_accessCopyDestination);
    mrhiPassId filled = CopyPass(device, &fill, 1);
    mrhiAccess uses[4] = {
        Whole(scene->u, mrhi_accessUniform), Whole(scene->d, mrhi_accessStorageReadWrite),
        Whole(scene->w, mrhi_accessSampled), Whole(e, mrhi_accessCopyDestination)};
    mrhiPassDef passDef = mrhiDefaultPassDef();
    passDef.accesses = uses;
    passDef.accessCount = 4;
    if (timestamps)
    {
        passDef.timestampQuerySet = times;
        passDef.timestampBegin = 0;
        passDef.timestampEnd = 1;
    }
    LABEL(passDef, "split");
    mrhiPassId split = {0};
    CHECK(mrhiAddPass(device, &passDef, &split) == mrhi_success, "a compute pass");
    mrhiAccess resolves = Whole(r, mrhi_accessQueryResolve);
    mrhiPassId resolve = CopyPass(device, &resolves, 1);
    mrhiAccess reads[3] = {Whole(scene->d, mrhi_accessCopySource), Whole(e, mrhi_accessCopySource),
                           Whole(r, mrhi_accessCopySource)};
    mrhiPassId read = CopyPass(device, reads, 3);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    const uint32_t values[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint32_t marks[4] = {9, 8, 7, 6};
    const float five[4] = {5.0f, 0.0f, 0.0f, 0.0f};
    CHECK(mrhiBeginPass(device, filled) == mrhi_success &&
              mrhiWriteBuffer(device, filled, scene->d, 0, values, sizeof(values)) ==
                  mrhi_success &&
              mrhiEndPass(device, filled) == mrhi_success &&
              mrhiBeginPass(device, split) == mrhi_success &&
              mrhiSetComputePipeline(device, split, scene->adding) == mrhi_success &&
              mrhiSetRootBlock(device, split, 0, five, sizeof(five)) == mrhi_success,
          "an adding pipeline set");
    BindScene(scene, split);
    CHECK(mrhiDispatch(device, split, 1, 1, 1) == mrhi_success &&
              mrhiWriteBuffer(device, split, e, 0, marks, sizeof(marks)) == mrhi_success &&
              mrhiDispatch(device, split, 1, 1, 1) == mrhi_success &&
              mrhiEndPass(device, split) == mrhi_success,
          "dispatched around an upload");
    mrhiRequestId sums = {0};
    mrhiRequestId marked = {0};
    mrhiRequestId stamps = {0};
    CHECK(mrhiBeginPass(device, resolve) == mrhi_success &&
              (!timestamps ||
               mrhiResolveQueries(device, resolve, times, 0, 2, r, 0) == mrhi_success) &&
              mrhiEndPass(device, resolve) == mrhi_success &&
              mrhiBeginPass(device, read) == mrhi_success &&
              mrhiReadBuffer(device, read, scene->d, 0, sizeof(values), &sums) == mrhi_success &&
              mrhiReadBuffer(device, read, e, 0, sizeof(marks), &marked) == mrhi_success &&
              mrhiReadBuffer(device, read, r, 0, 16, &stamps) == mrhi_success &&
              mrhiEndPass(device, read) == mrhi_success,
          "read");
    Finish(device, 3);
    const uint32_t expected[8] = {11, 12, 13, 14, 15, 16, 17, 18};
    CHECK(Taken(device, sums, (const uint8_t*)expected, sizeof(expected)),
          "both dispatches added the root block");
    CHECK(Taken(device, marked, (const uint8_t*)marks, sizeof(marks)), "the upload between");
    uint64_t ticks[2] = {0};
    size_t size = 0;
    CHECK(mrhiTakeReadback(device, stamps, ticks, sizeof(ticks), &size) == mrhi_success &&
              (!s_runs || !timestamps || (ticks[0] != 0 && ticks[1] >= ticks[0])),
          "the pass's timestamps");
    CHECK(!timestamps || mrhiDestroyQuerySet(device, times) == mrhi_success, "destroyed");
}

static void CheckDrawing(mrhiDevice* device, bool timestamps)
{
    Scene scene = {.device = device};
    MakeScene(&scene);
    CheckDrawFrame(&scene);
    CheckCulling(&scene);
    CheckQueries(&scene, timestamps);
    CheckRenderState(&scene);
    CheckComputeSplit(&scene, timestamps);
}

// A device made with the cache the last one exported takes it.
static void CheckCacheImport(mrhiInstance* instance, mrhiAdapterId adapter)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    def.pipelineCache = s_cache;
    def.pipelineCacheBytes = s_cacheBytes;
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success, "a device");
    mrhiInstanceNotification record;
    CHECK(NextInstance(instance, &record) == mrhi_success, "ready");
    CHECK(mrhiGetPipelineCacheOutcome(device) == mrhi_success, "its own cache taken");
    mrhiDestroyDevice(device);
}

// Opens a device on an adapter with the features asked for, which it
// answers ready at the next poll with them granted.
static void CheckDevice(mrhiInstance* instance, mrhiAdapterId adapter, const mrhiFeatures* asked)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    def.features = *asked;
    LABEL(def, "conformance");
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success, "a device");
    mrhiInstanceNotification record;
    CHECK(NextInstance(instance, &record) == mrhi_success &&
              record.kind == mrhi_instanceDeviceReady && record.outcome == mrhi_success,
          "ready at the next poll");
    CHECK(mrhiGetDeviceState(device) == mrhi_deviceReady, "ready");
    mrhiFeatures granted;
    CHECK(mrhiGetDeviceFeatures(device, &granted) == mrhi_success &&
              memcmp(&granted, asked, sizeof(granted)) == 0,
          "granted as asked");
    double period = 0.0;
    mrhiResult status = mrhiGetDeviceTimestampPeriod(device, &period);
    CHECK(asked->timestampQuery ? status == mrhi_success && period > 0.0
                                : status == mrhi_errorUnsupported,
          "a timestamp period with timestamps");
    CheckObjects(device, asked->timestampQuery);
    if (!s_noFrames)
    {
        CheckFrameMemory(device);
    }
    CheckPipelines(device);
    if (!s_noFrames)
    {
        CheckRoundTrip(device);
        CheckDrawing(device, asked->timestampQuery);
    }
    mrhiDestroyDevice(device);
}

// Checks every adapter an instance lists, and that a second search
// lists the same ones: the count found.
static size_t CheckDriver(mrhiInstance* instance, mrhiDriverKind driver)
{
    mrhiAdapterId ids[16];
    size_t count = Search(instance, ids, 16);
    s_runs = driver != mrhi_driverTest;
    s_noFrames = driver == mrhi_driverMetal;
    for (size_t i = 0; i < count; ++i)
    {
        mrhiAdapterInfo info;
        CHECK(mrhiGetAdapterInfo(instance, ids[i], &info) == mrhi_success, "info");
        CHECK(info.driver == driver, "the driver's adapter");
        CHECK(info.kind <= mrhi_adapterSoftware, "a kind");
        CHECK(info.nameLength > 0 && info.nameLength <= MRHI_ADAPTER_NAME_BYTES, "a name");
        CheckLimits(instance, ids[i]);
        CheckFormats(instance, ids[i]);
        mrhiFeatures none = {0};
        CheckDevice(instance, ids[i], &none);
        mrhiFeatures all;
        CHECK(mrhiGetAdapterFeatures(instance, ids[i], &all) == mrhi_success, "features");
        CheckDevice(instance, ids[i], &all);
        CheckCacheImport(instance, ids[i]);
        if (!s_noFrames)
        {
            CheckRetirement(instance, ids[i]);
        }
        CheckHeaps(instance, ids[i], driver != mrhi_driverTest);
    }
    mrhiAdapterId again[16];
    CHECK(Search(instance, again, 16) == count && memcmp(ids, again, count * sizeof(ids[0])) == 0,
          "the same adapters, in the same order");
    return count;
}

static void TestTestDriver(void)
{
    mrhiTestAdapter adapter = {
        .info = {.driver = mrhi_driverTest, .name = "conformance", .nameLength = 11},
        .limits = mrhiDefaultLimits(),
    };
    mrhiTestDriverDef test = {
        .chain = {.type = mrhi_structTestDriver},
        .adapters = &adapter,
        .adapterCount = 1,
    };
    mrhiInstance* instance = Create(&test.chain);
    if (instance == nullptr)
    {
        printf("skip: no test driver in this build\n");
        return;
    }
    CHECK(CheckDriver(instance, mrhi_driverTest) == 1, "the test adapter");
    mrhiDestroyInstance(instance);
}

[[maybe_unused]] static bool IsSet(const char* name)
{
    const char* value = getenv(name);
    return value != nullptr && value[0] != '\0';
}

static mrhiResult MakeSurface(mrhiInstance* instance, const mrhiChain* source,
                              mrhiSurfaceId* surfaceOut)
{
    mrhiSurfaceDef def = mrhiDefaultSurfaceDef();
    def.next = source;
    return mrhiCreateSurface(instance, &def, surfaceOut);
}

// A canvas is never a native surface, nor a window of another platform.
static void CheckForeignSources(mrhiInstance* instance)
{
    const mrhiSurfaceSourceCanvas canvas = {
        .chain = {.type = mrhi_structSurfaceSourceCanvas},
        .selector = "#c",
        .selectorLength = 2,
    };
    mrhiSurfaceId surface = {0};
#ifdef __EMSCRIPTEN__
    CHECK(MakeSurface(instance, &canvas.chain, &surface) == mrhi_errorUnsupported,
          "no canvas the selector names");
#else
    CHECK(MakeSurface(instance, &canvas.chain, &surface) == mrhi_errorUnsupported,
          "no canvas natively");
#endif
#ifndef _WIN32
    int window = 0;
    const mrhiSurfaceSourceWin32 win32 = {
        .chain = {.type = mrhi_structSurfaceSourceWin32},
        .hinstance = &window,
        .hwnd = &window,
    };
    CHECK(MakeSurface(instance, &win32.chain, &surface) == mrhi_errorUnsupported,
          "no Win32 window elsewhere");
#endif
}

#if defined(MRHI_TEST_XCB) || defined(__EMSCRIPTEN__)
// Whether caps meet the floors and offer 8-bit sRGB in Rec. 709.
static bool MeetsFloors(const mrhiSurfaceCaps* caps)
{
    bool srgb = false;
    for (uint32_t i = 0; i < caps->colorCount; ++i)
    {
        const mrhiSurfaceColor* color = &caps->colors[i];
        srgb =
            srgb ||
            ((color->format == mrhi_formatBgra8Unorm || color->format == mrhi_formatRgba8Unorm) &&
             color->primaries == mrhi_primariesBt709 && color->transfer == mrhi_transferSrgb &&
             color->range == mrhi_rangeStandard);
    }
    return caps->colorCount >= 1 && caps->colorCount <= MRHI_SURFACE_COLORS && srgb &&
           (caps->presentModes & mrhi_presentFifo) != 0 &&
           (caps->alphaModes & mrhi_alphaOpaque) != 0 &&
           (caps->usages & mrhi_textureRenderTarget) != 0;
}

// The first 8-bit sRGB color in Rec. 709 caps report, and its sRGB
// twin.
static mrhiSurfaceColor Srgb8(const mrhiSurfaceCaps* caps, mrhiFormat* twinOut)
{
    mrhiSurfaceColor found = {0};
    for (uint32_t i = caps->colorCount; i > 0; --i)
    {
        const mrhiSurfaceColor* color = &caps->colors[i - 1];
        bool srgb8 =
            (color->format == mrhi_formatBgra8Unorm || color->format == mrhi_formatRgba8Unorm) &&
            color->primaries == mrhi_primariesBt709 && color->transfer == mrhi_transferSrgb &&
            color->range == mrhi_rangeStandard;
        found = srgb8 ? *color : found;
    }
    *twinOut = found.format == mrhi_formatBgra8Unorm ? mrhi_formatBgra8UnormSrgb
                                                     : mrhi_formatRgba8UnormSrgb;
    return found;
}

static mrhiResult Configure(mrhiDevice* device, mrhiSurfaceId surface, const mrhiSurfaceCaps* caps,
                            uint32_t width, uint32_t height)
{
    mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
    config.surface = surface;
    config.color = Srgb8(caps, &config.viewFormats[0]);
    config.usage = mrhi_textureRenderTarget | (caps->usages & mrhi_textureCopySource);
    config.width = width;
    config.height = height;
    return mrhiConfigureSurface(device, &config);
}

// Whether an acquire gave an image.
static bool IsAcquired(mrhiResult result)
{
    return result == mrhi_success || result == mrhi_suboptimal;
}

// Records a frame, opened already, that clears the surface's image to
// red and reads its first pixel back when asked: the acquire's outcome.
// A frame without an image is dropped.
static mrhiResult RecordRed(mrhiDevice* device, mrhiSurfaceId surface, bool read,
                            mrhiRequestId* requestOut)
{
    mrhiResourceId image = {0};
    mrhiResult acquired = mrhiAcquireSurfaceImage(device, surface, &image);
    if (!IsAcquired(acquired))
    {
        CHECK(mrhiDropFrame(device) == mrhi_success, "dropped");
        return acquired;
    }
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){
        .resource = image,
        .load = mrhi_loadClear,
        .store = mrhi_storeKeep,
        .clear = {1.0f, 0.0f, 0.0f, 1.0f},
    };
    def.colorTargetCount = 1;
    mrhiPassId clear = {0};
    CHECK(mrhiAddPass(device, &def, &clear) == mrhi_success, "a clearing pass");
    mrhiAccess reads = Whole(image, mrhi_accessCopySource);
    mrhiPassId copy = read ? CopyPass(device, &reads, 1) : (mrhiPassId){0};
    CHECK(mrhiCompileFrame(device) == mrhi_success &&
              mrhiBeginPass(device, clear) == mrhi_success &&
              mrhiEndPass(device, clear) == mrhi_success,
          "cleared");
    if (read)
    {
        const mrhiTextureCopy source = {.resource = image};
        const mrhiExtent3d one = {1, 1, 1};
        CHECK(mrhiBeginPass(device, copy) == mrhi_success &&
                  mrhiReadTexture(device, copy, &source, &one, requestOut) == mrhi_success &&
                  mrhiEndPass(device, copy) == mrhi_success,
              "read");
    }
    return acquired;
}

// A frame that clears the surface's image to red, reads its first pixel
// back where the image allows, and presents it: the acquire's outcome.
static mrhiResult PresentRed(mrhiDevice* device, mrhiSurfaceId surface, bool read,
                             uint8_t pixelOut[4])
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success, "a frame");
    mrhiRequestId request = {0};
    mrhiResult acquired = RecordRed(device, surface, read, &request);
    if (!IsAcquired(acquired))
    {
        return acquired;
    }
    Finish(device, read ? 1 : 0);
    size_t size = 0;
    CHECK(!read ||
              (mrhiTakeReadback(device, request, pixelOut, 4, &size) == mrhi_success && size == 4),
          "a pixel");
    return acquired;
}

// Frames presented back to back, waiting only when the device's frames
// in flight are all running, so that acquire semaphores come round
// again while frames still run.
static void CheckFramesInFlight(mrhiDevice* device, mrhiSurfaceId surface)
{
    mrhiRequestId tokens[16];
    uint32_t submitted = 0;
    uint32_t waited = 0;
    for (int i = 0; i < 12; ++i)
    {
        mrhiFrameDef frame = mrhiDefaultFrameDef();
        mrhiResult begun = mrhiBeginFrame(device, &frame);
        while (begun == mrhi_errorCapacity && waited < submitted)
        {
            CHECK(WaitFor(device, tokens[waited++ % 16]) == mrhi_success, "a frame finished");
            begun = mrhiBeginFrame(device, &frame);
        }
        mrhiRequestId request = {0};
        CHECK(begun == mrhi_success && IsAcquired(RecordRed(device, surface, false, &request)) &&
                  mrhiSubmitFrame(device, &tokens[submitted++ % 16]) == mrhi_success,
              "a frame in flight");
    }
    while (waited < submitted)
    {
        CHECK(WaitFor(device, tokens[waited++ % 16]) == mrhi_success, "a frame finished");
    }
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(device, &record) == mrhi_success)
    {
        CHECK(record.outcome == mrhi_success, "a frame done");
    }
}

// A frame that acquires an image and gives it back, and one that
// presents an image no pass wrote.
static void CheckUnwrittenImages(mrhiDevice* device, mrhiSurfaceId surface)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId image = {0};
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
              IsAcquired(mrhiAcquireSurfaceImage(device, surface, &image)) &&
              mrhiDropFrame(device) == mrhi_success,
          "an image given back");
    uint8_t pixel[4];
    CHECK(IsAcquired(PresentRed(device, surface, false, pixel)), "the image taken again");
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
              IsAcquired(mrhiAcquireSurfaceImage(device, surface, &image)) &&
              mrhiCompileFrame(device) == mrhi_success,
          "an image no pass writes");
    Finish(device, 0);
}

// Presents to the surface from a device on an adapter that can: frames
// cleared and read back, more than the images and their semaphores; an
// image given back and taken again; one presented unwritten; a
// reconfiguration; and, where the window fixes its images' size, a size
// it does not take, which leaves the surface unconfigured until it is
// configured again, or else a size of the program's own.
static void CheckPresenting(mrhiInstance* instance, mrhiAdapterId adapter, mrhiSurfaceId surface,
                            const mrhiSurfaceCaps* caps, bool fixedSize)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success &&
              NextInstance(instance, &record) == mrhi_success,
          "a device");
    CHECK(Configure(device, surface, caps, 64, 48) == mrhi_success, "configured");
    bool read = (caps->usages & mrhi_textureCopySource) != 0;
    mrhiFormat twin = mrhi_formatNone;
    bool bgra = Srgb8(caps, &twin).format == mrhi_formatBgra8Unorm;
    const uint8_t red[4] = {bgra ? 0 : 255, 0, bgra ? 255 : 0, 255};
    for (int i = 0; i < 8; ++i)
    {
        uint8_t pixel[4] = {0};
        CHECK(IsAcquired(PresentRed(device, surface, read, pixel)), "presented");
        CHECK(!read || memcmp(pixel, red, 4) == 0, "red, in the format's channel order");
    }
    CheckFramesInFlight(device, surface);
    CheckUnwrittenImages(device, surface);
    uint8_t pixel[4];
    CHECK(Configure(device, surface, caps, 64, 48) == mrhi_success &&
              IsAcquired(PresentRed(device, surface, false, pixel)),
          "reconfigured");
    if (fixedSize)
    {
        CHECK(Configure(device, surface, caps, 32, 32) == mrhi_errorOutOfDate,
              "a size the window does not take");
        CHECK(PresentRed(device, surface, false, pixel) == mrhi_errorState, "unconfigured");
    }
    else
    {
        CHECK(Configure(device, surface, caps, 32, 32) == mrhi_success &&
                  IsAcquired(PresentRed(device, surface, false, pixel)),
              "a size of the program's own");
    }
    CHECK(Configure(device, surface, caps, 64, 48) == mrhi_success &&
              IsAcquired(PresentRed(device, surface, false, pixel)),
          "configured again");
    CHECK(mrhiUnconfigureSurface(device, surface) == mrhi_success, "unconfigured");
    mrhiDestroyDevice(device);
}

#endif

#ifdef MRHI_TEST_XCB
// A window of the X server the environment names, where there is one.
static void CheckXcbSurface(mrhiInstance* instance, const mrhiAdapterId* ids, size_t count)
{
    xcb_connection_t* connection = xcb_connect(nullptr, nullptr);
    if (xcb_connection_has_error(connection) != 0)
    {
        xcb_disconnect(connection);
        CHECK(!IsSet("MAUL_RHI_REQUIRE_SURFACE"), "an X server where required");
        printf("skip: no X server for surfaces\n");
        return;
    }
    xcb_screen_t* screen = xcb_setup_roots_iterator(xcb_get_setup(connection)).data;
    xcb_window_t window = xcb_generate_id(connection);
    xcb_create_window(connection, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, 64, 48, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, 0, nullptr);
    xcb_map_window(connection, window);
    xcb_flush(connection);
    const mrhiSurfaceSourceXcb source = {
        .chain = {.type = mrhi_structSurfaceSourceXcb},
        .connection = connection,
        .window = window,
    };
    mrhiSurfaceId surface = {0};
    CHECK(MakeSurface(instance, &source.chain, &surface) == mrhi_success, "an XCB surface");
    size_t presenting = 0;
    for (size_t i = 0; i < count; ++i)
    {
        mrhiSurfaceCaps caps;
        CHECK(mrhiGetSurfaceCaps(instance, surface, ids[i], &caps) == mrhi_success, "caps");
        CHECK(!caps.presentable || MeetsFloors(&caps), "the floors where it presents");
        if (caps.presentable)
        {
            CheckPresenting(instance, ids[i], surface, &caps, true);
        }
        presenting += caps.presentable ? 1 : 0;
    }
    CHECK(presenting > 0 || !IsSet("MAUL_RHI_REQUIRE_SURFACE"), "an adapter presents there");
    CHECK(mrhiDestroySurface(instance, surface) == mrhi_success, "the surface destroyed");
    xcb_destroy_window(connection, window);
    xcb_disconnect(connection);
}
#endif

#ifdef __EMSCRIPTEN__
// clang-format off
// Resizes the runner's canvas as a page would.
EM_JS(void, ResizeCanvas, (int width), {
    document.querySelector('#mrhi-canvas').width = width;
});
// clang-format on

// The web runner's canvas, which the adapter presents to; a drawing
// buffer the page resizes leaves the canvas out of date until it is
// configured again.
static void CheckCanvasSurface(mrhiInstance* instance, const mrhiAdapterId* ids, size_t count)
{
    const mrhiSurfaceSourceCanvas source = {
        .chain = {.type = mrhi_structSurfaceSourceCanvas},
        .selector = "#mrhi-canvas",
        .selectorLength = 12,
    };
    mrhiSurfaceId surface = {0};
    CHECK(MakeSurface(instance, &source.chain, &surface) == mrhi_success, "a canvas surface");
    for (size_t i = 0; i < count; ++i)
    {
        mrhiSurfaceCaps caps;
        CHECK(mrhiGetSurfaceCaps(instance, surface, ids[i], &caps) == mrhi_success &&
                  caps.presentable && MeetsFloors(&caps),
              "presentable, with the floors");
        CheckPresenting(instance, ids[i], surface, &caps, false);
        mrhiDeviceDef def = mrhiDefaultDeviceDef();
        def.adapter = ids[i];
        mrhiDevice* device = nullptr;
        mrhiRequestId request;
        mrhiInstanceNotification record;
        CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success &&
                  NextInstance(instance, &record) == mrhi_success,
              "a device");
        uint8_t pixel[4];
        CHECK(Configure(device, surface, &caps, 64, 48) == mrhi_success &&
                  IsAcquired(PresentRed(device, surface, false, pixel)),
              "configured");
        ResizeCanvas(20);
        CHECK(PresentRed(device, surface, false, pixel) == mrhi_errorOutOfDate,
              "out of date once the page resizes it");
        CHECK(Configure(device, surface, &caps, 20, 48) == mrhi_success &&
                  IsAcquired(PresentRed(device, surface, false, pixel)),
              "configured at the new size");
        mrhiDestroyDevice(device);
    }
    CHECK(mrhiDestroySurface(instance, surface) == mrhi_success, "the surface destroyed");
}
#endif

// Surfaces on the native driver's adapters.
static void CheckSurfaces(mrhiInstance* instance)
{
    CheckForeignSources(instance);
    mrhiAdapterId ids[16];
    size_t count = Search(instance, ids, 16);
#if defined(MRHI_TEST_XCB)
    CheckXcbSurface(instance, ids, count);
#elif defined(__EMSCRIPTEN__)
    CheckCanvasSurface(instance, ids, count);
#else
    (void)count;
    CHECK(!IsSet("MAUL_RHI_REQUIRE_SURFACE"), "a window system where required");
#endif
}

static void TestNativeDriver(void)
{
    mrhiInstance* instance = Create(nullptr);
    CHECK(instance != nullptr, "an instance");
    if (instance == nullptr)
    {
        return;
    }
    // The build's native driver: WebGPU on the web, Metal where the build
    // chose it, Vulkan elsewhere.
#ifdef __EMSCRIPTEN__
    size_t count = CheckDriver(instance, mrhi_driverWebGpu);
    const char* required = getenv("MAUL_RHI_REQUIRE_WEBGPU");
#elif defined(MAUL_RHI_METAL_DRIVER)
    size_t count = CheckDriver(instance, mrhi_driverMetal);
    const char* required = getenv("MAUL_RHI_REQUIRE_METAL");
#else
    size_t count = CheckDriver(instance, mrhi_driverVulkan);
    const char* required = getenv("MAUL_RHI_REQUIRE_VULKAN");
#endif
    if (count == 0)
    {
        CHECK(required == nullptr || required[0] == '\0', "a native adapter where required");
        printf("skip: no native adapter on this host\n");
    }
    else
    {
        CheckSurfaces(instance);
    }
    mrhiDestroyInstance(instance);
}

int main(void)
{
    TestTestDriver();
    TestNativeDriver();
    return s_failures == 0 ? 0 : 1;
}
