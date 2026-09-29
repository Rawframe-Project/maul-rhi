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
#include "maul-rhi/instance.h"
#include "maul-rhi/test.h"

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
