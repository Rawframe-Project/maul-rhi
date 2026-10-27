// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes the payload of a pipeline cache on the Vulkan driver, on the
// first Vulkan adapter (lavapipe in CI). The core's envelope is always
// sealed around the input, as anyone can seal it, so every input reaches
// the driver's import. The input's first byte picks which fields of
// Vulkan's own header the target copies from the device's cache before
// the import (its size, its version, the vendor, the device and the
// cache UUID), so that mutations reach each later check and
// vkCreatePipelineCache instead of stopping at the first. Another bit
// lays the device's own payload, which holds a pipeline, under the
// input, the two XORed, so that mutations reach the driver's reading of
// a real cache body, which gives the fuzzer no coverage of its own. A
// payload is taken only with a header the driver must take; then the
// device still exports its cache.

#include "bytes.h"
#include "device_core.h"
#include "sha256.h"

#include "maul-rhi/device.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/shader.h"
#include "shaders/storage_container.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// The core's envelope before the payload, as src/cache.c writes it.
#define ENVELOPE_BYTES 64
#define SIZE_AT        8
#define DIGEST_AT      16
#define DIGESTED_FROM  48

// Vulkan's VkPipelineCacheHeaderVersionOne: its size, version, vendor,
// device and UUID.
#define HEADER_BYTES 32
#define VERSION_ONE  1u
#define VENDOR_AT    8
#define UUID_AT      16

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

// One device for every input, its envelope's header and Vulkan's header
// of its own cache: the fuzzer's process is the target's, not the
// library's.
static mrhiInstance* s_instance;
static mrhiDevice* s_device;
static uint8_t s_envelope[ENVELOPE_BYTES];
static uint8_t s_header[HEADER_BYTES];
static uint8_t* s_payload;
static size_t s_payloadSize;

// Makes the storage shader's compute pipeline, so that the device's
// cache holds one.
static void MakePipeline(void)
{
    mrhiShaderDef shaderDef = mrhiDefaultShaderDef();
    shaderDef.bytes = s_storageContainer;
    shaderDef.byteCount = sizeof(s_storageContainer);
    mrhiShaderId shader = {0};
    Expect(mrhiCreateShader(s_device, &shaderDef, &shader) == mrhi_success);
    mrhiComputePipelineDef def = mrhiDefaultComputePipelineDef();
    def.shader = shader;
    def.entry = "cs";
    def.entryLength = 2;
    mrhiComputePipelineId pipeline = {0};
    mrhiRequestId request = {0};
    mrhiDeviceNotification record;
    Expect(mrhiCreateComputePipeline(s_device, &def, &pipeline, &request) == mrhi_success &&
           mrhiNextDeviceNotification(s_device, &record) == mrhi_success &&
           record.outcome == mrhi_success);
}

static void Open(void)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    Expect(mrhiCreateInstance(&def, &s_instance) == mrhi_success);
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    search.allowSoftware = true;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    size_t count = 0;
    Expect(mrhiRequestAdapters(s_instance, &search, &request) == mrhi_success &&
           mrhiNextInstanceNotification(s_instance, &record) == mrhi_success &&
           mrhiGetAdapters(s_instance, &deviceDef.adapter, 1, &count) == mrhi_success &&
           count >= 1);
    Expect(mrhiCreateDevice(s_instance, &deviceDef, &s_device, &request) == mrhi_success &&
           mrhiNextInstanceNotification(s_instance, &record) == mrhi_success &&
           record.outcome == mrhi_success);
    MakePipeline();
    size_t size = 0;
    Expect(mrhiGetPipelineCache(s_device, nullptr, 0, &size) == mrhi_success &&
           size >= ENVELOPE_BYTES + HEADER_BYTES);
    uint8_t* cache = malloc(size);
    Expect(cache != nullptr && mrhiGetPipelineCache(s_device, cache, size, &size) == mrhi_success);
    memcpy(s_envelope, cache, ENVELOPE_BYTES);
    memcpy(s_header, cache + ENVELOPE_BYTES, HEADER_BYTES);
    s_payloadSize = size - ENVELOPE_BYTES;
    s_payload = malloc(s_payloadSize);
    Expect(s_payload != nullptr);
    memcpy(s_payload, cache + ENVELOPE_BYTES, s_payloadSize);
    free(cache);
}

// Whether the driver must take a payload: Vulkan's header whole, its size
// within the payload, version one, and this device's vendor, device and
// cache UUID.
static bool IsTakeable(const uint8_t* payload, size_t size)
{
    if (size < HEADER_BYTES)
    {
        return false;
    }
    uint32_t headerSize = mrhiRead32(payload);
    return headerSize >= HEADER_BYTES && headerSize <= size &&
           mrhiRead32(payload + 4) == VERSION_ONE &&
           memcmp(payload + VENDOR_AT, s_header + VENDOR_AT, HEADER_BYTES - VENDOR_AT) == 0;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (s_device == nullptr)
    {
        Open();
    }
    if (size == 0)
    {
        return 0;
    }
    uint8_t fixes = data[0];
    bool own = (fixes & 32) != 0;
    // Over the device's own payload, at least its length.
    size_t payloadSize = own && s_payloadSize > size - 1 ? s_payloadSize : size - 1;
    size_t total = ENVELOPE_BYTES + payloadSize;
    // The exact size, so that ASan sees any read past the end.
    uint8_t* bytes = calloc(total, 1);
    Expect(bytes != nullptr);
    uint8_t* payload = bytes + ENVELOPE_BYTES;
    memcpy(bytes, s_envelope, ENVELOPE_BYTES);
    memcpy(payload, data + 1, size - 1);
    for (size_t i = 0; own && i < s_payloadSize; ++i)
    {
        payload[i] ^= s_payload[i];
    }
    if (payloadSize >= HEADER_BYTES)
    {
        if ((fixes & 1) != 0)
        {
            mrhiWrite32(payload, HEADER_BYTES);
        }
        if ((fixes & 2) != 0)
        {
            mrhiWrite32(payload + 4, VERSION_ONE);
        }
        if ((fixes & 4) != 0)
        {
            memcpy(payload + VENDOR_AT, s_header + VENDOR_AT, 4);
        }
        if ((fixes & 8) != 0)
        {
            memcpy(payload + VENDOR_AT + 4, s_header + VENDOR_AT + 4, 4);
        }
        if ((fixes & 16) != 0)
        {
            memcpy(payload + UUID_AT, s_header + UUID_AT, HEADER_BYTES - UUID_AT);
        }
    }
    mrhiWrite64(bytes + SIZE_AT, total);
    mrhiSha256(bytes + DIGESTED_FROM, total - DIGESTED_FROM, bytes + DIGEST_AT);
    mrhiResult status = mrhiImportPipelineCache(s_device, bytes, total);
    // The envelope holds, so the driver alone decides.
    Expect(status == mrhi_success || status == mrhi_errorStale);
    Expect(status != mrhi_success || IsTakeable(payload, payloadSize));
    if (status == mrhi_success)
    {
        size_t exported = 0;
        Expect(mrhiGetPipelineCache(s_device, nullptr, 0, &exported) == mrhi_success &&
               exported >= ENVELOPE_BYTES + HEADER_BYTES);
    }
    free(bytes);
    return 0;
}
