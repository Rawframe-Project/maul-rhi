// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Bytes the library writes for a program or a driver: a pipeline cache,
// an adapter's name, a shader's digest, a lost device's message, and the
// staging and command records a frame hands the driver. Made over memory
// that held one byte pattern, then over memory that held a second, each
// gives the same bytes, and a cache leaves the caller's bytes past its
// end as they were, so nothing the memory held before reaches them.

#include "test_harness.h"

#include "maul-rhi/device.h"
#include "maul-rhi/encoder.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/shader.h"
#include "maul-rhi/test.h"
#include "shaders/noop_container.h"
#include "shaders/storage_container.h"

#include <stdalign.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

// The byte allocations and the caller's buffers hold before the library
// writes them.
static int s_pattern;

static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)context;
    // The library asks for no more than malloc's alignment.
    void* block = alignment <= alignof(max_align_t) ? malloc(size) : nullptr;
    if (block != nullptr)
    {
        memset(block, s_pattern, size);
    }
    return block;
}

static void Free(void* block, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    free(block);
}

// Room past a cache's end, which the export leaves as it was.
#define TAIL       64
#define CACHE_ROOM 4096

// What one pass wrote.
typedef struct Written
{
    unsigned char cache[CACHE_ROOM];
    size_t cacheSize;
    mrhiAdapterInfo adapter;
    mrhiShaderInfo shader;
    mrhiDeviceLossReport loss;
    mrhiTestFrameLog log;
} Written;

static Written s_written[3];

static const mrhiAllocator kAllocator = {.alloc = Alloc, .free = Free};

static mrhiInstance* Instance(mrhiTestAdapter* adapter, mrhiTestDriverDef* driver)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &driver->chain;
    def.allocator = kAllocator;
    *driver = (mrhiTestDriverDef){
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = adapter,
        .adapterCount = 1,
    };
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "the instance");
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    mrhiInstanceNotification record;
    CHECK(mrhiRequestAdapters(instance, &search, &request) == mrhi_success &&
              mrhiNextInstanceNotification(instance, &record) == mrhi_success,
          "the adapter found");
    return instance;
}

static mrhiDevice* Device(mrhiInstance* instance, mrhiAdapterId* adapterOut)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    size_t count = 0;
    CHECK(mrhiGetAdapters(instance, &def.adapter, 1, &count) == mrhi_success && count == 1,
          "one adapter");
    *adapterOut = def.adapter;
    def.allocator = kAllocator;
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success &&
              mrhiNextInstanceNotification(instance, &record) == mrhi_success,
          "the device ready");
    return device;
}

// A shader and a compute pipeline of it, ready: the shader.
static mrhiShaderId Pipeline(mrhiDevice* device)
{
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_noopContainer;
    shaderDef.byteCount = sizeof(s_noopContainer);
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(device, &shaderDef, &shader) == mrhi_success, "the shader");
    mrhiComputePipelineDef def = mrhiDefaultComputePipelineDef();
    def.shader = shader;
    def.entry = "cs";
    def.entryLength = 2;
    mrhiComputePipelineId pipeline;
    mrhiRequestId request;
    mrhiDeviceNotification record;
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_success &&
              mrhiNextDeviceNotification(device, &record) == mrhi_success,
          "the pipeline ready");
    return shader;
}

// Fills the stack below the caller with the pattern, so that padding of
// a struct the library builds there holds it. Called through a volatile
// pointer so that no compiler inlines it away.
static void FillStack(void)
{
    volatile unsigned char stack[16384];
    for (size_t i = 0; i < sizeof stack; ++i)
    {
        stack[i] = (unsigned char)s_pattern;
    }
}

static void (*volatile s_fillStack)(void) = FillStack;

// A recording call's result, made over a stack of the pattern.
#define OVER_PATTERN(call) (s_fillStack(), (call) == mrhi_success)

