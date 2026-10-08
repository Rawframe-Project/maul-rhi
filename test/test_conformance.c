// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The conformance suite (mrhi-0003): the same checks, through the public API
// only, on every driver: the test driver, and each adapter of the
// build's native driver. A host without native adapters skips them,
// unless MAUL_RHI_REQUIRE_VULKAN (or _WEBGPU, _METAL or _D3D12, for the
// build's driver) is set and not empty. The checks are named cases in
// the requirements' ten categories (mrhi-0024): --list prints them, the
// mustpass list of the SPI version, and --case runs one; every run
// ends with each case's outcome.

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
#include "shaders/multiview_container.h"

#include <stdlib.h>
#include <string.h>

// On the web, built with Emscripten or with a plain WebAssembly
// toolchain (mrhi-0016), the suite runs in a page with a canvas.
#ifdef __EMSCRIPTEN__
#include <emscripten/em_js.h>
#include <emscripten/emscripten.h>
#define MRHI_TEST_WEB
// Waits a millisecond while the page runs its event loop (JSPI).
static void Sleep(void)
{
    emscripten_sleep(1);
}
// clang-format off
// Resizes the runner's canvas as a page would, or as a worker resizes
// the OffscreenCanvas it was given (mrhi-0026).
EM_JS(void, ResizeCanvas, (int width), {
    const canvases = Module.mrhiCanvases || {};
    (canvases['#mrhi-canvas'] || document.querySelector('#mrhi-canvas')).width = width;
});
// clang-format on
#elif defined(__wasi__)
#define MRHI_TEST_WEB
// Without Emscripten the runner's page gives both (web_runner.cjs), the
// sleep through JSPI's WebAssembly.Suspending.
__attribute__((import_module("env"), import_name("mrhiTestSleep"))) void mrhiTestSleep(void);
__attribute__((import_module("env"), import_name("mrhiTestResizeCanvas"))) void
mrhiTestResizeCanvas(int width);
static void Sleep(void)
{
    mrhiTestSleep();
}
static void ResizeCanvas(int width)
{
    mrhiTestResizeCanvas(width);
}
#endif

#ifdef MRHI_TEST_XCB
#include <threads.h>
#include <xcb/xcb.h>
#endif
#ifdef MRHI_TEST_ANDROID
#include "android_activity.h"
#endif
#ifdef MAUL_RHI_METAL_DRIVER
#include "metal_layer.h"
#endif
#ifdef MAUL_RHI_D3D12_DRIVER
#include "d3d12_debug.h"
#endif
// Threads for threads.recording: Windows's or POSIX's; the web builds
// have none.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif !defined(MRHI_TEST_WEB)
#include <pthread.h>
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
#ifdef MRHI_TEST_WEB
    for (int slept = 0; status == mrhi_empty && slept < 10000; ++slept)
    {
        Sleep();
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
#ifdef MRHI_TEST_WEB
    for (int slept = 0; status == mrhi_empty && slept < 10000; ++slept)
    {
        Sleep();
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
#ifdef MRHI_TEST_WEB
    for (int slept = 0; status == mrhi_timeout && slept < 10000; ++slept)
    {
        Sleep();
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
static bool IsNear(const uint8_t* pixel, const uint8_t* expected);

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

// A target with its twin among its view formats, only ever rendered to:
// made on every API, and not transient, since a pass may render it
// through the twin and WebGPU refuses view formats on a transient
// attachment (mrhi-0008).
static void CheckTransientTwin(mrhiDevice* device)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success, "a frame");
    mrhiTextureDef texture = mrhiDefaultTextureDef();
    texture.format = mrhi_formatRgba8UnormSrgb;
    texture.viewFormats[0] = mrhi_formatRgba8Unorm;
    texture.width = 16;
    texture.height = 16;
    mrhiResourceId target = {0};
    CHECK(mrhiDeclareTexture(device, &texture, &target) == mrhi_success, "a target with a twin");
    mrhiPassDef pass = mrhiDefaultPassDef();
    pass.colorTargets[0] =
        (mrhiColorTarget){.resource = target, .load = mrhi_loadClear, .store = mrhi_storeKeep};
    pass.colorTargetCount = 1;
    pass.neverCull = true;
    mrhiPassId id = {0};
    CHECK(mrhiAddPass(device, &pass, &id) == mrhi_success, "a pass");
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    mrhiResourcePlan plan = {0};
    CHECK(mrhiGetResourcePlan(device, target, &plan) == mrhi_success && !plan.transient,
          "not transient");
    CHECK(mrhiBeginPass(device, id) == mrhi_success && mrhiEndPass(device, id) == mrhi_success,
          "recorded");
    mrhiRequestId token = {0};
    CHECK(mrhiSubmitFrame(device, &token) == mrhi_success && WaitFor(device, token) == mrhi_success,
          "finished");
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(device, &record) == mrhi_success)
    {
        CHECK(record.outcome == mrhi_success, "a success");
    }
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

// A named case: its runs, over adapters and devices, and the failures
// counted in them.
typedef struct Case
{
    const char* name;
    int runs;
    int failures;
} Case;

// The cases, by category, in the order of the requirements'
// conformance section.
static Case s_cases[] = {
    {"api.adapters", 0, 0},
    {"api.devices", 0, 0},
    {"api.objects", 0, 0},
    {"api.pipelines", 0, 0},
    {"api.cache_import", 0, 0},
    {"api.queries", 0, 0},
    {"api.statistics", 0, 0},
    {"capabilities.limits", 0, 0},
    {"capabilities.formats", 0, 0},
    {"capabilities.feature_formats", 0, 0},
    {"capabilities.multiview", 0, 0},
    {"capabilities.required", 0, 0},
    {"binding.draws", 0, 0},
    {"binding.culling", 0, 0},
    {"binding.render_state", 0, 0},
    {"binding.compute_split", 0, 0},
    {"binding.heaps", 0, 0},
    {"hazards.aliasing", 0, 0},
    {"hazards.transient_twin", 0, 0},
    {"hazards.frame_memory", 0, 0},
    {"transfers.round_trip", 0, 0},
    {"transfers.clear", 0, 0},
    {"swapchain.foreign_sources", 0, 0},
    {"swapchain.present", 0, 0},
    {"loss.simulated", 0, 0},
    {"threads.recording", 0, 0},
    {"limits.retirement", 0, 0},
    {"validation.clean", 0, 0},
};

// The one case --case runs, or NULL for all.
static const char* s_only;

// The case named, when it is to run: NULL for one --case leaves out. A
// name missing from the table fails.
static Case* BeginCase(const char* name)
{
    for (size_t i = 0; i < sizeof(s_cases) / sizeof(s_cases[0]); ++i)
    {
        if (strcmp(s_cases[i].name, name) == 0)
        {
            return s_only == nullptr || strcmp(s_only, name) == 0 ? &s_cases[i] : nullptr;
        }
    }
    CHECK(false, "a listed case");
    return nullptr;
}

// Runs a call as a case, counting its failures against it.
#define RUN(name, call)                                                                            \
    do                                                                                             \
    {                                                                                              \
        Case* run_ = BeginCase(name);                                                              \
        if (run_ != nullptr)                                                                       \
        {                                                                                          \
            int before_ = s_failures;                                                              \
            call;                                                                                  \
            run_->runs += 1;                                                                       \
            run_->failures += s_failures - before_;                                                \
        }                                                                                          \
    } while (0)

static bool Taken(mrhiDevice* device, mrhiRequestId request, const uint8_t* expected, size_t size)
{
    static uint8_t bytes[4096];
    size_t taken = 0;
    return mrhiTakeReadback(device, request, bytes, sizeof(bytes), &taken) == mrhi_success &&
           taken == size && (!s_runs || memcmp(bytes, expected, size) == 0);
}

// Bytes and texels uploaded to device objects, copied through a
// transient buffer, and from the texture into a layer of an array, and
// read back; then read again in a frame that
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
    textureDef.kind = mrhi_texture2dArray;
    textureDef.width = 4;
    textureDef.height = 4;
    textureDef.depthOrLayers = 3;
    mrhiTextureId array = {0};
    CHECK(mrhiCreateTexture(device, &textureDef, &array) == mrhi_success, "an array");
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success, "a frame");
    mrhiResourceId b = {0};
    mrhiResourceId t = {0};
    mrhiResourceId x = {0};
    mrhiResourceId y = {0};
    mrhiResourceId l = {0};
    mrhiBufferDef transient = mrhiDefaultBufferDef();
    transient.size = 256;
    CHECK(mrhiImportBuffer(device, buffer, &b) == mrhi_success &&
              mrhiImportTexture(device, texture, &t) == mrhi_success &&
              mrhiImportTexture(device, array, &l) == mrhi_success &&
              mrhiDeclareBuffer(device, &transient, &x) == mrhi_success &&
              mrhiDeclareBuffer(device, &transient, &y) == mrhi_success,
          "the resources");
    mrhiAccess writes[2] = {Whole(b, mrhi_accessCopyDestination),
                            Whole(t, mrhi_accessCopyDestination)};
    mrhiPassId upload = CopyPass(device, writes, 2);
    mrhiAccess moves[5] = {Whole(b, mrhi_accessCopySource), Whole(x, mrhi_accessCopyDestination),
                           Whole(y, mrhi_accessCopyDestination), Whole(t, mrhi_accessCopySource),
                           Whole(l, mrhi_accessCopyDestination)};
    mrhiPassId move = CopyPass(device, moves, 5);
    mrhiAccess reads[4] = {Whole(x, mrhi_accessCopySource), Whole(y, mrhi_accessCopySource),
                           Whole(t, mrhi_accessCopySource), Whole(l, mrhi_accessCopySource)};
    mrhiPassId read = CopyPass(device, reads, 4);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    const mrhiTextureCopy texels = {.resource = t};
    const mrhiTexelLayout layout = {.bytesPerRow = 64, .rowsPerImage = 16};
    const mrhiExtent3d extent = {16, 16, 1};
    mrhiRequestId fromX = {0};
    mrhiRequestId fromY = {0};
    mrhiRequestId fromT = {0};
    mrhiRequestId empty = {0};
    mrhiRequestId emptyTexels = {0};
    mrhiRequestId fromL = {0};
    const mrhiTextureCopy corner = {.resource = t, .x = 4, .y = 4};
    const mrhiTextureCopy layer = {.resource = l, .z = 2};
    const mrhiExtent3d square = {4, 4, 1};
    CHECK(mrhiBeginPass(device, upload) == mrhi_success &&
              mrhiWriteBuffer(device, upload, b, 0, pattern, sizeof(pattern)) == mrhi_success &&
              mrhiWriteTexture(device, upload, &texels, pattern, sizeof(pattern), &layout,
                               &extent) == mrhi_success &&
              // Empty writes copy nothing, as WebGPU allows.
              mrhiWriteBuffer(device, upload, b, 0, nullptr, 0) == mrhi_success &&
              mrhiWriteTexture(device, upload, &texels, nullptr, 0, &layout,
                               &(mrhiExtent3d){0, 1, 1}) == mrhi_success &&
              mrhiEndPass(device, upload) == mrhi_success,
          "uploaded");
    CHECK(mrhiBeginPass(device, move) == mrhi_success &&
              mrhiCopyBuffer(device, move, b, 256, x, 0, 256) == mrhi_success &&
              mrhiCopyBuffer(device, move, b, 768, y, 0, 256) == mrhi_success &&
              // Zero bytes copy nothing, as WebGPU allows.
              mrhiCopyBuffer(device, move, b, 0, y, 0, 0) == mrhi_success &&
              mrhiCopyTexture(device, move, &corner, &layer, &square) == mrhi_success &&
              mrhiEndPass(device, move) == mrhi_success,
          "copied");
    CHECK(mrhiBeginPass(device, read) == mrhi_success &&
              mrhiReadBuffer(device, read, x, 0, 256, &fromX) == mrhi_success &&
              mrhiReadBuffer(device, read, y, 0, 256, &fromY) == mrhi_success &&
              mrhiReadTexture(device, read, &texels, &extent, &fromT) == mrhi_success &&
              // Empty reads are answered with no bytes.
              mrhiReadBuffer(device, read, x, 0, 0, &empty) == mrhi_success &&
              mrhiReadTexture(device, read, &texels, &(mrhiExtent3d){0, 1, 1}, &emptyTexels) ==
                  mrhi_success &&
              mrhiReadTexture(device, read, &layer, &square, &fromL) == mrhi_success &&
              mrhiEndPass(device, read) == mrhi_success,
          "read");
    Finish(device, 6);
    CHECK(Taken(device, empty, pattern, 0) && Taken(device, emptyTexels, pattern, 0),
          "no bytes from empty reads");
    CHECK(Taken(device, fromX, pattern + 256, 256), "the buffer's bytes, through a transient");
    CHECK(Taken(device, fromY, pattern + 768, 256), "a second transient apart from the first");
    CHECK(Taken(device, fromT, pattern, sizeof(pattern)), "the texture's texels");
    uint8_t square16[64];
    for (int row = 0; row < 4; ++row)
    {
        memcpy(square16 + 16 * row, pattern + 64 * (4 + row) + 16, 16);
    }
    CHECK(Taken(device, fromL, square16, sizeof(square16)),
          "a corner of the texture, copied into an array's third layer");
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success, "another frame");
    CHECK(mrhiImportBuffer(device, buffer, &b) == mrhi_success, "imported again");
    // Two ranges cleared first (mrhi-0022), one to the buffer's end.
    mrhiAccess cleared = Whole(b, mrhi_accessCopyDestination);
    mrhiPassId clear = CopyPass(device, &cleared, 1);
    mrhiAccess again = Whole(b, mrhi_accessCopySource);
    read = CopyPass(device, &again, 1);
    CHECK(mrhiCompileFrame(device) == mrhi_success &&
              mrhiBeginPass(device, clear) == mrhi_success &&
              mrhiClearBuffer(device, clear, b, 576, 128) == mrhi_success &&
              mrhiClearBuffer(device, clear, b, 1008, MRHI_WHOLE_SIZE) == mrhi_success &&
              mrhiClearBuffer(device, clear, b, 1024, MRHI_WHOLE_SIZE) == mrhi_success &&
              mrhiEndPass(device, clear) == mrhi_success,
          "cleared");
    CHECK(mrhiBeginPass(device, read) == mrhi_success &&
              mrhiReadBuffer(device, read, b, 512, 512, &fromX) == mrhi_success &&
              mrhiEndPass(device, read) == mrhi_success,
          "read again");
    CHECK(mrhiDestroyBuffer(device, buffer) == mrhi_success, "destroyed while recorded");
    Finish(device, 1);
    uint8_t expected[512];
    memcpy(expected, pattern + 512, sizeof(expected));
    memset(expected + 64, 0, 128);
    memset(expected + 496, 0, 16);
    CHECK(Taken(device, fromX, expected, sizeof(expected)),
          "kept across frames and until the end, the cleared ranges zero");
}

