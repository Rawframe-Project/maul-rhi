// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Format capabilities on the test driver: the floor every adapter meets,
// what features add, the compressed families behind their features, and
// adapters below the floor left out.

#include "test_harness.h"

#include "maul-rhi/test.h"

static mrhiTestAdapter s_adapters[2];
static mrhiTestDriverDef s_driver;

static mrhiInstance* Create(void)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    s_driver = (mrhiTestDriverDef){
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = s_adapters,
        .adapterCount = 2,
    };
    def.next = &s_driver.chain;
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "the instance");
    mrhiAdapterRequestDef request = mrhiDefaultAdapterRequestDef();
    mrhiRequestId id;
    mrhiInstanceNotification record;
    CHECK(mrhiRequestAdapters(instance, &request, &id) == mrhi_success, "the search");
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_success, "its answer");
    return instance;
}

static mrhiTestAdapter Adapter(mrhiFeatures features)
{
    return (mrhiTestAdapter){
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .features = features,
        .limits = mrhiDefaultLimits(),
    };
}

static mrhiFormatCaps Caps(mrhiInstance* instance, mrhiAdapterId adapter, mrhiFormat format)
{
    mrhiFormatCaps caps = {0};
    CHECK(mrhiGetFormatCaps(instance, adapter, format, &caps) == mrhi_success, "the caps");
    return caps;
}

static void TestTheFloor(void)
{
    s_adapters[0] = Adapter((mrhiFeatures){0});
    s_adapters[1] = Adapter((mrhiFeatures){0});
    mrhiInstance* instance = Create();
    mrhiAdapterId adapter;
    size_t count = 0;
    CHECK(mrhiGetAdapters(instance, &adapter, 1, &count) == mrhi_success && count == 2, "two");
    mrhiFormatCaps rgba8 = Caps(instance, adapter, mrhi_formatRgba8Unorm);
    CHECK(rgba8.sampling && rgba8.filtering && rgba8.rendering && rgba8.blending && rgba8.storage &&
              rgba8.sampleCounts == 5,
          "rgba8: everything, with 1 and 4 samples");
    mrhiFormatCaps srgb = Caps(instance, adapter, mrhi_formatRgba8UnormSrgb);
    CHECK(srgb.rendering && !srgb.storage, "no storage for sRGB");
    mrhiFormatCaps r32 = Caps(instance, adapter, mrhi_formatR32Float);
    CHECK(r32.sampling && !r32.filtering && r32.storage && r32.sampleCounts == 1,
          "32-bit floats are not filtered without the feature");
    mrhiFormatCaps depth = Caps(instance, adapter, mrhi_formatDepthStencil);
    CHECK(depth.rendering && !depth.filtering && depth.sampleCounts == 5, "depth with stencil");
    mrhiFormatCaps bc = Caps(instance, adapter, mrhi_formatBc7RgbaUnorm);
    CHECK(!bc.sampling && !bc.rendering && bc.sampleCounts == 0, "no BC without the feature");
    mrhiDestroyInstance(instance);
}

static void TestFeaturesAdd(void)
{
    s_adapters[0] = Adapter((mrhiFeatures){
        .float32Filterable = true,
        .rg11b10Renderable = true,
        .textureCompressionBc = true,
        .textureCompressionAstc = true,
    });
    s_adapters[0].info.driver = mrhi_driverD3d12;
    s_adapters[1] = Adapter((mrhiFeatures){0});
    mrhiInstance* instance = Create();
    mrhiAdapterId adapter;
    size_t count = 0;
    CHECK(mrhiGetAdapters(instance, &adapter, 1, &count) == mrhi_success, "listed");
    CHECK(Caps(instance, adapter, mrhi_formatR32Float).filtering, "filtered with the feature");
    CHECK(Caps(instance, adapter, mrhi_formatRg11b10Ufloat).rendering, "rendered with it");
    CHECK(Caps(instance, adapter, mrhi_formatBc1RgbaUnormSrgb).sampling, "BC with it");
    CHECK(!Caps(instance, adapter, mrhi_formatAstc4x4Unorm).sampling,
          "no ASTC on D3D12, whatever the adapter says");
    mrhiDestroyInstance(instance);
}

// Whether an adapter whose rgba8 capabilities are cut to caps is listed.
static bool ListedWith(mrhiFormat format, mrhiFormatCaps caps)
{
    s_adapters[0] = Adapter((mrhiFeatures){0});
    s_adapters[0].limitedFormat = format;
    s_adapters[0].limitedCaps = caps;
    s_adapters[1] = Adapter((mrhiFeatures){0});
    s_adapters[1].info.kind = mrhi_adapterIntegrated;
    mrhiInstance* instance = Create();
    mrhiAdapterId ids[2];
    size_t count = 0;
    CHECK(mrhiGetAdapters(instance, ids, 2, &count) == mrhi_success, "the listing");
    mrhiDestroyInstance(instance);
    return count == 2;
}

static void TestBelowTheFloorIsLeftOut(void)
{
    mrhiFormatCaps floor = {
        .sampling = true,
        .filtering = true,
        .rendering = true,
        .blending = true,
        .storage = true,
        .sampleCounts = 5,
    };
    CHECK(ListedWith(mrhi_formatRgba8Unorm, floor), "the floor is enough");
    mrhiFormatCaps cut = floor;
    cut.storage = false;
    CHECK(!ListedWith(mrhi_formatRgba8Unorm, cut), "without storage");
    cut = floor;
    cut.sampleCounts = 1;
    CHECK(!ListedWith(mrhi_formatRgba8Unorm, cut), "without 4 samples");
    cut = floor;
    cut.blending = false;
    CHECK(!ListedWith(mrhi_formatRgba8Unorm, cut), "without blending");
    CHECK(ListedWith(mrhi_formatBc1RgbaUnorm, (mrhiFormatCaps){0}),
          "a missing compressed format is no loss");
}

static void TestRefusals(void)
{
    s_adapters[0] = Adapter((mrhiFeatures){0});
    s_adapters[1] = Adapter((mrhiFeatures){0});
    mrhiInstance* instance = Create();
    mrhiAdapterId adapter;
    size_t count = 0;
    CHECK(mrhiGetAdapters(instance, &adapter, 1, &count) == mrhi_success, "listed");
    mrhiFormatCaps caps;
    CHECK(mrhiGetFormatCaps(instance, adapter, mrhi_formatNone, &caps) == mrhi_errorInvalid,
          "no format");
    CHECK(mrhiGetFormatCaps(instance, adapter, 999, &caps) == mrhi_errorInvalid, "unlisted");
    CHECK(mrhiGetFormatCaps(instance, adapter, mrhi_formatR8Unorm, nullptr) == mrhi_errorInvalid,
          "no out");
    CHECK(mrhiGetInstanceMisuse(instance) == 3, "counted");
    CHECK(mrhiGetFormatCaps(instance, (mrhiAdapterId){0, 0}, mrhi_formatR8Unorm, &caps) ==
              mrhi_errorStale,
          "a stale adapter");
    CHECK(mrhiGetFormatCaps(nullptr, adapter, mrhi_formatR8Unorm, &caps) == mrhi_errorInvalid,
          "no instance");
    mrhiDestroyInstance(instance);
}

int main(void)
{
    TestTheFloor();
    TestFeaturesAdd();
    TestBelowTheFloorIsLeftOut();
    TestRefusals();
    return s_failures == 0 ? 0 : 1;
}