// A compute pipeline of the storage container, ready.
static mrhiComputePipelineId Storage(mrhiDevice* device)
{
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_storageContainer;
    shaderDef.byteCount = sizeof(s_storageContainer);
    mrhiShaderId shader = {0};
    CHECK(mrhiCreateShader(device, &shaderDef, &shader) == mrhi_success, "the storage shader");
    mrhiComputePipelineDef def = mrhiDefaultComputePipelineDef();
    def.shader = shader;
    def.entry = "cs";
    def.entryLength = 2;
    mrhiComputePipelineId pipeline;
    mrhiRequestId request;
    mrhiDeviceNotification record;
    CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_success &&
              mrhiNextDeviceNotification(device, &record) == mrhi_success,
          "the storage pipeline ready");
    return pipeline;
}

// The frame's resources: buffers A and B, textures T and U.
enum
{
    A,
    B,
    T,
    U,
    RESOURCES,
};

typedef struct Objects
{
    mrhiBufferId buffers[2];
    mrhiTextureId textures[2];
} Objects;

static void Make(mrhiDevice* device, Objects* objects)
{
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 512;
    bufferDef.usage = mrhi_bufferCopySource | mrhi_bufferCopyDestination | mrhi_bufferStorage;
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = 3;
    textureDef.height = 3;
    textureDef.usage = mrhi_textureCopySource | mrhi_textureCopyDestination;
    mrhiTextureDef storageDef = textureDef;
    storageDef.usage = mrhi_textureCopyDestination | mrhi_textureStorage;
    CHECK(mrhiCreateBuffer(device, &bufferDef, &objects->buffers[0]) == mrhi_success &&
              mrhiCreateBuffer(device, &bufferDef, &objects->buffers[1]) == mrhi_success &&
              mrhiCreateTexture(device, &textureDef, &objects->textures[0]) == mrhi_success &&
              mrhiCreateTexture(device, &storageDef, &objects->textures[1]) == mrhi_success,
          "the resources");
}

static mrhiPassId Pass(mrhiDevice* device, mrhiPassClass passClass, const mrhiAccess* accesses,
                       uint32_t count)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = passClass;
    def.neverCull = true;
    def.accesses = accesses;
    def.accessCount = count;
    mrhiPassId pass;
    CHECK(mrhiAddPass(device, &def, &pass) == mrhi_success, "a pass");
    return pass;
}

static mrhiAccess Access(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
}

// Uploads to A at an offset, and to T, whose rows the staging pitches
// apart.
static bool Uploads(mrhiDevice* device, mrhiPassId pass, const mrhiResourceId* r, uint64_t offset)
{
    static const uint8_t bytes[36] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                      13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
                                      25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36};
    mrhiTextureCopy to = {.resource = r[T]};
    mrhiTexelLayout layout = {.bytesPerRow = 12};
    mrhiExtent3d size = {3, 3, 1};
    return OVER_PATTERN(mrhiBeginPass(device, pass)) &&
           OVER_PATTERN(mrhiWriteBuffer(device, pass, r[A], offset, bytes, 12)) &&
           OVER_PATTERN(
               mrhiWriteTexture(device, pass, &to, bytes, sizeof(bytes), &layout, &size)) &&
           OVER_PATTERN(mrhiEndPass(device, pass));
}

// Every copy between A, B, T and U, and a readback of A and of T.
static bool Copies(mrhiDevice* device, mrhiPassId pass, const mrhiResourceId* r)
{
    mrhiBufferCopy fromA = {.resource = r[A]};
    mrhiBufferCopy toB = {.resource = r[B], .offset = 256};
    mrhiTextureCopy fromT = {.resource = r[T]};
    mrhiTextureCopy toU = {.resource = r[U], .x = 1};
    mrhiExtent3d texel = {1, 1, 1};
    mrhiExtent3d all = {3, 3, 1};
    mrhiRequestId read;
    return OVER_PATTERN(mrhiBeginPass(device, pass)) &&
           OVER_PATTERN(mrhiCopyBuffer(device, pass, r[A], 0, r[B], 0, 16)) &&
           OVER_PATTERN(mrhiCopyBufferToTexture(device, pass, &fromA, &toU, &texel)) &&
           OVER_PATTERN(mrhiCopyTextureToBuffer(device, pass, &fromT, &toB, &texel)) &&
           OVER_PATTERN(mrhiCopyTexture(device, pass, &fromT, &toU, &texel)) &&
           OVER_PATTERN(mrhiReadBuffer(device, pass, r[A], 0, 16, &read)) &&
           OVER_PATTERN(mrhiReadTexture(device, pass, &fromT, &all, &read)) &&
           OVER_PATTERN(mrhiEndPass(device, pass));
}