// A texture of a format for the feature format check.
static mrhiTextureId MakeFormatTexture(mrhiDevice* device, mrhiFormat format, uint32_t size,
                                       mrhiTextureUsage usage)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = format;
    def.width = size;
    def.height = size;
    def.usage = usage | mrhi_textureCopySource;
    LABEL(def, "feature format");
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(device, &def, &texture) == mrhi_success, "a texture of a feature");
    return texture;
}

// What formats' features grant: BC1 blocks uploaded and read back
// unchanged, and an rg11b10ufloat target cleared to 1, 0.5 and 0.25,
// which pack exactly.
static void CheckFeatureFormats(mrhiDevice* device, const mrhiFeatures* granted)
{
    if (!granted->textureCompressionBc && !granted->rg11b10Renderable)
    {
        return;
    }
    uint8_t blocks[32];
    for (size_t i = 0; i < sizeof(blocks); ++i)
    {
        blocks[i] = (uint8_t)(i * 13 + 5);
    }
    mrhiTextureId compressed = {0};
    mrhiTextureId packed = {0};
    if (granted->textureCompressionBc)
    {
        compressed = MakeFormatTexture(device, mrhi_formatBc1RgbaUnorm, 8,
                                       mrhi_textureSampled | mrhi_textureCopyDestination);
    }
    if (granted->rg11b10Renderable)
    {
        packed = MakeFormatTexture(device, mrhi_formatRg11b10Ufloat, 4, mrhi_textureRenderTarget);
    }
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success, "a feature format frame");
    mrhiResourceId c = {0};
    mrhiResourceId p = {0};
    mrhiPassId upload = {0};
    mrhiPassId clear = {0};
    mrhiAccess reads[2];
    uint32_t readCount = 0;
    if (granted->textureCompressionBc)
    {
        CHECK(mrhiImportTexture(device, compressed, &c) == mrhi_success, "the BC texture");
        mrhiAccess write = Whole(c, mrhi_accessCopyDestination);
        upload = CopyPass(device, &write, 1);
        reads[readCount++] = Whole(c, mrhi_accessCopySource);
    }
    if (granted->rg11b10Renderable)
    {
        CHECK(mrhiImportTexture(device, packed, &p) == mrhi_success, "the rg11b10 target");
        mrhiPassDef def = mrhiDefaultPassDef();
        def.colorTargets[0] = (mrhiColorTarget){
            .resource = p,
            .load = mrhi_loadClear,
            .store = mrhi_storeKeep,
            .clear = {1.0f, 0.5f, 0.25f, 1.0f},
        };
        def.colorTargetCount = 1;
        CHECK(mrhiAddPass(device, &def, &clear) == mrhi_success, "a pass clearing rg11b10");
        reads[readCount++] = Whole(p, mrhi_accessCopySource);
    }
    mrhiPassId read = CopyPass(device, reads, readCount);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "the feature formats compiled");
    const mrhiTextureCopy compressedCopy = {.resource = c};
    const mrhiTextureCopy packedCopy = {.resource = p};
    const mrhiTexelLayout layout = {.bytesPerRow = 16, .rowsPerImage = 8};
    const mrhiExtent3d compressedExtent = {8, 8, 1};
    const mrhiExtent3d packedExtent = {4, 4, 1};
    if (granted->textureCompressionBc)
    {
        CHECK(mrhiBeginPass(device, upload) == mrhi_success &&
                  mrhiWriteTexture(device, upload, &compressedCopy, blocks, sizeof(blocks), &layout,
                                   &compressedExtent) == mrhi_success &&
                  mrhiEndPass(device, upload) == mrhi_success,
              "BC blocks uploaded");
    }
    if (granted->rg11b10Renderable)
    {
        CHECK(mrhiBeginPass(device, clear) == mrhi_success &&
                  mrhiEndPass(device, clear) == mrhi_success,
              "rg11b10 cleared");
    }
    mrhiRequestId fromC = {0};
    mrhiRequestId fromP = {0};
    CHECK(mrhiBeginPass(device, read) == mrhi_success &&
              (!granted->textureCompressionBc ||
               mrhiReadTexture(device, read, &compressedCopy, &compressedExtent, &fromC) ==
                   mrhi_success) &&
              (!granted->rg11b10Renderable ||
               mrhiReadTexture(device, read, &packedCopy, &packedExtent, &fromP) == mrhi_success) &&
              mrhiEndPass(device, read) == mrhi_success,
          "the feature formats read");
    Finish(device, readCount);
    if (granted->textureCompressionBc)
    {
        CHECK(Taken(device, fromC, blocks, sizeof(blocks)), "BC blocks, unchanged");
        CHECK(mrhiDestroyTexture(device, compressed) == mrhi_success, "the BC texture destroyed");
    }
    if (granted->rg11b10Renderable)
    {
        // 1.0 and 0.5 as 6e5 floats, 0.25 as a 5e5 one.
        const uint32_t texel = 0x3C0u | 0x380u << 11 | 0x1A0u << 22;
        uint8_t texels[64];
        for (size_t i = 0; i < sizeof(texels); i += 4)
        {
            memcpy(texels + i, &texel, 4);
        }
        CHECK(Taken(device, fromP, texels, sizeof(texels)), "rg11b10 packed exactly");
        CHECK(mrhiDestroyTexture(device, packed) == mrhi_success, "the rg11b10 target destroyed");
    }
}

// Declared resources whose passes never meet share memory: two buffers,
// each uploaded to and copied out before the next takes the same bytes,
// then two targets, each cleared and copied out, over the same bytes
// again. Each copy holds only its own resource's contents.
static void CheckAliasing(mrhiDevice* device)
{
    uint8_t first[256];
    uint8_t second[256];
    for (size_t i = 0; i < sizeof(first); ++i)
    {
        first[i] = (uint8_t)(i * 5 + 1);
        second[i] = (uint8_t)(i * 11 + 7);
    }
    mrhiBufferId buffer = MakeBuffer(device, 1024);
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success, "a frame");
    mrhiResourceId out = {0};
    mrhiResourceId buffers[2] = {{0}};
    mrhiResourceId targets[2] = {{0}};
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = sizeof(first);
    mrhiTextureDef targetDef = mrhiDefaultTextureDef();
    targetDef.format = mrhi_formatRgba8Unorm;
    targetDef.width = 4;
    targetDef.height = 4;
    CHECK(mrhiImportBuffer(device, buffer, &out) == mrhi_success, "the output");
    const uint8_t* uploads[2] = {first, second};
    mrhiPassId passes[8] = {{0}};
    for (int i = 0; i < 2; ++i)
    {
        CHECK(mrhiDeclareBuffer(device, &bufferDef, &buffers[i]) == mrhi_success, "a buffer");
        mrhiAccess write = Whole(buffers[i], mrhi_accessCopyDestination);
        passes[2 * i] = CopyPass(device, &write, 1);
        mrhiAccess copies[2] = {Whole(buffers[i], mrhi_accessCopySource),
                                Whole(out, mrhi_accessCopyDestination)};
        passes[2 * i + 1] = CopyPass(device, copies, 2);
    }
    for (int i = 0; i < 2; ++i)
    {
        CHECK(mrhiDeclareTexture(device, &targetDef, &targets[i]) == mrhi_success, "a target");
        mrhiPassDef def = mrhiDefaultPassDef();
        def.colorTargets[0] = (mrhiColorTarget){
            .resource = targets[i],
            .load = mrhi_loadClear,
            .store = mrhi_storeKeep,
            .clear = i == 0 ? (mrhiClearColor){1.0f, 0.0f, 0.0f, 1.0f}
                            : (mrhiClearColor){0.0f, 0.0f, 1.0f, 1.0f},
        };
        def.colorTargetCount = 1;
        def.neverCull = true;
        CHECK(mrhiAddPass(device, &def, &passes[4 + 2 * i]) == mrhi_success, "a clearing pass");
        mrhiAccess copies[2] = {Whole(targets[i], mrhi_accessCopySource),
                                Whole(out, mrhi_accessCopyDestination)};
        passes[5 + 2 * i] = CopyPass(device, copies, 2);
    }
    mrhiAccess read = Whole(out, mrhi_accessCopySource);
    mrhiPassId reading = CopyPass(device, &read, 1);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    mrhiResourcePlan plans[4];
    const mrhiResourceId planned[4] = {buffers[0], buffers[1], targets[0], targets[1]};
    for (int i = 0; i < 4; ++i)
    {
        CHECK(mrhiGetResourcePlan(device, planned[i], &plans[i]) == mrhi_success, "a plan");
    }
    CHECK(plans[0].memoryBytes > 0 && plans[1].memoryOffset == plans[0].memoryOffset &&
              plans[3].memoryBytes > 0 && plans[3].memoryOffset == plans[2].memoryOffset,
          "each second resource placed over the first");
    const mrhiExtent3d row = {4, 1, 1};
    for (int i = 0; i < 2; ++i)
    {
        CHECK(mrhiBeginPass(device, passes[2 * i]) == mrhi_success &&
                  mrhiWriteBuffer(device, passes[2 * i], buffers[i], 0, uploads[i],
                                  sizeof(first)) == mrhi_success &&
                  mrhiEndPass(device, passes[2 * i]) == mrhi_success &&
                  mrhiBeginPass(device, passes[2 * i + 1]) == mrhi_success &&
                  mrhiCopyBuffer(device, passes[2 * i + 1], buffers[i], 0, out,
                                 (uint64_t)i * sizeof(first), sizeof(first)) == mrhi_success &&
                  mrhiEndPass(device, passes[2 * i + 1]) == mrhi_success,
              "a buffer uploaded and copied out");
    }
    for (int i = 0; i < 2; ++i)
    {
        const mrhiTextureCopy texels = {.resource = targets[i]};
        const mrhiBufferCopy bytes = {.resource = out, .offset = 512 + (uint64_t)i * 16};
        CHECK(mrhiBeginPass(device, passes[4 + 2 * i]) == mrhi_success &&
                  mrhiEndPass(device, passes[4 + 2 * i]) == mrhi_success &&
                  mrhiBeginPass(device, passes[5 + 2 * i]) == mrhi_success &&
                  mrhiCopyTextureToBuffer(device, passes[5 + 2 * i], &texels, &bytes, &row) ==
                      mrhi_success &&
                  mrhiEndPass(device, passes[5 + 2 * i]) == mrhi_success,
              "a target cleared and copied out");
    }
    mrhiRequestId taken = {0};
    CHECK(mrhiBeginPass(device, reading) == mrhi_success &&
              mrhiReadBuffer(device, reading, out, 0, 544, &taken) == mrhi_success &&
              mrhiEndPass(device, reading) == mrhi_success,
          "read");
    Finish(device, 1);
    uint8_t bytes[544];
    size_t size = 0;
    CHECK(mrhiTakeReadback(device, taken, bytes, sizeof(bytes), &size) == mrhi_success &&
              size == sizeof(bytes),
          "the bytes");
    static const uint8_t kRed[4] = {255, 0, 0, 255};
    static const uint8_t kBlue[4] = {0, 0, 255, 255};
    bool red = true;
    bool blue = true;
    for (int i = 0; i < 4; ++i)
    {
        red = red && memcmp(bytes + 512 + 4 * i, kRed, 4) == 0;
        blue = blue && memcmp(bytes + 528 + 4 * i, kBlue, 4) == 0;
    }
    CHECK(!s_runs || memcmp(bytes, first, sizeof(first)) == 0, "the first buffer's own bytes");
    CHECK(!s_runs || memcmp(bytes + 256, second, sizeof(second)) == 0,
          "the second buffer's own bytes over the first's memory");
    CHECK(!s_runs || red, "the first target's own texels");
    CHECK(!s_runs || blue, "the second target's own texels over the same memory");
    CHECK(mrhiDestroyBuffer(device, buffer) == mrhi_success, "destroyed");
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

