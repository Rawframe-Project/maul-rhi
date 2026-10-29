// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Pipeline caches on Vulkan whose envelope holds but whose payload does
// not: a cache file is the program's to read back, and a digest is no
// secret, so any payload can arrive sealed. Each one the driver must
// refuse (shorter than Vulkan's header, a header size out of range,
// another header version, vendor, device or cache UUID) leaves the
// device ready, answers stale, and keeps the device's own cache, which
// then takes a pipeline and exports. Skipped where there is no Vulkan
// adapter, unless MAUL_RHI_REQUIRE_VULKAN is set.

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "bytes.h"
#include "sha256.h"
#include "test_harness.h"

#include "maul-rhi/instance.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/shader.h"
#include "shaders/storage_container.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan_core.h>

// The core's envelope before the driver's payload, and where it keeps
// its size and digest, as src/cache.c writes them.
#define ENVELOPE_BYTES 64
#define SIZE_AT        8
#define DIGEST_AT      16
#define DIGESTED_FROM  48

// Vulkan's header at the start of the payload
// (VkPipelineCacheHeaderVersionOne).
#define HEADER_SIZE_AT    0
#define HEADER_VERSION_AT 4
#define VENDOR_AT         8
#define DEVICE_AT         12
#define UUID_AT           16
#define HEADER_BYTES      32

#define CACHE_ROOM (1u << 20)

static mrhiInstance* s_instance;
static mrhiAdapterId s_adapter;
static uint8_t s_written[CACHE_ROOM];
static size_t s_writtenSize;
static uint8_t s_cache[CACHE_ROOM];
static size_t s_cacheSize;

// Opens a device on the Vulkan adapter, given a cache or none.
static mrhiDevice* Open(const void* bytes, size_t size)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = s_adapter;
    def.pipelineCache = bytes;
    def.pipelineCacheBytes = size;
    mrhiDevice* device = nullptr;
    mrhiRequestId opened = {0};
    mrhiInstanceNotification record;
    bool ready = mrhiCreateDevice(s_instance, &def, &device, &opened) == mrhi_success &&
                 mrhiInstance_TakeNotification(s_instance, &record) == mrhi_success &&
                 record.outcome == mrhi_success;
    CHECK(ready, "a device");
    return ready ? device : nullptr;
}

// Makes the storage shader's compute pipeline, answered ready.
static bool MakePipeline(mrhiDevice* device)
{
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_storageContainer;
    shaderDef.byteCount = sizeof(s_storageContainer);
    mrhiShaderId shader = {0};
    mrhiComputePipelineDef def = mrhiDefaultComputePipelineDef();
    def.entry = "cs";
    def.entryLength = 2;
    mrhiComputePipelineId pipeline = {0};
    mrhiRequestId request = {0};
    mrhiDeviceNotification record;
    if (mrhiCreateShader(device, &shaderDef, &shader) != mrhi_success)
    {
        return false;
    }
    def.shader = shader;
    return mrhiCreateComputePipeline(device, &def, &pipeline, &request) == mrhi_success &&
           mrhiDevice_TakeNotification(device, &record) == mrhi_success &&
           record.kind == mrhi_devicePipelineReady && record.outcome == mrhi_success;
}

// Starts s_cache over from the cache as written.
static void Restore(void)
{
    memcpy(s_cache, s_written, s_writtenSize);
    s_cacheSize = s_writtenSize;
}

// Seals s_cache again at its size, as anyone can.
static void Seal(void)
{
    mrhiWrite64(s_cache + SIZE_AT, s_cacheSize);
    mrhiSha256(s_cache + DIGESTED_FROM, s_cacheSize - DIGESTED_FROM, s_cache + DIGEST_AT);
}

// Sets a 32-bit field of the payload's header.
static void SetHeader(size_t at, uint32_t value)
{
    mrhiWrite32(s_cache + ENVELOPE_BYTES + at, value);
}

// The payload's header size, as Vulkan wrote it.
static uint32_t HeaderSize(void)
{
    return mrhiRead32(s_written + ENVELOPE_BYTES + HEADER_SIZE_AT);
}

