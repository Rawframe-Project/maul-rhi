// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes pipeline-cache imports (mrhi-0010), the bytes a program reads
// back from disk and hands the library, on the test driver. The input's
// first byte picks which of the envelope's checks the target satisfies
// before the import (its magic, its size, the identity of this library,
// driver and adapter, and its digest), so that mutations reach each
// later check and the driver's own reading instead of stopping at the
// first. An import is accepted only with a digest that matches, and the
// test driver takes only its own 16-byte payload.

#include "device_core.h"
#include "sha256.h"

#include "maul-rhi/device.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/test.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_BYTES 64

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

// One device for every input, and the header of an envelope it wrote:
// the fuzzer's process is the target's, not the library's.
static mrhiInstance* s_instance;
static mrhiDevice* s_device;
static uint8_t s_header[HEADER_BYTES];

static void Open(void)
{
    static mrhiTestAdapter adapter;
    static mrhiTestDriverDef driver;
    adapter = (mrhiTestAdapter){
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .limits = mrhiDefaultLimits(),
    };
    driver = (mrhiTestDriverDef){
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = &adapter,
        .adapterCount = 1,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &driver.chain;
    Expect(mrhiCreateInstance(&def, &s_instance) == mrhi_success);
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    mrhiInstanceNotification record;
    Expect(mrhiRequestAdapters(s_instance, &search, &request) == mrhi_success &&
           mrhiNextInstanceNotification(s_instance, &record) == mrhi_success);
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    size_t count = 0;
    Expect(mrhiGetAdapters(s_instance, &deviceDef.adapter, 1, &count) == mrhi_success &&
           count == 1);
    Expect(mrhiCreateDevice(s_instance, &deviceDef, &s_device, &request) == mrhi_success &&
           mrhiNextInstanceNotification(s_instance, &record) == mrhi_success);
    size_t size = 0;
    Expect(mrhiGetPipelineCache(s_device, nullptr, 0, &size) == mrhi_success &&
           size >= HEADER_BYTES);
    uint8_t* envelope = malloc(size);
    Expect(envelope != nullptr &&
           mrhiGetPipelineCache(s_device, envelope, size, &size) == mrhi_success);
    memcpy(s_header, envelope, HEADER_BYTES);
    free(envelope);
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
    size_t bytesSize = size - 1;
    // The exact size, so that ASan sees any read past the end.
    uint8_t* bytes = malloc(bytesSize > 0 ? bytesSize : 1);
    Expect(bytes != nullptr);
    if (bytesSize > 0)
    {
        memcpy(bytes, data + 1, bytesSize);
    }
    if (bytesSize >= HEADER_BYTES)
    {
        if ((fixes & 1) != 0)
        {
            memcpy(bytes, s_header, 8);
        }
        if ((fixes & 2) != 0)
        {
            for (int i = 0; i < 8; ++i)
            {
                bytes[8 + i] = (uint8_t)((uint64_t)bytesSize >> (8 * i));
            }
        }
        if ((fixes & 4) != 0)
        {
            memcpy(bytes + 48, s_header + 48, 16);
        }
        if ((fixes & 8) != 0)
        {
            mrhiSha256(bytes + 48, bytesSize - 48, bytes + 16);
        }
    }
    mrhiResult status = mrhiImportPipelineCache(s_device, bytes, bytesSize);
    Expect(status == mrhi_success || status == mrhi_errorInvalid || status == mrhi_errorStale);
    if (status == mrhi_success)
    {
        // Taken: the envelope held, and the payload was the test
        // driver's.
        uint8_t digest[MRHI_DIGEST_BYTES];
        Expect(bytesSize == HEADER_BYTES + 16 && memcmp(bytes, s_header, 8) == 0 &&
               memcmp(bytes + 48, s_header + 48, 16) == 0);
        mrhiSha256(bytes + 48, bytesSize - 48, digest);
        Expect(memcmp(digest, bytes + 16, MRHI_DIGEST_BYTES) == 0);
    }
    free(bytes);
    return 0;
}