// Where the heap check's objects sit in its heaps: the sampler it reads,
// and another it must not. The texture sits where the second descriptor
// a frame binds lands if a driver's rings overlap its heaps. A storage
// texture sits beside it, unread, so that its view is checked as it is
// written.
enum
{
    HEAP_TEXTURE = 1,
    HEAP_STORAGE = 3,
    HEAP_SAMPLER = 2,
    HEAP_OTHER_SAMPLER = 0,
    HEAP_BUFFER = 7,
};

// Makes a heap of the heap check with its entries: the texture's view,
// the storage texture's, the buffer and two samplers, the one read
// clamping and filtering linearly on minification, or repeating and
// taking the nearest texel.
static mrhiHeapId MakeHeap(mrhiDevice* device, mrhiTextureId texture, mrhiTextureId image,
                           mrhiBufferId buffer, bool clamping)
{
    // The storage view made first, so that it is not the last.
    mrhiViewDef storageDef = mrhiDefaultViewDef();
    storageDef.texture = image;
    mrhiViewId storageView = {0};
    CHECK(mrhiCreateView(device, &storageDef, &storageView) == mrhi_success, "a storage view");
    mrhiViewDef viewDef = mrhiDefaultViewDef();
    viewDef.texture = texture;
    mrhiViewId view = {0};
    mrhiSamplerDef repeatDef = mrhiDefaultSamplerDef();
    repeatDef.addressU = mrhi_addressRepeat;
    mrhiSamplerId repeat = {0};
    mrhiSamplerDef clampDef = mrhiDefaultSamplerDef();
    clampDef.minFilter = mrhi_filterLinear;
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
    const mrhiHeapEntry written = {.kind = mrhi_heapStorageTexture, .view = storageView};
    const mrhiHeapEntry storage = {
        .kind = mrhi_heapStorageBuffer,
        .buffer = buffer,
        .size = MRHI_WHOLE_SIZE,
        .writable = true,
    };
    CHECK(mrhiSetHeapEntry(device, heap, HEAP_TEXTURE, &sampled) == mrhi_success &&
              mrhiSetHeapEntry(device, heap, HEAP_STORAGE, &written) == mrhi_success &&
              mrhiSetHeapEntry(device, heap, HEAP_BUFFER, &storage) == mrhi_success &&
              mrhiSetHeapSampler(device, heap, HEAP_SAMPLER, clamping ? clamp : repeat) ==
                  mrhi_success &&
              mrhiSetHeapSampler(device, heap, HEAP_OTHER_SAMPLER, clamping ? repeat : clamp) ==
                  mrhi_success,
          "the entries");
    return heap;
}

// Texels uploaded to a texture sealed in the same frame, then sampled
// past its right edge by a compute pipeline in each of two passes, each
// through its own heap, which holds a repeating or a clamping sampler
// at the index read, and each writing the texel to a buffer from its
// heap, then a word from a uniform buffer bound in a table, then a texel
// minified, the nearest or a linear blend as its sampler filters: the
// sealed texture undeclared, the buffers declared. Two heaps tell a
// driver's regions apart. Needs heterogeneous heaps, which
// MAUL_RHI_REQUIRE_BINDLESS requires of every native adapter.
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
    mrhiTextureId storage = MakeImage(device, 2, mrhi_textureStorage);
    mrhiBufferId buffers[2] = {MakeBuffer(device, 256), MakeBuffer(device, 256)};
    mrhiBufferDef uniformDef = mrhiDefaultBufferDef();
    uniformDef.size = 16;
    uniformDef.usage = mrhi_bufferUniform | mrhi_bufferCopyDestination;
    mrhiBufferId uniform = {0};
    CHECK(mrhiCreateBuffer(device, &uniformDef, &uniform) == mrhi_success, "a uniform buffer");
    // The repeating heap first, so the clamping one is not the first
    // region.
    mrhiHeapId heaps[2] = {MakeHeap(device, texture, storage, buffers[0], false),
                           MakeHeap(device, texture, storage, buffers[1], true)};
    // Each row the texel a repeating sampler reads, then the one the
    // clamping sampler reads.
    static const uint8_t pixels[16] = {10, 20, 30, 255, 255, 51, 153, 255,
                                       10, 20, 30, 255, 255, 51, 153, 255};
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
    mrhiResourceId b[2] = {{0}, {0}};
    mrhiResourceId u = {0};
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
              mrhiImportBuffer(device, buffers[0], &b[0]) == mrhi_success &&
              mrhiImportBuffer(device, buffers[1], &b[1]) == mrhi_success &&
              mrhiImportBuffer(device, uniform, &u) == mrhi_success,
          "a frame reading the heaps");
    mrhiAccess fill = Whole(u, mrhi_accessCopyDestination);
    mrhiPassId fillPass = CopyPass(device, &fill, 1);
    mrhiPassId dispatches[2] = {{0}, {0}};
    for (int i = 0; i < 2; ++i)
    {
        mrhiPassDef passDef = mrhiDefaultPassDef();
        mrhiAccess accesses[2] = {Whole(b[i], mrhi_accessStorageWrite),
                                  Whole(u, mrhi_accessUniform)};
        passDef.accesses = accesses;
        passDef.accessCount = 2;
        passDef.heap = heaps[i];
        LABEL(passDef, "bindless");
        CHECK(mrhiAddPass(device, &passDef, &dispatches[i]) == mrhi_success,
              "a pass naming a heap");
    }
    mrhiAccess reads[2] = {Whole(b[0], mrhi_accessCopySource), Whole(b[1], mrhi_accessCopySource)};
    mrhiPassId readPass = CopyPass(device, reads, 2);
    static const uint32_t kWord[4] = {0x5A1234A5u, 0, 0, 0};
    const uint32_t indices[4] = {HEAP_TEXTURE, HEAP_SAMPLER, HEAP_BUFFER, 0};
    const mrhiBinding table = {.slot = 0, .resource = u, .size = MRHI_WHOLE_SIZE};
    CHECK(mrhiCompileFrame(device) == mrhi_success &&
              mrhiBeginPass(device, fillPass) == mrhi_success &&
              mrhiWriteBuffer(device, fillPass, u, 0, kWord, sizeof(kWord)) == mrhi_success &&
              mrhiEndPass(device, fillPass) == mrhi_success,
          "the uniform buffer filled");
    for (int i = 0; i < 2; ++i)
    {
        CHECK(mrhiBeginPass(device, dispatches[i]) == mrhi_success &&
                  mrhiSetComputePipeline(device, dispatches[i], pipeline) == mrhi_success &&
                  mrhiSetBindings(device, dispatches[i], 0, &table, 1) == mrhi_success &&
                  mrhiSetRootBlock(device, dispatches[i], 0, indices, sizeof(indices)) ==
                      mrhi_success &&
                  mrhiDispatch(device, dispatches[i], 1, 1, 1) == mrhi_success &&
                  mrhiEndPass(device, dispatches[i]) == mrhi_success,
              "dispatched through a heap");
    }
    mrhiRequestId written[2] = {{0}, {0}};
    CHECK(mrhiBeginPass(device, readPass) == mrhi_success &&
              mrhiReadBuffer(device, readPass, b[0], 0, 12, &written[0]) == mrhi_success &&
              mrhiReadBuffer(device, readPass, b[1], 0, 12, &written[1]) == mrhi_success &&
              mrhiEndPass(device, readPass) == mrhi_success,
          "read");
    Finish(device, 2);
    // Minified, the nearest texel, and three quarters of the first with
    // a quarter of the second.
    static const uint8_t kMinified[2][4] = {{10, 20, 30, 255}, {71, 28, 61, 255}};
    uint8_t taken[2][12];
    for (int i = 0; i < 2; ++i)
    {
        size_t size = 0;
        CHECK(mrhiTakeReadback(device, written[i], taken[i], sizeof(taken[i]), &size) ==
                      mrhi_success &&
                  size == sizeof(taken[i]),
              "the words");
    }
    CHECK(!s_runs || (memcmp(taken[0], pixels, 4) == 0 && memcmp(taken[0] + 4, kWord, 4) == 0),
          "the repeated texel, through a heap");
    CHECK(!s_runs || (memcmp(taken[1], pixels + 4, 4) == 0 && memcmp(taken[1] + 4, kWord, 4) == 0),
          "the clamped texel, through the other");
    CHECK(!s_runs || (IsNear(taken[0] + 8, kMinified[0]) && IsNear(taken[1] + 8, kMinified[1])),
          "minified, the nearest texel, then a linear blend");
    CHECK(mrhiDestroyHeap(device, heaps[0]) == mrhi_success &&
              mrhiDestroyHeap(device, heaps[1]) == mrhi_success,
          "the heaps destroyed");
    mrhiDestroyDevice(device);
}

// The placed pipelines: vertices of four floats each, depth and stencil
// written, with blend factors that would blank the target but blending
// off, and a constant depth bias that brings what is drawn at the far
// plane in front of it (to 0.75 in a float format, 0.875 in a 24-bit
// one), then tested, without a bias.
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
    def.colorTargets[0].color = (mrhiBlendComponent){
        .srcFactor = mrhi_blendZero,
        .dstFactor = mrhi_blendZero,
        .operation = mrhi_blendAdd,
    };
    def.depthBias = -(1 << 21);
    LABEL(def, "placed");
    mrhiRequestId request = {0};
    CHECK(mrhiCreateGraphicsPipeline(scene->device, &def, &scene->placed, &request) == mrhi_success,
          "a pipeline writing depth and stencil");
    def.depthWrite = false;
    def.depthBias = 0;
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

