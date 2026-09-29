// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The conformance suite (mrhi-0003): the same checks, through the public API
// only, on every driver: the test driver, and each adapter of the
// build's native driver. A host without native adapters skips them,
// unless MAUL_RHI_REQUIRE_VULKAN is set and not empty.

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "test_harness.h"

#include "maul-rhi/capabilities.h"
#include "maul-rhi/device.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/shader.h"
#include "maul-rhi/test.h"
#include "shaders/conformance_container.h"

#include <stdlib.h>
#include <string.h>

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
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
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
    mrhiViewId view = {0};
    CHECK(mrhiCreateView(device, &def, &view) == mrhi_success, "a view");
    return view;
}

// Objects of every kind: many buffers over more than one block, one
// larger than half a block, textures and views of each shape, a
// comparing anisotropic sampler; some destroyed, the rest left for the
// device's end.
static void CheckObjects(mrhiDevice* device)
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

// A pipeline cache a device exported, for the next device to import.
static uint8_t s_cache[1u << 20];
static size_t s_cacheBytes;

// Takes the device's pipeline answers: ready ones, and stale ones for
// pipelines destroyed before their answer.
static void AwaitPipelines(mrhiDevice* device, uint32_t ready, uint32_t stale)
{
    mrhiDeviceNotification record;
    uint32_t counts[2] = {0};
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
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_success, "ready");
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
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success, "a device");
    mrhiInstanceNotification record;
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
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
    CheckObjects(device);
    CheckFrameMemory(device);
    CheckPipelines(device);
    mrhiDestroyDevice(device);
}

// Checks every adapter an instance lists, and that a second search
// lists the same ones: the count found.
static size_t CheckDriver(mrhiInstance* instance, mrhiDriverKind driver)
{
    mrhiAdapterId ids[16];
    size_t count = Search(instance, ids, 16);
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

static void TestNativeDriver(void)
{
    mrhiInstance* instance = Create(nullptr);
    CHECK(instance != nullptr, "an instance");
    if (instance == nullptr)
    {
        return;
    }
    size_t count = CheckDriver(instance, mrhi_driverVulkan);
    if (count == 0)
    {
        const char* required = getenv("MAUL_RHI_REQUIRE_VULKAN");
        CHECK(required == nullptr || required[0] == '\0', "a Vulkan adapter where required");
        printf("skip: no native adapter on this host\n");
    }
    mrhiDestroyInstance(instance);
}

int main(void)
{
    TestTestDriver();
    TestNativeDriver();
    return s_failures == 0 ? 0 : 1;
}