// A dispatch with A, B and U bound to the storage table.
static bool Dispatch(mrhiDevice* device, mrhiPassId pass, const mrhiResourceId* r,
                     mrhiComputePipelineId pipeline)
{
    const mrhiBinding bindings[3] = {
        {.slot = 0, .resource = r[A], .size = MRHI_WHOLE_SIZE},
        {.slot = 1, .resource = r[B], .offset = 256, .size = 64},
        {.slot = 2,
         .resource = r[U],
         .viewKind = mrhi_texture2d,
         .range = {.mipCount = 1, .layerCount = 1}},
    };
    return OVER_PATTERN(mrhiBeginPass(device, pass)) &&
           OVER_PATTERN(mrhiSetComputePipeline(device, pass, pipeline)) &&
           OVER_PATTERN(mrhiSetBindings(device, pass, 0, bindings, 3)) &&
           OVER_PATTERN(mrhiDispatch(device, pass, 1, 1, 1)) &&
           OVER_PATTERN(mrhiEndPass(device, pass));
}

// A frame of uploads with A's at an offset, every copy and readback,
// and a dispatch with a table bound: every payload a record holds.
static void Frame(mrhiDevice* device, uint64_t offset, mrhiComputePipelineId pipeline)
{
    Objects objects;
    Make(device, &objects);
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    mrhiResourceId r[RESOURCES];
    CHECK(mrhiBeginFrame(device, &frameDef) == mrhi_success &&
              mrhiImportBuffer(device, objects.buffers[0], &r[A]) == mrhi_success &&
              mrhiImportBuffer(device, objects.buffers[1], &r[B]) == mrhi_success &&
              mrhiImportTexture(device, objects.textures[0], &r[T]) == mrhi_success &&
              mrhiImportTexture(device, objects.textures[1], &r[U]) == mrhi_success,
          "the frame");
    const mrhiAccess uploads[2] = {Access(r[A], mrhi_accessCopyDestination),
                                   Access(r[T], mrhi_accessCopyDestination)};
    const mrhiAccess copies[4] = {
        Access(r[A], mrhi_accessCopySource), Access(r[T], mrhi_accessCopySource),
        Access(r[B], mrhi_accessCopyDestination), Access(r[U], mrhi_accessCopyDestination)};
    const mrhiAccess storage[3] = {Access(r[A], mrhi_accessStorageRead),
                                   Access(r[B], mrhi_accessStorageReadWrite),
                                   Access(r[U], mrhi_accessStorageWrite)};
    mrhiPassId first = Pass(device, mrhi_passTransfer, uploads, 2);
    mrhiPassId second = Pass(device, mrhi_passTransfer, copies, 4);
    mrhiPassId third = Pass(device, mrhi_passGraphics, storage, 3);
    mrhiRequestId token;
    CHECK(mrhiCompileFrame(device) == mrhi_success, "compiled");
    CHECK(Uploads(device, first, r, offset), "the uploads");
    CHECK(Copies(device, second, r), "the copies");
    CHECK(Dispatch(device, third, r, pipeline), "the dispatch");
    CHECK(mrhiSubmitFrame(device, &token) == mrhi_success, "submitted");
    mrhiDeviceNotification record;
    bool done = false;
    while (mrhiNextDeviceNotification(device, &record) == mrhi_success)
    {
        done = done || record.kind == mrhi_deviceFrameDone;
    }
    CHECK(done, "done");
    CHECK(mrhiDestroyTexture(device, objects.textures[1]) == mrhi_success &&
              mrhiDestroyTexture(device, objects.textures[0]) == mrhi_success &&
              mrhiDestroyBuffer(device, objects.buffers[1]) == mrhi_success &&
              mrhiDestroyBuffer(device, objects.buffers[0]) == mrhi_success,
          "the resources ended");
}