// How a frame of CheckDrawFrame draws.
typedef enum DrawMode
{
    DRAW_DIRECT,
    DRAW_INDIRECT,
    // Two counted multi-draws over two records, the second record
    // covering the whole target: one with a count of 7 clamped to a
    // maxCount of 1, one with a count of 1 under a maxCount of 2, so that
    // only the first record draws.
    DRAW_COUNTED,
} DrawMode;

// One frame uploads the scene, draws the upper left half of the target
// (vertices 2 to 4, the triangle y > x with +Y up), directly, from
// indirect arguments or by counted multi-draws, scales the storage
// buffer in a compute pass and reads both back. The vertex entry reads
// its vertex index, which includes the first vertex either way.
static void CheckDrawFrame(Scene* scene, DrawMode mode)
{
    mrhiDevice* device = scene->device;
    bool indirect = mode != DRAW_DIRECT;
    BeginScene(scene);
    mrhiBufferDef argumentsDef = mrhiDefaultBufferDef();
    argumentsDef.size = mode == DRAW_COUNTED ? 40 : 16;
    mrhiResourceId a = {0};
    CHECK(!indirect || mrhiDeclareBuffer(device, &argumentsDef, &a) == mrhi_success,
          "an arguments buffer");
    mrhiAccess uploads[4] = {
        Whole(scene->u, mrhi_accessCopyDestination), Whole(scene->w, mrhi_accessCopyDestination),
        Whole(scene->d, mrhi_accessCopyDestination), Whole(a, mrhi_accessCopyDestination)};
    mrhiPassId upload = CopyPass(device, uploads, indirect ? 4 : 3);
    mrhiAccess draws[4] = {Whole(scene->u, mrhi_accessUniform),
                           Whole(scene->d, mrhi_accessStorageReadWrite),
                           Whole(scene->w, mrhi_accessSampled), Whole(a, mrhi_accessIndirect)};
    mrhiPassDef drawDef = DrawDef(scene, draws);
    drawDef.accessCount = indirect ? 4 : 3;
    LABEL(drawDef, "draw");
    mrhiPassId draw = {0};
    CHECK(mrhiAddPass(device, &drawDef, &draw) == mrhi_success, "a drawing pass");
    mrhiPassId compute = CopyPass(device, draws, 3);
    mrhiAccess reads[2] = {Whole(scene->t, mrhi_accessCopySource),
                           Whole(scene->d, mrhi_accessCopySource)};
    mrhiPassId read = CopyPass(device, reads, 2);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    const uint8_t white[4] = {255, 255, 255, 255};
    uint32_t values[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    // Three vertices from vertex 2, one instance from instance 0; then
    // the whole target, and the two counts.
    const uint32_t arguments[10] = {3, 1, 2, 0, 3, 1, 0, 0, 7, 1};
    const mrhiTextureCopy texel = {.resource = scene->w};
    const mrhiTexelLayout layout = {.bytesPerRow = 4, .rowsPerImage = 1};
    const mrhiExtent3d one = {1, 1, 1};
    CHECK(
        mrhiBeginPass(device, upload) == mrhi_success &&
            mrhiWriteBuffer(device, upload, scene->u, 0, kColor, sizeof(kColor)) == mrhi_success &&
            mrhiWriteTexture(device, upload, &texel, white, 4, &layout, &one) == mrhi_success &&
            mrhiWriteBuffer(device, upload, scene->d, 0, values, sizeof(values)) == mrhi_success &&
            (!indirect ||
             mrhiWriteBuffer(device, upload, a, 0, arguments, argumentsDef.size) == mrhi_success) &&
            mrhiEndPass(device, upload) == mrhi_success,
        "uploaded");
    const float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    CHECK(mrhiBeginPass(device, draw) == mrhi_success &&
              mrhiSetGraphicsPipeline(device, draw, scene->draw) == mrhi_success &&
              mrhiSetRootBlock(device, draw, 0, tint, sizeof(tint)) == mrhi_success,
          "a pipeline set");
    BindScene(scene, draw);
    mrhiFeatures features = {0};
    CHECK(mrhiGetDeviceFeatures(device, &features) == mrhi_success, "the device's features");
    CHECK(mode != DRAW_INDIRECT || features.multiDrawIndirectCount ||
              mrhiDrawIndirectCount(device, draw, a, 0, a, 0, 1) == mrhi_errorUnsupported,
          "no counted multi-draw without its feature");
    CHECK(mrhiPushDebugGroup(device, draw, "half", 4) == mrhi_success &&
              mrhiInsertDebugMarker(device, draw, "upper left", 10) == mrhi_success,
          "a debug group");
    bool drawn = false;
    switch (mode)
    {
    case DRAW_DIRECT:
        drawn = mrhiDraw(device, draw, 3, 1, 2, 0) == mrhi_success;
        break;
    case DRAW_INDIRECT:
        drawn = mrhiDrawIndirect(device, draw, a, 0) == mrhi_success;
        break;
    case DRAW_COUNTED:
        drawn = mrhiDrawIndirectCount(device, draw, a, 0, a, 32, 1) == mrhi_success &&
                mrhiDrawIndirectCount(device, draw, a, 0, a, 36, 2) == mrhi_success;
        break;
    }
    CHECK(drawn && mrhiPopDebugGroup(device, draw) == mrhi_success &&
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

// The frame memory one declared 4 by 64 texture of 4 slices or layers
// and 4 mips takes, copied to in a pass never culled.
static uint64_t SlicedBytes(mrhiDevice* device, mrhiTextureKind kind)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success, "a frame");
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.kind = kind;
    def.format = mrhi_formatRgba8Unorm;
    def.width = 4;
    def.height = 64;
    def.depthOrLayers = 4;
    def.mipLevels = 4;
    mrhiResourceId texture = {0};
    CHECK(mrhiDeclareTexture(device, &def, &texture) == mrhi_success, "a texture");
    mrhiAccess write = {.resource = texture,
                        .kind = mrhi_accessCopyDestination,
                        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING}};
    mrhiPassDef pass = mrhiDefaultPassDef();
    pass.passClass = mrhi_passTransfer;
    pass.accesses = &write;
    pass.accessCount = 1;
    pass.neverCull = true;
    mrhiPassId id = {0};
    uint64_t bytes = 0;
    CHECK(mrhiAddPass(device, &pass, &id) == mrhi_success &&
              mrhiCompileFrame(device) == mrhi_success &&
              mrhiGetFrameMemory(device, &bytes) == mrhi_success &&
              mrhiDropFrame(device) == mrhi_success,
          "measured");
    return bytes;
}

// A volume's mips halve its depth too, an array's keep their layers,
// and an edge that halves below one texel stays one: at 4 mips the
// volume holds 1,176 texels (1,024, 128, 16 and 8) and the array 1,376
// (1,024, 256, 64 and 32), 4 bytes each, which every driver's memory
// holds at least. WebGPU's
// measure is the texels themselves, so there they are exact; native
// drivers pad as their GPUs lay textures out (Metal pads volumes).
static void CheckSlicedMemory(mrhiDevice* device, bool webGpu)
{
    uint64_t volume = SlicedBytes(device, mrhi_texture3d);
    uint64_t array = SlicedBytes(device, mrhi_texture2dArray);
    // The test driver doubles any mipped texture's texels.
    CHECK(!s_runs || (volume >= 4704 && array >= 5504), "a texture's texels at least");
    CHECK(!webGpu || (volume == 4704 && array == 5504),
          "a volume's and an array's texels exactly, on WebGPU");
}

// A texel of 188 sampled through the texture's sRGB twin decodes to
// about 0.503, where the texture's own format would give 0.737: the
// triangle drawn with it is the scene's color at half strength.
static void CheckSampledTwin(Scene* scene)
{
    mrhiDevice* device = scene->device;
    BeginScene(scene);
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = 1;
    def.height = 1;
    def.viewFormats[0] = mrhi_formatRgba8UnormSrgb;
    mrhiResourceId gray = {0};
    CHECK(mrhiDeclareTexture(device, &def, &gray) == mrhi_success, "a texture with a twin");
    mrhiAccess uploads[2] = {Whole(gray, mrhi_accessCopyDestination),
                             Whole(scene->u, mrhi_accessCopyDestination)};
    mrhiPassId copy = CopyPass(device, uploads, 2);
    mrhiAccess draws[3] = {Whole(scene->u, mrhi_accessUniform),
                           Whole(scene->d, mrhi_accessStorageReadWrite),
                           Whole(gray, mrhi_accessSampled)};
    mrhiPassId draw = DrawPass(scene, draws);
    mrhiAccess reads = Whole(scene->t, mrhi_accessCopySource);
    mrhiPassId read = CopyPass(device, &reads, 1);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    const uint8_t texel[4] = {188, 188, 188, 255};
    const mrhiTexelLayout layout = {.bytesPerRow = 4, .rowsPerImage = 1};
    CHECK(mrhiBeginPass(device, copy) == mrhi_success &&
              mrhiWriteBuffer(device, copy, scene->u, 0, kColor, sizeof(kColor)) == mrhi_success &&
              mrhiWriteTexture(device, copy, &(mrhiTextureCopy){.resource = gray}, texel, 4,
                               &layout, &(mrhiExtent3d){1, 1, 1}) == mrhi_success &&
              mrhiEndPass(device, copy) == mrhi_success,
          "the color and the gray texel uploaded");
    const float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    CHECK(mrhiBeginPass(device, draw) == mrhi_success &&
              mrhiSetGraphicsPipeline(device, draw, scene->draw) == mrhi_success &&
              mrhiSetRootBlock(device, draw, 0, tint, sizeof(tint)) == mrhi_success,
          "a pipeline set");
    const mrhiBinding buffers[2] = {
        {.slot = 0, .resource = scene->u, .size = MRHI_WHOLE_SIZE},
        {.slot = 1, .resource = scene->d, .size = MRHI_WHOLE_SIZE},
    };
    CHECK(mrhiSetBindings(device, draw, 0, buffers, 2) == mrhi_success, "table 0");
    const mrhiBinding twin[2] = {
        {.slot = 0,
         .resource = gray,
         .viewFormat = mrhi_formatRgba8UnormSrgb,
         .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING}},
        {.slot = 1, .sampler = scene->sampler},
    };
    CHECK(mrhiSetBindings(device, draw, 1, twin, 2) == mrhi_success, "the twin bound");
    CHECK(mrhiDraw(device, draw, 3, 1, 2, 0) == mrhi_success &&
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
    // The scene's color, 1, 0.2, 0.6 and 1, times the decoded texel.
    static const uint8_t kHalf[4] = {128, 26, 77, 255};
    bool decoded = true;
    for (int i = 0; i < 4; ++i)
    {
        int difference = image[4 + i] - kHalf[i];
        decoded = decoded && difference >= -2 && difference <= 2;
    }
    CHECK(!s_runs || decoded, "a texture sampled through its sRGB twin decodes");
}