// Opens a device given s_cache, sealed: the driver refuses it, the device
// stays ready, keeps its own cache, makes a pipeline and exports again.
// The device reads a copy of the exact size, so that AddressSanitizer
// sees a read past its end.
static void CheckRefused(const char* what)
{
    Seal();
    uint8_t* exact = malloc(s_cacheSize);
    if (exact == nullptr)
    {
        printf("FAIL: %s: no room\n", what);
        s_failures += 1;
        return;
    }
    memcpy(exact, s_cache, s_cacheSize);
    mrhiDevice* device = Open(exact, s_cacheSize);
    free(exact);
    if (device == nullptr)
    {
        printf("FAIL: %s: no device\n", what);
        s_failures += 1;
        return;
    }
    if (mrhiDevice_GetPipelineCacheOutcome(device) != mrhi_errorStale)
    {
        printf("FAIL: %s: answered %d, not stale\n", what,
               (int)mrhiDevice_GetPipelineCacheOutcome(device));
        s_failures += 1;
    }
    if (!MakePipeline(device))
    {
        printf("FAIL: %s: no pipeline after it\n", what);
        s_failures += 1;
    }
    size_t size = 0;
    static uint8_t exported[CACHE_ROOM];
    bool own =
        mrhiDevice_GetPipelineCache(device, exported, sizeof(exported), &size) == mrhi_success &&
        size >= ENVELOPE_BYTES + HEADER_BYTES &&
        memcmp(exported + ENVELOPE_BYTES + VENDOR_AT, s_written + ENVELOPE_BYTES + VENDOR_AT,
               HEADER_BYTES - VENDOR_AT) == 0;
    if (!own)
    {
        printf("FAIL: %s: the device's own cache not exported\n", what);
        s_failures += 1;
    }
    mrhiDestroyDevice(device);
}

static void TestRefusals(void)
{
    mrhiDevice* device = Open(nullptr, 0);
    if (device == nullptr)
    {
        return;
    }
    CHECK(MakePipeline(device), "a pipeline to cache");
    CHECK(mrhiDevice_GetPipelineCache(device, s_written, sizeof(s_written), &s_writtenSize) ==
                  mrhi_success &&
              s_writtenSize >= ENVELOPE_BYTES + HEADER_BYTES && HeaderSize() >= HEADER_BYTES,
          "the cache written, with Vulkan's header");
    mrhiDestroyDevice(device);
    Restore();
    device = Open(s_cache, s_cacheSize);
    CHECK(device != nullptr && mrhiDevice_GetPipelineCacheOutcome(device) == mrhi_success,
          "the cache as written taken");
    mrhiDestroyDevice(device);

    Restore();
    s_cacheSize = ENVELOPE_BYTES + HEADER_BYTES - 1;
    CheckRefused("a payload shorter than Vulkan's header");
    Restore();
    SetHeader(HEADER_SIZE_AT, HEADER_BYTES - 1);
    CheckRefused("a header size shorter than the header");
    Restore();
    SetHeader(HEADER_SIZE_AT, (uint32_t)(s_cacheSize - ENVELOPE_BYTES + 1));
    CheckRefused("a header size past the payload");
    Restore();
    SetHeader(HEADER_VERSION_AT, VK_PIPELINE_CACHE_HEADER_VERSION_ONE + 1);
    CheckRefused("another header version");
    Restore();
    SetHeader(VENDOR_AT, mrhiRead32(s_cache + ENVELOPE_BYTES + VENDOR_AT) ^ 1);
    CheckRefused("another vendor");
    Restore();
    SetHeader(DEVICE_AT, mrhiRead32(s_cache + ENVELOPE_BYTES + DEVICE_AT) ^ 1);
    CheckRefused("another device");
    Restore();
    s_cache[ENVELOPE_BYTES + UUID_AT + VK_UUID_SIZE - 1] ^= 1;
    CheckRefused("another cache UUID");
}

int main(void)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    CHECK(mrhiCreateInstance(&def, &s_instance) == mrhi_success, "an instance");
    mrhiAdapterRequestDef request = mrhiDefaultAdapterRequestDef();
    request.allowSoftware = true;
    mrhiRequestId searched = {0};
    mrhiInstanceNotification record;
    size_t count = 0;
    bool found = s_instance != nullptr &&
                 mrhiInstance_RequestAdapters(s_instance, &request, &searched) == mrhi_success &&
                 mrhiInstance_TakeNotification(s_instance, &record) == mrhi_success &&
                 mrhiInstance_GetAdapters(s_instance, &s_adapter, 1, &count) == mrhi_success &&
                 count >= 1;
    if (found)
    {
        TestRefusals();
    }
    else
    {
        const char* required = getenv("MAUL_RHI_REQUIRE_VULKAN");
        CHECK(required == nullptr || required[0] == '\0', "Vulkan where required");
        printf("skip: no Vulkan adapter\n");
    }
    mrhiDestroyInstance(s_instance);
    return s_failures == 0 ? 0 : 1;
}
