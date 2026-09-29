// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Pipeline caches on the test driver: written in their envelope, taken
// by a later device of the same library version, driver and adapter,
// and otherwise ignored and reported, never failing the device.

#include "sha256.h"
#include "test_container.h"
#include "test_device_setup.h"

#include "maul-rhi/pipeline.h"

#include <stdlib.h>

#define CACHE_ROOM 256

static uint8_t s_cache[CACHE_ROOM];
static size_t s_cacheSize;

// Opens a device given a cache, or none when bytes is NULL.
static mrhiDevice* OpenCached(const void* bytes, size_t size)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.pipelineCache = bytes;
    def.pipelineCacheBytes = size;
    return OpenWith(def, true);
}

// Makes count compute pipelines on the device, answered.
static void MakePipelines(mrhiDevice* device, uint32_t count)
{
    Reset();
    Assemble();
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_container;
    shaderDef.byteCount = s_size;
    mrhiShaderId shader;
    CHECK(mrhiCreateShader(device, &shaderDef, &shader) == mrhi_success, "a shader");
    mrhiComputePipelineDef def = mrhiDefaultComputePipelineDef();
    def.shader = shader;
    def.entry = "cs";
    def.entryLength = 2;
    for (uint32_t i = 0; i < count; ++i)
    {
        mrhiComputePipelineId pipeline;
        mrhiRequestId request;
        CHECK(mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_success,
              "a pipeline");
    }
}

// Writes the device's cache into s_cache.
static void Export(mrhiDevice* device)
{
    CHECK(mrhiGetPipelineCache(device, s_cache, sizeof(s_cache), &s_cacheSize) == mrhi_success,
          "the cache written");
}

// The pipelines a cache in s_cache says its devices made: the test
// driver's count after the 64-byte envelope and its 8-byte magic.
static uint64_t CachedCount(void)
{
    uint64_t count = 0;
    for (int i = 7; i >= 0; --i)
    {
        count = count << 8 | s_cache[64 + 8 + i];
    }
    return count;
}

// Sets a 32-bit field of s_cache and seals it again.
static void Forge(size_t at, uint32_t value)
{
    Put32(s_cache + at, value);
    mrhiSha256(s_cache + 48, s_cacheSize - 48, s_cache + 16);
}

// The outcome of a device given s_cache as it stands, size bytes of it.
static mrhiResult Outcome(size_t size)
{
    mrhiDevice* device = OpenCached(s_cache, size);
    mrhiResult outcome = mrhiGetPipelineCacheOutcome(device);
    Close(device);
    return outcome;
}

static void TestRoundTrip(void)
{
    mrhiDevice* device = OpenCached(nullptr, 0);
    CHECK(mrhiGetPipelineCacheOutcome(device) == mrhi_empty, "no cache given");
    size_t size = 0;
    CHECK(mrhiGetPipelineCache(device, nullptr, 0, &size) == mrhi_success && size == 80,
          "the size alone: the envelope and the driver's 16 bytes");
    CHECK(mrhiGetPipelineCache(device, s_cache, size - 1, &size) == mrhi_errorCapacity &&
              size == 80,
          "too little room, with the size");
    MakePipelines(device, 3);
    Export(device);
    CHECK(s_cacheSize == 80 && memcmp(s_cache, "MRPC", 4) == 0 && CachedCount() == 3,
          "the cache of three pipelines");
    Close(device);
    device = OpenCached(s_cache, s_cacheSize);
    CHECK(mrhiGetPipelineCacheOutcome(device) == mrhi_success, "taken by the next device");
    MakePipelines(device, 2);
    Export(device);
    CHECK(CachedCount() == 5, "and carried on");
    Close(device);
}