// A target cleared to a color whose channels all differ reads back as
// that color, channel by channel; so does a depth slice of a volume
// drawn to.
static void CheckClear(Scene* scene)
{
    mrhiDevice* device = scene->device;
    BeginScene(scene);
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){
        .resource = scene->t,
        .load = mrhi_loadClear,
        .store = mrhi_storeKeep,
        .clear = {0.2f, 0.4f, 0.6f, 0.8f},
    };
    def.colorTargetCount = 1;
    mrhiPassId clear = {0};
    CHECK(mrhiAddPass(device, &def, &clear) == mrhi_success, "a clearing pass");
    mrhiTextureDef volumeDef = mrhiDefaultTextureDef();
    volumeDef.kind = mrhi_texture3d;
    volumeDef.format = mrhi_formatRgba8Unorm;
    volumeDef.width = 4;
    volumeDef.height = 4;
    volumeDef.depthOrLayers = 3;
    mrhiResourceId volume = {0};
    CHECK(mrhiDeclareTexture(device, &volumeDef, &volume) == mrhi_success, "a volume");
    def.colorTargets[0].resource = volume;
    def.colorTargets[0].layer = 2;
    def.colorTargets[0].clear = (mrhiClearColor){0.8f, 0.6f, 0.4f, 0.2f};
    mrhiPassId slice = {0};
    CHECK(mrhiAddPass(device, &def, &slice) == mrhi_success, "a pass clearing a depth slice");
    mrhiAccess reads[2] = {Whole(scene->t, mrhi_accessCopySource),
                           Whole(volume, mrhi_accessCopySource)};
    mrhiPassId read = CopyPass(device, reads, 2);
    CHECK(mrhiCompileFrame(device) == mrhi_success &&
              mrhiBeginPass(device, clear) == mrhi_success &&
              mrhiEndPass(device, clear) == mrhi_success &&
              mrhiBeginPass(device, slice) == mrhi_success &&
              mrhiEndPass(device, slice) == mrhi_success &&
              mrhiBeginPass(device, read) == mrhi_success,
          "cleared");
    mrhiRequestId pixels = ReadTarget(scene, read);
    const mrhiTextureCopy sliceAt = {.resource = volume, .z = 2};
    mrhiRequestId slicePixels = {0};
    CHECK(mrhiReadTexture(device, read, &sliceAt, &(mrhiExtent3d){4, 4, 1}, &slicePixels) ==
              mrhi_success,
          "the slice read");
    CHECK(mrhiEndPass(device, read) == mrhi_success, "read");
    Finish(device, 2);
    uint8_t image[256];
    size_t size = 0;
    CHECK(mrhiTakeReadback(device, pixels, image, sizeof(image), &size) == mrhi_success &&
              size == sizeof(image),
          "the pixels");
    static const uint8_t kCleared[4] = {51, 102, 153, 204};
    CHECK(!s_runs || (IsNear(&image[0], kCleared) && IsNear(&image[(7 * 8 + 7) * 4], kCleared)),
          "the clear color, channel by channel");
    uint8_t texels[64];
    CHECK(mrhiTakeReadback(device, slicePixels, texels, sizeof(texels), &size) == mrhi_success &&
              size == sizeof(texels),
          "the slice's texels");
    static const uint8_t kSlice[4] = {204, 153, 102, 51};
    CHECK(!s_runs || (IsNear(&texels[0], kSlice) && IsNear(&texels[60], kSlice)),
          "a volume's depth slice cleared");
}

// An rgba8 texture of the samples, declared viewable as its sRGB twin.
static mrhiResourceId DeclareTwin(mrhiDevice* device, uint32_t samples)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = mrhi_formatRgba8Unorm;
    def.width = 4;
    def.height = 4;
    def.sampleCount = samples;
    def.viewFormats[0] = mrhi_formatRgba8UnormSrgb;
    mrhiResourceId resource = {0};
    CHECK(mrhiDeclareTexture(device, &def, &resource) == mrhi_success, "a texture with a twin");
    return resource;
}

// Targets rendered as their sRGB twin store the clear color encoded: a
// texture cleared in the twin, and four samples cleared and resolved in
// it.
static void CheckViewFormat(Scene* scene)
{
    mrhiDevice* device = scene->device;
    BeginScene(scene);
    mrhiResourceId single = DeclareTwin(device, 1);
    mrhiResourceId resolved = DeclareTwin(device, 1);
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){
        .resource = single,
        .load = mrhi_loadClear,
        .store = mrhi_storeKeep,
        .clear = {0.2f, 0.4f, 0.6f, 0.8f},
        .viewFormat = mrhi_formatRgba8UnormSrgb,
    };
    def.colorTargetCount = 1;
    mrhiPassId clear = {0};
    CHECK(mrhiAddPass(device, &def, &clear) == mrhi_success, "a pass clearing the twin");
    def.colorTargets[0].resource = DeclareTwin(device, 4);
    def.colorTargets[0].store = mrhi_storeDiscard;
    def.colorTargets[0].resolve = resolved;
    mrhiPassId resolve = {0};
    CHECK(mrhiAddPass(device, &def, &resolve) == mrhi_success, "a pass resolving in the twin");
    mrhiAccess reads[2] = {Whole(single, mrhi_accessCopySource),
                           Whole(resolved, mrhi_accessCopySource)};
    mrhiPassId read = CopyPass(device, reads, 2);
    CHECK(mrhiCompileFrame(device) == mrhi_success &&
              mrhiBeginPass(device, clear) == mrhi_success &&
              mrhiEndPass(device, clear) == mrhi_success &&
              mrhiBeginPass(device, resolve) == mrhi_success &&
              mrhiEndPass(device, resolve) == mrhi_success &&
              mrhiBeginPass(device, read) == mrhi_success,
          "cleared in the twin");
    const mrhiExtent3d extent = {4, 4, 1};
    mrhiRequestId requests[2] = {0};
    CHECK(mrhiReadTexture(device, read, &(mrhiTextureCopy){.resource = single}, &extent,
                          &requests[0]) == mrhi_success &&
              mrhiReadTexture(device, read, &(mrhiTextureCopy){.resource = resolved}, &extent,
                              &requests[1]) == mrhi_success,
          "both read");
    CHECK(mrhiEndPass(device, read) == mrhi_success, "read");
    Finish(device, 2);
    // 0.2, 0.4 and 0.6 sRGB-encoded; alpha stays linear.
    static const uint8_t kEncoded[4] = {124, 170, 203, 204};
    for (int i = 0; i < 2; ++i)
    {
        uint8_t texels[64];
        size_t size = 0;
        CHECK(mrhiTakeReadback(device, requests[i], texels, sizeof(texels), &size) ==
                      mrhi_success &&
                  size == sizeof(texels),
              "the texels");
        CHECK(!s_runs || (IsNear(&texels[0], kEncoded) && IsNear(&texels[60], kEncoded)),
              i == 0 ? "the clear color encoded" : "the resolved clear color encoded");
    }
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
// indexed from vertex 1, and triangles over the target at the far plane
// and at 0.4.
static const float kPlaced[11][4] = {
    {0.0f, 0.0f, 0.0f, 1.0f},  {-1.0f, -1.0f, 0.5f, 1.0f}, {1.0f, -1.0f, 0.5f, 1.0f},
    {-1.0f, 1.0f, 0.5f, 1.0f}, {1.0f, 1.0f, 0.5f, 1.0f},   {-1.0f, -1.0f, 1.0f, 1.0f},
    {3.0f, -1.0f, 1.0f, 1.0f}, {-1.0f, 3.0f, 1.0f, 1.0f},  {-1.0f, -1.0f, 0.4f, 1.0f},
    {3.0f, -1.0f, 0.4f, 1.0f}, {-1.0f, 3.0f, 0.4f, 1.0f},
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
// through the viewport, stencil 1 there; then green over the lower right
// quarter through the scissor, which starts off both edges, at the far
// plane but biased in front of it, farther than red, so only that
// quarter passes the depth test and takes stencil 2.
static void DrawPlaced(const Scene* scene, mrhiPassId pass, mrhiResourceId vertices,
                       mrhiResourceId indices)
{
    mrhiDevice* device = scene->device;
    const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
    const float green[4] = {0.0f, 1.0f, 0.0f, 1.0f};
    const mrhiViewport left = {0.0f, 0.0f, 4.0f, 8.0f, 0.0f, 1.0f};
    const mrhiViewport whole = {0.0f, 0.0f, 8.0f, 8.0f, 0.0f, 1.0f};
    const mrhiScissorRect lowerRight = {4, 4, 4, 4};
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
              mrhiSetScissor(device, pass, &lowerRight) == mrhi_success &&
              mrhiSetStencilReference(device, pass, 2) == mrhi_success &&
              mrhiSetRootBlock(device, pass, 0, green, sizeof(green)) == mrhi_success &&
              mrhiDraw(device, pass, 3, 1, 5, 0) == mrhi_success &&
              mrhiEndPass(device, pass) == mrhi_success,
          "placed");
}

// Render state on the target: a first pass writes depth and stencil,
// kept for a second that loads them and draws, from indirect arguments
// past a draw of nothing, blue scaled by the blend constant only where
// both tests pass, which is the lower right: the left half stays red,
// the upper right black.
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
            right = right && (x < 4 ? IsNear(pixel, kRed)
                                    : (y >= 4 ? IsNear(pixel, kBlue) : IsBlack(pixel)));
        }
    }
    CHECK(right, "red left, blended blue lower right, black upper right");
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

// A multiview pass (mrhi-0020): one draw into two layers of a target,
// red in view 0 and green in view 1; on a device without multiview, the
// shader reading the view index and a pass of two views refused.
static void CheckMultiview(mrhiDevice* device)
{
    mrhiFeatures features = {0};
    mrhiLimits limits;
    CHECK(mrhiGetDeviceFeatures(device, &features) == mrhi_success &&
              mrhiGetDeviceLimits(device, &limits) == mrhi_success,
          "the device's features and limits");
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_multiviewContainer;
    shaderDef.byteCount = sizeof(s_multiviewContainer);
    LABEL(shaderDef, "multiview");
    mrhiShaderId shader = {0};
    mrhiResult made = mrhiCreateShader(device, &shaderDef, &shader);
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.kind = mrhi_texture2dArray;
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = 8;
    textureDef.height = 8;
    textureDef.depthOrLayers = 2;
    textureDef.usage = mrhi_textureRenderTarget | mrhi_textureCopySource;
    mrhiTextureId texture = {0};
    CHECK(mrhiCreateTexture(device, &textureDef, &texture) == mrhi_success, "two layers");
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    mrhiResourceId t = {0};
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success &&
              mrhiImportTexture(device, texture, &t) == mrhi_success,
          "a frame with them");
    mrhiPassDef drawDef = mrhiDefaultPassDef();
    drawDef.colorTargets[0] = (mrhiColorTarget){
        .resource = t,
        .load = mrhi_loadClear,
        .store = mrhi_storeKeep,
        .clear = {0.0f, 0.0f, 1.0f, 1.0f},
    };
    drawDef.colorTargetCount = 1;
    drawDef.viewCount = 2;
    mrhiPassId draw = {0};
    // Vulkan's multiview has six views at least; every driver's two.
    CHECK(!features.multiview || limits.multiviewViews >= 2, "multiview with two views");
    if (!features.multiview || limits.multiviewViews < 2)
    {
        CHECK(features.multiview || made == mrhi_errorUnsupported,
              "no view index without multiview");
        CHECK(!features.multiview || made != mrhi_success ||
                  mrhiDestroyShader(device, shader) == mrhi_success,
              "the shader destroyed");
        CHECK(mrhiAddPass(device, &drawDef, &draw) == mrhi_errorUnsupported,
              "no two views without multiview");
        CHECK(mrhiDropFrame(device) == mrhi_success &&
                  mrhiDestroyTexture(device, texture) == mrhi_success,
              "dropped");
        return;
    }
    CHECK(made == mrhi_success, "a shader reading the view index");
    mrhiGraphicsPipelineDef pipelineDef = GraphicsDef(shader);
    pipelineDef.viewCount = 2;
    LABEL(pipelineDef, "two views");
    mrhiGraphicsPipelineId pipeline = {0};
    mrhiRequestId request = {0};
    CHECK(mrhiCreateGraphicsPipeline(device, &pipelineDef, &pipeline, &request) == mrhi_success,
          "a pipeline of two views");
    AwaitPipelines(device, 1, 0);
    LABEL(drawDef, "two views");
    CHECK(mrhiAddPass(device, &drawDef, &draw) == mrhi_success, "a pass of two views");
    mrhiAccess read = Whole(t, mrhi_accessCopySource);
    mrhiPassId reading = CopyPass(device, &read, 1);
    const mrhiTextureCopy source = {.resource = t};
    const mrhiExtent3d extent = {8, 8, 2};
    mrhiRequestId pixels = {0};
    CHECK(mrhiCompileFrame(device) == mrhi_success && mrhiBeginPass(device, draw) == mrhi_success &&
              mrhiSetGraphicsPipeline(device, draw, pipeline) == mrhi_success &&
              mrhiDraw(device, draw, 3, 1, 0, 0) == mrhi_success &&
              mrhiEndPass(device, draw) == mrhi_success &&
              mrhiBeginPass(device, reading) == mrhi_success &&
              mrhiReadTexture(device, reading, &source, &extent, &pixels) == mrhi_success &&
              mrhiEndPass(device, reading) == mrhi_success,
          "drawn in two views and read");
    Finish(device, 1);
    uint8_t expected[2 * 8 * 8 * 4];
    static const uint8_t kRed[4] = {255, 0, 0, 255};
    static const uint8_t kGreen[4] = {0, 255, 0, 255};
    for (size_t i = 0; i < sizeof(expected); i += 4)
    {
        memcpy(&expected[i], i < sizeof(expected) / 2 ? kRed : kGreen, 4);
    }
    CHECK(Taken(device, pixels, expected, sizeof(expected)), "red in view 0, green in view 1");
    CHECK(mrhiDestroyGraphicsPipeline(device, pipeline) == mrhi_success &&
              mrhiDestroyShader(device, shader) == mrhi_success &&
              mrhiDestroyTexture(device, texture) == mrhi_success,
          "destroyed");
}