// Everything made over memory and buffers of the pattern.
static void Write(int pattern, uint64_t offset, Written* written)
{
    s_pattern = pattern;
    bool lose = false;
    mrhiTestAdapter adapter = {
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .limits = mrhiDefaultLimits(),
        .frameLog = &written->log,
        .loseDevice = &lose,
    };
    memcpy(adapter.info.name, "stored", 6);
    adapter.info.nameLength = 6;
    mrhiTestDriverDef driver;
    mrhiInstance* instance = Instance(&adapter, &driver);
    mrhiAdapterId id;
    mrhiDevice* device = Device(instance, &id);
    memset(written, pattern, sizeof(*written));
    written->log = (mrhiTestFrameLog){0};
    CHECK(mrhiGetAdapterInfo(instance, id, &written->adapter) == mrhi_success, "the info");
    mrhiShaderId shader = Pipeline(device);
    CHECK(mrhiGetShaderInfo(device, shader, &written->shader) == mrhi_success, "the shader's");
    CHECK(mrhiGetPipelineCache(device, nullptr, 0, &written->cacheSize) == mrhi_success &&
              written->cacheSize + TAIL <= CACHE_ROOM,
          "the cache's size");
    CHECK(mrhiGetPipelineCache(device, written->cache, CACHE_ROOM, &written->cacheSize) ==
              mrhi_success,
          "the cache");
    Frame(device, offset, Storage(device));
    lose = true;
    mrhiDeviceNotification record;
    while (mrhiNextDeviceNotification(device, &record) == mrhi_success)
    {
    }
    CHECK(mrhiGetDeviceLossReport(device, &written->loss) == mrhi_success, "the loss report");
    mrhiDestroyDevice(device);
    mrhiDestroyInstance(instance);
}

static bool IsPattern(const unsigned char* bytes, size_t count, int pattern)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (bytes[i] != (unsigned char)pattern)
        {
            return false;
        }
    }
    return true;
}

static void TestSameBytes(void)
{
    Write(0xAA, 4, &s_written[0]);
    Write(0x55, 4, &s_written[1]);
    const Written* a = &s_written[0];
    const Written* b = &s_written[1];
    CHECK(a->cacheSize == b->cacheSize && memcmp(a->cache, b->cache, a->cacheSize) == 0,
          "the same cache");
    CHECK(IsPattern(a->cache + a->cacheSize, TAIL, 0xAA) &&
              IsPattern(b->cache + b->cacheSize, TAIL, 0x55),
          "nothing written past the cache");
    CHECK(a->adapter.nameLength == b->adapter.nameLength &&
              memcmp(a->adapter.name, b->adapter.name, sizeof(a->adapter.name)) == 0 &&
              a->adapter.vendorId == b->adapter.vendorId &&
              a->adapter.deviceId == b->adapter.deviceId,
          "the same adapter name, all its bytes");
    CHECK(memcmp(a->shader.digest, b->shader.digest, sizeof(a->shader.digest)) == 0 &&
              a->shader.entryCount == b->shader.entryCount &&
              a->shader.rootBlockBytes == b->shader.rootBlockBytes,
          "the same shader digest");
    CHECK(a->loss.messageLength == b->loss.messageLength &&
              memcmp(a->loss.message, b->loss.message, sizeof(a->loss.message)) == 0 &&
              a->loss.reason == b->loss.reason && a->loss.faultingPass == b->loss.faultingPass,
          "the same loss message, all its bytes");
    CHECK(a->log.stagingBytes == b->log.stagingBytes && a->log.stagingBytes > 0 &&
              a->log.uploadSum == b->log.uploadSum && a->log.stagingSum == b->log.stagingSum,
          "the same staging bytes handed the driver");
    CHECK(a->log.recordSum == b->log.recordSum, "the same command records handed the driver");
    // The sum reads the records' bytes: another offset changes it alone.
    Write(0xAA, 8, &s_written[2]);
    CHECK(s_written[2].log.records == a->log.records &&
              s_written[2].log.recordSum == a->log.recordSum + 4,
          "an offset 4 bytes on adds 4 to the records' bytes");
    // The buffer's 1 to 12 and the texture's 1 to 36, and zeros around them.
    CHECK(a->log.stagingSum == 78 + 666, "nothing but the uploads and zeros");
}

int main(void)
{
    TestSameBytes();
    return s_failures == 0 ? 0 : 1;
}
