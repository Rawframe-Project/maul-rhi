// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Bytes the library writes for a program or a driver: a pipeline cache,
// an adapter's name, a shader's digest, a lost device's message, and the
// staging a frame's uploads hand the driver. Made over memory that held
// one byte pattern, then over memory that held a second, each gives the
// same bytes, and a cache leaves the caller's bytes past its end as they
// were, so nothing the memory held before reaches them.

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

// A frame that uploads to a buffer at an offset, and to a texture whose
// rows the staging pitches apart.
static void Upload(mrhiDevice* device, uint64_t offset)
{
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 256;
    bufferDef.usage = mrhi_bufferCopyDestination;
    mrhiBufferId buffer;
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = 3;
    textureDef.height = 3;
    textureDef.usage = mrhi_textureCopyDestination;
    mrhiTextureId texture;
    CHECK(mrhiCreateBuffer(device, &bufferDef, &buffer) == mrhi_success &&
              mrhiCreateTexture(device, &textureDef, &texture) == mrhi_success,
          "the resources");
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    mrhiResourceId resources[2];
    CHECK(mrhiBeginFrame(device, &frameDef) == mrhi_success &&
              mrhiImportBuffer(device, buffer, &resources[0]) == mrhi_success &&
              mrhiImportTexture(device, texture, &resources[1]) == mrhi_success,
          "the frame");
    mrhiAccess accesses[2];
    for (uint32_t i = 0; i < 2; ++i)
    {
        accesses[i] = (mrhiAccess){
            .resource = resources[i],
            .kind = mrhi_accessCopyDestination,
            .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
        };
    }
    mrhiPassDef def = mrhiDefaultPassDef();
    def.passClass = mrhi_passTransfer;
    def.neverCull = true;
    def.accesses = accesses;
    def.accessCount = 2;
    mrhiPassId pass;
    CHECK(mrhiAddPass(device, &def, &pass) == mrhi_success, "the pass");
    static const uint8_t bytes[36] = {1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12,
                                      13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
                                      25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36};
    mrhiTextureCopy to = {.resource = resources[1]};
    mrhiTexelLayout layout = {.bytesPerRow = 12};
    mrhiExtent3d size = {3, 3, 1};
    mrhiRequestId token;
    CHECK(mrhiCompileFrame(device) == mrhi_success && mrhiBeginPass(device, pass) == mrhi_success &&
              mrhiWriteBuffer(device, pass, resources[0], offset, bytes, 12) == mrhi_success &&
              mrhiWriteTexture(device, pass, &to, bytes, sizeof(bytes), &layout, &size) ==
                  mrhi_success &&
              mrhiEndPass(device, pass) == mrhi_success &&
              mrhiSubmitFrame(device, &token) == mrhi_success,
          "the uploads submitted");
    mrhiDeviceNotification record;
    CHECK(mrhiNextDeviceNotification(device, &record) == mrhi_success &&
              record.kind == mrhi_deviceFrameDone,
          "done");
    CHECK(mrhiDestroyTexture(device, texture) == mrhi_success &&
              mrhiDestroyBuffer(device, buffer) == mrhi_success,
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
    Upload(device, offset);
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