// Pipeline statistics (mrhi-0023): a query around a triangle drawn,
// one around two dispatches of 8 invocations, and a third left
// unwritten, resolved over bytes set to 0xFF. The triangle's input
// assembly counts are exact; the stages after it ran at least once;
// the stages Maul RHI lacks and those outside each query count 0.
static void CheckStatistics(Scene* scene)
{
    enum
    {
        COUNTERS = 11,
    };
    mrhiDevice* device = scene->device;
    mrhiQuerySetId set = MakeQuerySet(device, mrhi_queryPipelineStatistics, 3);
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
    mrhiAccess uses[3] = {Whole(scene->u, mrhi_accessUniform),
                          Whole(scene->d, mrhi_accessStorageReadWrite),
                          Whole(scene->w, mrhi_accessSampled)};
    mrhiPassId draw = DrawPass(scene, uses);
    mrhiPassId dispatch = CopyPass(device, uses, 3);
    mrhiAccess resolves = Whole(r, mrhi_accessQueryResolve);
    mrhiPassId resolve = CopyPass(device, &resolves, 1);
    mrhiAccess reads = Whole(r, mrhi_accessCopySource);
    mrhiPassId read = CopyPass(device, &reads, 1);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    uint8_t ones[3 * COUNTERS * sizeof(uint64_t)];
    memset(ones, 0xFF, sizeof(ones));
    CHECK(mrhiBeginPass(device, filled) == mrhi_success &&
              mrhiWriteBuffer(device, filled, r, 0, ones, sizeof(ones)) == mrhi_success &&
              mrhiEndPass(device, filled) == mrhi_success,
          "filled");
    CHECK(mrhiBeginPass(device, draw) == mrhi_success &&
              mrhiBeginStatisticsQuery(device, draw, set, 0) == mrhi_success,
          "query 0 begun");
    DrawWith(scene, draw, scene->draw, 2);
    CHECK(mrhiEndStatisticsQuery(device, draw) == mrhi_success &&
              mrhiEndPass(device, draw) == mrhi_success,
          "drawn");
    const float five[4] = {5.0f, 0.0f, 0.0f, 0.0f};
    CHECK(mrhiBeginPass(device, dispatch) == mrhi_success &&
              mrhiBeginStatisticsQuery(device, dispatch, set, 1) == mrhi_success &&
              mrhiSetComputePipeline(device, dispatch, scene->adding) == mrhi_success &&
              mrhiSetRootBlock(device, dispatch, 0, five, sizeof(five)) == mrhi_success,
          "query 1 begun");
    BindScene(scene, dispatch);
    CHECK(mrhiDispatch(device, dispatch, 1, 1, 1) == mrhi_success &&
              mrhiDispatch(device, dispatch, 1, 1, 1) == mrhi_success &&
              mrhiEndStatisticsQuery(device, dispatch) == mrhi_success &&
              mrhiEndPass(device, dispatch) == mrhi_success,
          "dispatched");
    mrhiRequestId request = {0};
    CHECK(mrhiBeginPass(device, resolve) == mrhi_success &&
              mrhiResolveQueries(device, resolve, set, 0, 3, r, 0) == mrhi_success &&
              mrhiEndPass(device, resolve) == mrhi_success &&
              mrhiBeginPass(device, read) == mrhi_success &&
              mrhiReadBuffer(device, read, r, 0, sizeof(ones), &request) == mrhi_success &&
              mrhiEndPass(device, read) == mrhi_success,
          "resolved");
    Finish(device, 1);
    uint64_t values[3][COUNTERS];
    size_t size = 0;
    CHECK(mrhiTakeReadback(device, request, values, sizeof(values), &size) == mrhi_success &&
              size == sizeof(values),
          "the counters");
    const uint64_t* drawn = values[0];
    CHECK(!s_runs || (drawn[0] == 3 && drawn[1] == 1), "three vertices, one triangle");
    CHECK(!s_runs || (drawn[2] > 0 && drawn[5] > 0 && drawn[6] > 0 && drawn[7] > 0),
          "vertex, clipping and fragment work");
    CHECK(!s_runs ||
              (drawn[3] == 0 && drawn[4] == 0 && drawn[8] == 0 && drawn[9] == 0 && drawn[10] == 0),
          "no geometry, tessellation or compute work");
    bool dispatched = true;
    bool unwritten = true;
    for (int i = 0; i < COUNTERS - 1; ++i)
    {
        dispatched = dispatched && values[1][i] == 0;
    }
    for (int i = 0; i < COUNTERS; ++i)
    {
        unwritten = unwritten && values[2][i] == 0;
    }
    CHECK(!s_runs || (dispatched && values[1][10] == 16), "16 compute invocations, nothing else");
    CHECK(!s_runs || unwritten, "eleven zeros for the query left unwritten");
    CHECK(mrhiDestroyQuerySet(device, set) == mrhi_success &&
              mrhiDestroyBuffer(device, results) == mrhi_success,
          "destroyed");
}

static void CheckDrawing(mrhiDevice* device, bool timestamps)
{
    Scene scene = {.device = device};
    MakeScene(&scene);
    mrhiFeatures features = {0};
    CHECK(mrhiGetDeviceFeatures(device, &features) == mrhi_success, "its features");
    RUN("binding.draws", {
        CheckDrawFrame(&scene, DRAW_DIRECT);
        CheckSampledTwin(&scene);
        CheckDrawFrame(&scene, DRAW_INDIRECT);
        if (features.multiDrawIndirectCount)
        {
            CheckDrawFrame(&scene, DRAW_COUNTED);
        }
    });
    RUN("transfers.clear", {
        CheckClear(&scene);
        CheckViewFormat(&scene);
    });
    RUN("binding.culling", CheckCulling(&scene));
    RUN("api.queries", CheckQueries(&scene, timestamps));
    RUN("binding.render_state", CheckRenderState(&scene));
    RUN("binding.compute_split", CheckComputeSplit(&scene, timestamps));
    if (features.pipelineStatisticsQuery)
    {
        RUN("api.statistics", CheckStatistics(&scene));
    }
}

// One of two passes recorded at once (threads.recording): its device
// and pass, the buffer it uploads its value to, the steps it has
// recorded, and whether every call held. A thread records it, so it
// counts no failure itself.
typedef struct Recorder
{
    mrhiDevice* device;
    mrhiPassId pass;
    mrhiResourceId buffer;
    uint32_t value;
    int step;
    bool held;
} Recorder;

// Records a pass's next step: its begin, an upload of 64 words of its
// value, its end.
static void RecordStep(Recorder* recorder)
{
    uint32_t words[64];
    for (size_t i = 0; i < 64; ++i)
    {
        words[i] = recorder->value;
    }
    switch (recorder->step++)
    {
    case 0:
        recorder->held = mrhiBeginPass(recorder->device, recorder->pass) == mrhi_success;
        break;
    case 1:
        recorder->held =
            recorder->held && mrhiWriteBuffer(recorder->device, recorder->pass, recorder->buffer, 0,
                                              words, sizeof(words)) == mrhi_success;
        break;
    default:
        recorder->held =
            recorder->held && mrhiEndPass(recorder->device, recorder->pass) == mrhi_success;
        break;
    }
}

#ifdef MRHI_TEST_WEB
// No threads in the web builds: the passes record interleaved, call by
// call.
static void RecordBoth(Recorder* first, Recorder* second)
{
    for (int i = 0; i < 3; ++i)
    {
        RecordStep(first);
        RecordStep(second);
    }
}
#elif defined(_WIN32)
static DWORD WINAPI RecordAll(void* recorder)
{
    for (int i = 0; i < 3; ++i)
    {
        RecordStep(recorder);
    }
    return 0;
}

// The second pass on a thread of its own while this one records the
// first.
static void RecordBoth(Recorder* first, Recorder* second)
{
    HANDLE thread = CreateThread(nullptr, 0, RecordAll, second, 0, nullptr);
    CHECK(thread != nullptr, "a thread");
    RecordAll(first);
    if (thread != nullptr)
    {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }
}
#else
static void* RecordAll(void* recorder)
{
    for (int i = 0; i < 3; ++i)
    {
        RecordStep(recorder);
    }
    return nullptr;
}

// The second pass on a thread of its own while this one records the
// first.
static void RecordBoth(Recorder* first, Recorder* second)
{
    pthread_t thread;
    bool started = pthread_create(&thread, nullptr, RecordAll, second) == 0;
    CHECK(started, "a thread");
    RecordAll(first);
    if (started)
    {
        pthread_join(thread, nullptr);
    }
}
#endif

// Two passes of one frame recorded at once, each uploading its own value
// to its own buffer, then both read back (mrhi-0011's threading).
static void CheckThreads(mrhiDevice* device)
{
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success, "a frame");
    Recorder recorders[2] = {
        {.device = device, .buffer = Declared(device, 256), .value = 0x11111111u},
        {.device = device, .buffer = Declared(device, 256), .value = 0x22222222u},
    };
    for (int i = 0; i < 2; ++i)
    {
        mrhiAccess fill = Whole(recorders[i].buffer, mrhi_accessCopyDestination);
        recorders[i].pass = CopyPass(device, &fill, 1);
    }
    mrhiAccess reads[2] = {Whole(recorders[0].buffer, mrhi_accessCopySource),
                           Whole(recorders[1].buffer, mrhi_accessCopySource)};
    mrhiPassId read = CopyPass(device, reads, 2);
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    RecordBoth(&recorders[0], &recorders[1]);
    CHECK(recorders[0].held && recorders[1].held, "both recorded");
    mrhiRequestId requests[2] = {{0}, {0}};
    CHECK(mrhiBeginPass(device, read) == mrhi_success &&
              mrhiReadBuffer(device, read, recorders[0].buffer, 0, 256, &requests[0]) ==
                  mrhi_success &&
              mrhiReadBuffer(device, read, recorders[1].buffer, 0, 256, &requests[1]) ==
                  mrhi_success &&
              mrhiEndPass(device, read) == mrhi_success,
          "read");
    Finish(device, 2);
    for (int i = 0; i < 2; ++i)
    {
        uint32_t expected[64];
        for (size_t j = 0; j < 64; ++j)
        {
            expected[j] = recorders[i].value;
        }
        CHECK(Taken(device, requests[i], (const uint8_t*)expected, sizeof(expected)),
              "each pass's own upload");
    }
}