static void TestDamaged(void)
{
    mrhiDevice* device = OpenCached(nullptr, 0);
    Export(device);
    Close(device);
    size_t size = s_cacheSize;
    CHECK(Outcome(size) == mrhi_success, "the cache as written");
    CHECK(Outcome(63) == mrhi_errorInvalid, "shorter than an envelope");
    CHECK(Outcome(size - 1) == mrhi_errorInvalid, "shorter than its size");
    s_cache[size - 1] ^= 1;
    CHECK(Outcome(size) == mrhi_errorInvalid, "a damaged byte");
    s_cache[size - 1] ^= 1;
    s_cache[20] ^= 1;
    CHECK(Outcome(size) == mrhi_errorInvalid, "a damaged digest");
    s_cache[20] ^= 1;
    s_cache[47] ^= 1;
    CHECK(Outcome(size) == mrhi_errorInvalid, "a digest damaged in its last byte");
    s_cache[47] ^= 1;
    s_cache[0] = 'm';
    CHECK(Outcome(size) == mrhi_errorInvalid, "another magic");
    s_cache[0] = 'M';
    Put64(s_cache + 8, size + 1);
    CHECK(Outcome(size) == mrhi_errorInvalid, "a size past its bytes");
    Put64(s_cache + 8, size);
    CHECK(Outcome(size) == mrhi_success, "restored");
    // A blob shorter than the envelope that claims its own size, so that
    // only the envelope's length keeps the digest from reading past it.
    uint8_t* shortBlob = malloc(40);
    memcpy(shortBlob, s_cache, 40);
    Put64(shortBlob + 8, 40);
    device = OpenCached(shortBlob, 40);
    CHECK(mrhiGetPipelineCacheOutcome(device) == mrhi_errorInvalid, "shorter than an envelope");
    Close(device);
    free(shortBlob);
}

static void TestIdentity(void)
{
    // Every field of the envelope is written, whatever the buffer held.
    s_adapter.info.vendorId = 0x1002;
    s_adapter.info.deviceId = 0x73BF;
    mrhiDevice* device = OpenCached(nullptr, 0);
    memset(s_cache, 0xAB, sizeof(s_cache));
    Export(device);
    Close(device);
    // Close reset the adapter: the importing device has no ids.
    CHECK(Outcome(s_cacheSize) == mrhi_errorStale, "not taken on an adapter without ids");
    s_adapter.info.vendorId = 0x1002;
    s_adapter.info.deviceId = 0x73BF;
    CHECK(Outcome(s_cacheSize) == mrhi_success, "taken on the adapter with them");
}

static void TestStale(void)
{
    mrhiDevice* device = OpenCached(nullptr, 0);
    Export(device);
    Close(device);
    size_t fields[] = {4, 48, 52, 56, 60};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
    {
        uint32_t value = (uint32_t)s_cache[fields[i]] | (uint32_t)s_cache[fields[i] + 1] << 8 |
                         (uint32_t)s_cache[fields[i] + 2] << 16 |
                         (uint32_t)s_cache[fields[i] + 3] << 24;
        Forge(fields[i], value + 1);
        CHECK(Outcome(s_cacheSize) == mrhi_errorStale,
              "another version, library, driver, vendor or device");
        Forge(fields[i], value);
    }
    CHECK(Outcome(s_cacheSize) == mrhi_success, "restored");
    s_cache[64] = 'X';
    mrhiSha256(s_cache + 48, s_cacheSize - 48, s_cache + 16);
    CHECK(Outcome(s_cacheSize) == mrhi_errorStale, "a blob the driver declines");
    // A cache from another adapter of the same driver.
    device = OpenCached(nullptr, 0);
    Export(device);
    Close(device);
    s_adapter.info.vendorId = 0x10DE;
    CHECK(Outcome(s_cacheSize) == mrhi_errorStale, "another adapter's");
    ResetAdapter();
}

static void TestMisuse(void)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.pipelineCacheBytes = 16;
    mrhiDevice* device = OpenCached(nullptr, 0);
    size_t size = 0;
    CHECK(mrhiGetPipelineCache(device, s_cache, sizeof(s_cache), nullptr) == mrhi_errorInvalid,
          "no size out");
    CHECK(mrhiGetDeviceMisuse(device) == 1, "counted");
    CHECK(mrhiGetPipelineCache(nullptr, s_cache, sizeof(s_cache), &size) == mrhi_errorInvalid,
          "no device");
    CHECK(mrhiGetPipelineCacheOutcome(nullptr) == mrhi_errorInvalid, "no device's outcome");
    mrhiAdapterId adapter = {0};
    size_t count = 0;
    CHECK(mrhiGetAdapters(s_instance, &adapter, 1, &count) == mrhi_success, "the adapter");
    def.adapter = adapter;
    mrhiDevice* other = nullptr;
    mrhiRequestId request;
    CHECK(mrhiCreateDevice(s_instance, &def, &other, &request) == mrhi_errorInvalid &&
              other == nullptr,
          "a cache size without its bytes");
    Close(device);
    device = OpenWith(mrhiDefaultDeviceDef(), false);
    CHECK(mrhiGetPipelineCache(device, s_cache, sizeof(s_cache), &size) == mrhi_errorState,
          "a device not ready");
    Close(device);
}

int main(void)
{
    ResetAdapter();
    TestRoundTrip();
    TestDamaged();
    TestStale();
    TestIdentity();
    TestMisuse();
    return s_failures == 0 ? 0 : 1;
}