// A device lost through mrhiSimulateDeviceLoss with a frame and its
// readback running (mrhi-0014): the notice, then both answered
// lost, the report, calls answering lost, a clean destroy, and a new
// device on the same adapter.
static void CheckLoss(mrhiInstance* instance, mrhiAdapterId adapter)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success &&
              NextInstance(instance, &record) == mrhi_success && record.outcome == mrhi_success,
          "a device");
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(device, &frame) == mrhi_success, "a frame");
    mrhiResourceId buffer = Declared(device, 256);
    mrhiAccess fill = Whole(buffer, mrhi_accessCopyDestination);
    mrhiPassId filled = CopyPass(device, &fill, 1);
    mrhiAccess reads = Whole(buffer, mrhi_accessCopySource);
    mrhiPassId read = CopyPass(device, &reads, 1);
    const uint32_t words[4] = {1, 2, 3, 4};
    mrhiRequestId readback = {0};
    mrhiRequestId token = {0};
    CHECK(mrhiCompileFrame(device) == mrhi_success &&
              mrhiBeginPass(device, filled) == mrhi_success &&
              mrhiWriteBuffer(device, filled, buffer, 0, words, sizeof(words)) == mrhi_success &&
              mrhiEndPass(device, filled) == mrhi_success &&
              mrhiBeginPass(device, read) == mrhi_success &&
              mrhiReadBuffer(device, read, buffer, 0, sizeof(words), &readback) == mrhi_success &&
              mrhiEndPass(device, read) == mrhi_success &&
              mrhiSubmitFrame(device, &token) == mrhi_success,
          "a frame running");
    CHECK(mrhiSimulateDeviceLoss(device) == mrhi_success &&
              mrhiGetDeviceState(device) == mrhi_deviceLost,
          "lost at once");
    mrhiDeviceNotification notice;
    CHECK(mrhiNextDeviceNotification(device, &notice) == mrhi_success &&
              notice.kind == mrhi_deviceLostNotice,
          "the notice first");
    CHECK(mrhiNextDeviceNotification(device, &notice) == mrhi_success &&
              notice.kind == mrhi_deviceFrameDone && notice.requestId.index1 == token.index1 &&
              notice.outcome == mrhi_errorDeviceLost,
          "the frame answered lost");
    CHECK(mrhiNextDeviceNotification(device, &notice) == mrhi_success &&
              notice.kind == mrhi_deviceReadbackReady &&
              notice.requestId.index1 == readback.index1 && notice.outcome == mrhi_errorDeviceLost,
          "its readback answered lost");
    mrhiDeviceLossReport report;
    CHECK(mrhiGetDeviceLossReport(device, &report) == mrhi_success &&
              report.reason == mrhi_lossSimulated && report.lastSubmitted.index1 == token.index1,
          "the report");
    CHECK(mrhiSimulateDeviceLoss(device) == mrhi_errorDeviceLost &&
              mrhiBeginFrame(device, &frame) == mrhi_errorDeviceLost,
          "lost from then on");
    mrhiDestroyDevice(device);
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success &&
              NextInstance(instance, &record) == mrhi_success && record.outcome == mrhi_success &&
              mrhiGetDeviceState(device) == mrhi_deviceReady,
          "a new device on the adapter");
    mrhiDestroyDevice(device);
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
// A device opened with features: granted as asked, with a timestamp
// period where it has timestamps.
static void CheckOpened(mrhiDevice* device, const mrhiFeatures* asked)
{
    mrhiFeatures granted;
    CHECK(mrhiGetDeviceFeatures(device, &granted) == mrhi_success &&
              memcmp(&granted, asked, sizeof(granted)) == 0,
          "granted as asked");
    double period = 0.0;
    mrhiResult status = mrhiGetDeviceTimestampPeriod(device, &period);
    CHECK(asked->timestampQuery ? status == mrhi_success && period > 0.0
                                : status == mrhi_errorUnsupported,
          "a timestamp period with timestamps");
}

static void CheckDevice(mrhiInstance* instance, mrhiAdapterId adapter, const mrhiFeatures* asked)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = adapter;
    def.features = *asked;
    // Multiview asked for with the adapter's views.
    mrhiLimits limits;
    CHECK(mrhiGetAdapterLimits(instance, adapter, &limits) == mrhi_success, "limits");
    def.limits.multiviewViews = asked->multiview ? limits.multiviewViews : 1;
    LABEL(def, "conformance");
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success, "a device");
    mrhiInstanceNotification record;
    CHECK(NextInstance(instance, &record) == mrhi_success &&
              record.kind == mrhi_instanceDeviceReady && record.outcome == mrhi_success,
          "ready at the next poll");
    CHECK(mrhiGetDeviceState(device) == mrhi_deviceReady, "ready");
    RUN("api.devices", CheckOpened(device, asked));
    RUN("api.objects", CheckObjects(device, asked->timestampQuery));
    RUN("hazards.frame_memory", {
        CheckFrameMemory(device);
        mrhiAdapterInfo info = {0};
        CHECK(mrhiGetAdapterInfo(instance, adapter, &info) == mrhi_success, "its driver");
        CheckSlicedMemory(device, info.driver == mrhi_driverWebGpu);
    });
    RUN("hazards.transient_twin", CheckTransientTwin(device));
    RUN("api.pipelines", CheckPipelines(device));
    RUN("transfers.round_trip", CheckRoundTrip(device));
    RUN("hazards.aliasing", CheckAliasing(device));
    RUN("capabilities.feature_formats", CheckFeatureFormats(device, asked));
    CheckDrawing(device, asked->timestampQuery);
    RUN("capabilities.multiview", CheckMultiview(device));
    RUN("threads.recording", CheckThreads(device));
    mrhiDestroyDevice(device);
}

// An adapter's facts: of the driver, of a known kind, with a name.
static void CheckInfo(mrhiInstance* instance, mrhiAdapterId adapter, mrhiDriverKind driver)
{
    mrhiAdapterInfo info;
    CHECK(mrhiGetAdapterInfo(instance, adapter, &info) == mrhi_success, "info");
    CHECK(info.driver == driver, "the driver's adapter");
    CHECK(info.kind <= mrhi_adapterSoftware, "a kind");
    CHECK(info.nameLength > 0 && info.nameLength <= MRHI_ADAPTER_NAME_BYTES, "a name");
}

// Where MAUL_RHI_REQUIRE_STATISTICS is set, every native adapter counts
// pipeline statistics, so that their case runs.
static void CheckRequired(const mrhiFeatures* all)
{
    const char* statistics = getenv("MAUL_RHI_REQUIRE_STATISTICS");
    CHECK(all->pipelineStatisticsQuery || !s_runs || statistics == nullptr || statistics[0] == '\0',
          "pipeline statistics, required");
}

// Checks every adapter an instance lists, and that a second search
// lists the same ones: the count found.
static size_t CheckDriver(mrhiInstance* instance, mrhiDriverKind driver)
{
    mrhiAdapterId ids[16];
    size_t count = Search(instance, ids, 16);
    s_runs = driver != mrhi_driverTest;
    for (size_t i = 0; i < count; ++i)
    {
        RUN("api.adapters", CheckInfo(instance, ids[i], driver));
        RUN("capabilities.limits", CheckLimits(instance, ids[i]));
        RUN("capabilities.formats", CheckFormats(instance, ids[i]));
        mrhiFeatures none = {0};
        CheckDevice(instance, ids[i], &none);
        mrhiFeatures all;
        CHECK(mrhiGetAdapterFeatures(instance, ids[i], &all) == mrhi_success, "features");
        RUN("capabilities.required", CheckRequired(&all));
        CheckDevice(instance, ids[i], &all);
        RUN("api.cache_import", CheckCacheImport(instance, ids[i]));
        RUN("limits.retirement", CheckRetirement(instance, ids[i]));
        RUN("binding.heaps", CheckHeaps(instance, ids[i], driver != mrhi_driverTest));
        RUN("loss.simulated", CheckLoss(instance, ids[i]));
    }
    mrhiAdapterId again[16];
    RUN("api.adapters", CHECK(Search(instance, again, 16) == count &&
                                  memcmp(ids, again, count * sizeof(ids[0])) == 0,
                              "the same adapters, in the same order"));
    return count;
}

// The breaches of the driver SPI the validation layer found on the
// instances the suite made (mrhi-0025); none in builds without it.
static uint64_t s_driverFaults;

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
    s_driverFaults += mrhiGetDriverFaults(instance);
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
#ifdef MRHI_TEST_WEB
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

#if defined(MRHI_TEST_XCB) || defined(MRHI_TEST_WEB) || defined(MAUL_RHI_METAL_DRIVER) ||          \
    defined(MAUL_RHI_D3D12_DRIVER) || defined(MRHI_TEST_ANDROID)
// Whether caps meet the floors and offer 8-bit sRGB in Rec. 709, each
// color listed once.
static bool MeetsFloors(const mrhiSurfaceCaps* caps)
{
    bool srgb = false;
    bool once = true;
    for (uint32_t i = 0; i < caps->colorCount && i < MRHI_SURFACE_COLORS; ++i)
    {
        const mrhiSurfaceColor* color = &caps->colors[i];
        for (uint32_t j = 0; j < i; ++j)
        {
            const mrhiSurfaceColor* other = &caps->colors[j];
            once =
                once && (other->format != color->format || other->primaries != color->primaries ||
                         other->transfer != color->transfer || other->range != color->range);
        }
        srgb =
            srgb ||
            ((color->format == mrhi_formatBgra8Unorm || color->format == mrhi_formatRgba8Unorm) &&
             color->primaries == mrhi_primariesBt709 && color->transfer == mrhi_transferSrgb &&
             color->range == mrhi_rangeStandard);
    }
    return caps->colorCount >= 1 && caps->colorCount <= MRHI_SURFACE_COLORS && srgb && once &&
           (caps->presentModes & mrhi_presentFifo) != 0 && (caps->twinViews || caps->twinImages) &&
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
    // sRGB through the twin: as a view where the surface allows one, else
    // as the images' own format.
    mrhiFormat twin = mrhi_formatNone;
    config.color = Srgb8(caps, &twin);
    if (caps->twinViews)
    {
        config.viewFormats[0] = twin;
    }
    else
    {
        config.color.format = twin;
    }
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

// Every color the surface lists configures it, and a frame clears and
// presents on it: HDR10 and extended range as much as sRGB.
static void CheckEveryColor(mrhiDevice* device, mrhiSurfaceId surface, const mrhiSurfaceCaps* caps)
{
    for (uint32_t i = 0; i < caps->colorCount; ++i)
    {
        mrhiSurfaceConfig config = mrhiDefaultSurfaceConfig();
        config.surface = surface;
        config.color = caps->colors[i];
        config.usage = mrhi_textureRenderTarget;
        config.width = 64;
        config.height = 48;
        uint8_t pixel[4];
        CHECK(mrhiConfigureSurface(device, &config) == mrhi_success &&
                  IsAcquired(PresentRed(device, surface, false, pixel)),
              "every color listed presents");
    }
}

// Presents to the surface from a device on an adapter that can: frames
// cleared and read back, more than the images and their semaphores; an
// image given back and taken again; one presented unwritten; a
// reconfiguration; every color listed; and, where the window fixes its
// images' size, a size it does not take, which leaves the surface
// unconfigured until it is configured again, or else a size of the
// program's own.
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
    CheckEveryColor(device, surface, caps);
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
// Connects to the X server DISPLAY names, trying again for a second: a
// server busy with many clients at once may refuse a connection for a
// moment, as Xvfb under a parallel test run does.
static xcb_connection_t* ConnectX(void)
{
    xcb_connection_t* connection = xcb_connect(nullptr, nullptr);
    for (int tries = 1;
         tries < 20 && xcb_connection_has_error(connection) != 0 && getenv("DISPLAY") != nullptr;
         ++tries)
    {
        xcb_disconnect(connection);
        (void)thrd_sleep(&(struct timespec){.tv_nsec = 50000000}, nullptr);
        connection = xcb_connect(nullptr, nullptr);
    }
    return connection;
}

// A window of the X server the environment names, where there is one.
static void CheckXcbSurface(mrhiInstance* instance, const mrhiAdapterId* ids, size_t count)
{
    xcb_connection_t* connection = ConnectX();
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

#ifdef MRHI_TEST_ANDROID
// The application's window, which the system sizes: a swapchain of
// another size is scaled to it, so every size configures.
static void CheckAndroidSurface(mrhiInstance* instance, const mrhiAdapterId* ids, size_t count)
{
    const mrhiSurfaceSourceAndroid source = {
        .chain = {.type = mrhi_structSurfaceSourceAndroid},
        .window = mrhiTestAndroidWindow(),
    };
    mrhiSurfaceId surface = {0};
    CHECK(source.window != nullptr &&
              MakeSurface(instance, &source.chain, &surface) == mrhi_success,
          "a window surface");
    size_t presenting = 0;
    for (size_t i = 0; i < count; ++i)
    {
        mrhiSurfaceCaps caps;
        CHECK(mrhiGetSurfaceCaps(instance, surface, ids[i], &caps) == mrhi_success &&
                  caps.presentable && MeetsFloors(&caps),
              "presentable, with the floors");
        if (caps.presentable)
        {
            CheckPresenting(instance, ids[i], surface, &caps, false);
            presenting += 1;
        }
    }
    CHECK(count == 0 || presenting > 0, "an adapter presents there");
    CHECK(mrhiDestroySurface(instance, surface) == mrhi_success, "the surface destroyed");
}
#endif

#ifdef MAUL_RHI_D3D12_DRIVER
// A Win32 window whose client area is 64 by 48, which every adapter
// presents to; a swapchain of another size stretches to it.
static void CheckWin32Surface(mrhiInstance* instance, const mrhiAdapterId* ids, size_t count)
{
    HINSTANCE module = GetModuleHandleW(nullptr);
    const WNDCLASSW windowClass = {
        .lpfnWndProc = DefWindowProcW,
        .hInstance = module,
        .lpszClassName = L"mrhiConformance",
    };
    CHECK(RegisterClassW(&windowClass) != 0, "a window class");
    RECT rect = {0, 0, 64, 48};
    (void)AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    HWND window = CreateWindowExW(0, L"mrhiConformance", L"conformance", WS_OVERLAPPEDWINDOW, 0, 0,
                                  rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                                  module, nullptr);
    CHECK(window != nullptr, "a window");
    (void)ShowWindow(window, SW_SHOWNOACTIVATE);
    const mrhiSurfaceSourceWin32 source = {
        .chain = {.type = mrhi_structSurfaceSourceWin32},
        .hinstance = module,
        .hwnd = window,
    };
    mrhiSurfaceId surface = {0};
    CHECK(MakeSurface(instance, &source.chain, &surface) == mrhi_success, "a Win32 surface");
    size_t presenting = 0;
    for (size_t i = 0; i < count; ++i)
    {
        mrhiSurfaceCaps caps;
        CHECK(mrhiGetSurfaceCaps(instance, surface, ids[i], &caps) == mrhi_success, "caps");
        CHECK(!caps.presentable || MeetsFloors(&caps), "the floors where it presents");
        if (caps.presentable)
        {
            CheckPresenting(instance, ids[i], surface, &caps, false);
        }
        presenting += caps.presentable ? 1 : 0;
    }
    CHECK(presenting > 0 || !IsSet("MAUL_RHI_REQUIRE_SURFACE"), "an adapter presents there");
    CHECK(mrhiDestroySurface(instance, surface) == mrhi_success, "the surface destroyed");
    (void)DestroyWindow(window);
    (void)UnregisterClassW(L"mrhiConformance", module);
}
#endif

#ifdef MRHI_TEST_WEB
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

#ifdef MAUL_RHI_METAL_DRIVER
// A CAMetalLayer, which every adapter presents to; drawables a view
// resizes leave the layer out of date until it is configured again.
static void CheckMetalSurface(mrhiInstance* instance, const mrhiAdapterId* ids, size_t count)
{
    void* layer = mrhiTestNewMetalLayer(64, 48);
    const mrhiSurfaceSourceMetalLayer source = {
        .chain = {.type = mrhi_structSurfaceSourceMetalLayer},
        .layer = layer,
    };
    mrhiSurfaceId surface = {0};
    CHECK(layer != nullptr && MakeSurface(instance, &source.chain, &surface) == mrhi_success,
          "a layer surface");
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
        mrhiTestResizeMetalLayer(layer, 20, 48);
        CHECK(PresentRed(device, surface, false, pixel) == mrhi_errorOutOfDate,
              "out of date once a view resizes it");
        CHECK(Configure(device, surface, &caps, 20, 48) == mrhi_success &&
                  IsAcquired(PresentRed(device, surface, false, pixel)),
              "configured at the new size");
        mrhiDestroyDevice(device);
        mrhiTestResizeMetalLayer(layer, 64, 48);
    }
    CHECK(mrhiDestroySurface(instance, surface) == mrhi_success, "the surface destroyed");
    mrhiTestReleaseMetalLayer(layer);
}
#endif

// Surfaces on the native driver's adapters.
#ifdef MRHI_TEST_EXTERNAL
// The window of the outside driver's harness (mrhi-0024): its surface
// source, or NULL for the build platform's window system, and whether
// the window fixes its images' size.
const mrhiChain* mrhiConformanceSurface(bool* fixedSizeOut);

// Presents to the outside driver's window from each adapter that can.
static void CheckOutsideSurface(mrhiInstance* instance, const mrhiAdapterId* ids, size_t count,
                                const mrhiChain* source, bool fixedSize)
{
    mrhiSurfaceId surface = {0};
    CHECK(MakeSurface(instance, source, &surface) == mrhi_success, "the outside driver's surface");
    size_t presenting = 0;
    for (size_t i = 0; i < count; ++i)
    {
        mrhiSurfaceCaps caps;
        CHECK(mrhiGetSurfaceCaps(instance, surface, ids[i], &caps) == mrhi_success, "caps");
        CHECK(!caps.presentable || MeetsFloors(&caps), "the floors where it presents");
        if (caps.presentable)
        {
            CheckPresenting(instance, ids[i], surface, &caps, fixedSize);
        }
        presenting += caps.presentable ? 1 : 0;
    }
    CHECK(presenting > 0, "an adapter presents there");
    CHECK(mrhiDestroySurface(instance, surface) == mrhi_success, "the surface destroyed");
}
#endif

static void CheckSurfaces(mrhiInstance* instance)
{
    RUN("swapchain.foreign_sources", CheckForeignSources(instance));
    mrhiAdapterId ids[16];
    size_t count = Search(instance, ids, 16);
#ifdef MRHI_TEST_EXTERNAL
    bool fixedSize = true;
    const mrhiChain* outside = mrhiConformanceSurface(&fixedSize);
    if (outside != nullptr)
    {
        RUN("swapchain.present", CheckOutsideSurface(instance, ids, count, outside, fixedSize));
        return;
    }
#endif
#if defined(MRHI_TEST_XCB)
    RUN("swapchain.present", CheckXcbSurface(instance, ids, count));
#elif defined(MRHI_TEST_ANDROID)
    RUN("swapchain.present", CheckAndroidSurface(instance, ids, count));
#elif defined(MRHI_TEST_WEB)
    RUN("swapchain.present", CheckCanvasSurface(instance, ids, count));
#elif defined(MAUL_RHI_METAL_DRIVER)
    RUN("swapchain.present", CheckMetalSurface(instance, ids, count));
#elif defined(MAUL_RHI_D3D12_DRIVER)
    RUN("swapchain.present", CheckWin32Surface(instance, ids, count));
#else
    (void)count;
    CHECK(!IsSet("MAUL_RHI_REQUIRE_SURFACE"), "a window system where required");
#endif
}

#ifdef MRHI_TEST_EXTERNAL
// Made by the driver built outside the tree that the suite is linked
// with (MAUL_RHI_CONFORMANCE_DRIVER, mrhi-0024): its def, or a failure.
mrhiResult mrhiConformanceDriver(mrhiExternalDriverDef* driverOut);
#endif

static void TestNativeDriver(void)
{
#ifdef MRHI_TEST_EXTERNAL
    // An outside driver that finds no API to run on is skipped, unless
    // MAUL_RHI_REQUIRE_EXTERNAL is set and not empty.
    mrhiExternalDriverDef external;
    mrhiResult made = mrhiConformanceDriver(&external);
    if (made != mrhi_success)
    {
        CHECK(made == mrhi_errorUnsupported && !IsSet("MAUL_RHI_REQUIRE_EXTERNAL"),
              "the outside driver");
        printf("skip: the outside driver has no adapter here\n");
        return;
    }
    mrhiInstance* instance = Create(&external.chain);
#else
    mrhiInstance* instance = Create(nullptr);
#endif
    CHECK(instance != nullptr, "an instance");
    if (instance == nullptr)
    {
        return;
    }
    // The outside driver the suite is linked with, or else the build's
    // native driver: WebGPU on the web, Metal or D3D12 where the build
    // chose it, Vulkan elsewhere.
#ifdef MRHI_TEST_EXTERNAL
    size_t count = CheckDriver(instance, mrhi_driverExternal);
    const char* required = getenv("MAUL_RHI_REQUIRE_EXTERNAL");
#elif defined(MRHI_TEST_WEB)
    size_t count = CheckDriver(instance, mrhi_driverWebGpu);
    const char* required = getenv("MAUL_RHI_REQUIRE_WEBGPU");
#elif defined(MAUL_RHI_METAL_DRIVER)
    size_t count = CheckDriver(instance, mrhi_driverMetal);
    const char* required = getenv("MAUL_RHI_REQUIRE_METAL");
#elif defined(MAUL_RHI_D3D12_DRIVER)
    size_t count = CheckDriver(instance, mrhi_driverD3d12);
    const char* required = getenv("MAUL_RHI_REQUIRE_D3D12");
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
    s_driverFaults += mrhiGetDriverFaults(instance);
    mrhiDestroyInstance(instance);
}

// Prints the mustpass list of this SPI version: the cases by category. Unused where the runner
// calls main without arguments.
[[maybe_unused]] static void List(void)
{
    printf("# Maul RHI conformance cases, SPI version %d (mrhi-0024).\n"
           "# A driver is admitted on a run that passes every case listed.\n",
           MRHI_TEST_SPI_VERSION);
    for (size_t i = 0; i < sizeof(s_cases) / sizeof(s_cases[0]); ++i)
    {
        printf("%s\n", s_cases[i].name);
    }
}

// Each case's outcome, and failures outside any case (setting one up).
static void Report(void)
{
    int counted = 0;
    for (size_t i = 0; i < sizeof(s_cases) / sizeof(s_cases[0]); ++i)
    {
        const Case* run = &s_cases[i];
        const char* outcome = run->runs == 0 ? "not run" : run->failures > 0 ? "fail" : "pass";
        printf("case %s: %s\n", run->name, outcome);
        counted += run->failures;
    }
    if (s_failures > counted)
    {
        printf("setup: %d failures outside the cases\n", s_failures - counted);
    }
}

static int Run(void)
{
#ifdef MAUL_RHI_D3D12_DRIVER
    // Before the library opens a device, as the debug layer needs.
    mrhiTestWatchD3d12();
#endif
    TestTestDriver();
    TestNativeDriver();
    // The library's layer counts breaches of the SPI; the D3D12 debug
    // layer's errors are counted here; the Vulkan validation layer's
    // messages fail the test through CTest.
    RUN("validation.clean", {
        CHECK(s_driverFaults == 0, "no breach of the driver SPI");
#ifdef MAUL_RHI_D3D12_DRIVER
        CHECK(mrhiTestD3d12Errors() == 0, "no D3D12 debug layer error");
#endif
    });
    Report();
    return s_failures == 0 ? 0 : 1;
}

// On the web and on Android the runner calls main without arguments.
#if defined(MRHI_TEST_WEB) || defined(MRHI_TEST_ANDROID)
int main(void)
{
    return Run();
}
#else
int main(int argc, char** argv)
{
    if (argc == 2 && strcmp(argv[1], "--list") == 0)
    {
        List();
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--case") == 0)
    {
        s_only = argv[2];
    }
    else if (argc != 1)
    {
        printf("usage: test_conformance [--list | --case <name>]\n");
        return 2;
    }
    return Run();
}
#endif
