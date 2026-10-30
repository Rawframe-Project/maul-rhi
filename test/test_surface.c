// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Surfaces on the test driver: their sources, their ends, what each
// adapter can do with them, and adapter searches that name one. The test
// driver traps when an instance goes before its surfaces.

#include "test_harness.h"

#include "maul-rhi/test.h"

#include <string.h>

static mrhiTestAdapter s_adapters[2];
static mrhiTestDriverDef s_driver;

// An instance with a discrete and an integrated adapter, both found;
// their ids go to adaptersOut when it is not NULL.
static mrhiInstance* Create(uint32_t surfaces, mrhiAdapterId* adaptersOut)
{
    for (size_t i = 0; i < 2; ++i)
    {
        s_adapters[i] = (mrhiTestAdapter){
            .info = {.driver = mrhi_driverTest,
                     .kind = i == 0 ? mrhi_adapterDiscrete : mrhi_adapterIntegrated},
            .limits = mrhiDefaultLimits(),
        };
    }
    s_driver = (mrhiTestDriverDef){
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = s_adapters,
        .adapterCount = 2,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &s_driver.chain;
    def.limits.surfaces = surfaces;
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "the instance");
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    mrhiInstanceNotification record;
    CHECK(mrhiInstance_RequestAdapters(instance, &search, &request) == mrhi_success, "the search");
    CHECK(mrhiInstance_TakeNotification(instance, &record) == mrhi_success, "found");
    size_t count = 0;
    mrhiAdapterId adapters[2];
    CHECK(mrhiInstance_GetAdapters(instance, adapters, 2, &count) == mrhi_success && count == 2,
          "two");
    if (adaptersOut != nullptr)
    {
        adaptersOut[0] = adapters[0];
        adaptersOut[1] = adapters[1];
    }
    return instance;
}

// A test source presenting on the adapters in the mask.
static mrhiSurfaceSourceTest Source(uint32_t presenting)
{
    return (mrhiSurfaceSourceTest){
        .chain = {.next = nullptr, .type = mrhi_structSurfaceSourceTest},
        .presentingAdapters = presenting,
    };
}

static mrhiResult Make(mrhiInstance* instance, const mrhiChain* next, mrhiSurfaceId* surfaceOut)
{
    mrhiSurfaceDef def = mrhiDefaultSurfaceDef();
    def.next = next;
    def.label = "main window";
    def.labelLength = 11;
    return mrhiCreateSurface(instance, &def, surfaceOut);
}

static void TestSources(void)
{
    mrhiInstance* instance = Create(4, nullptr);
    mrhiSurfaceSourceTest test = Source(1);
    mrhiSurfaceId surface;
    CHECK(Make(instance, &test.chain, &surface) == mrhi_success, "a test surface");
    CHECK(surface.index1 != 0, "an id");
    CHECK(mrhiDestroySurface(instance, surface) == mrhi_success, "destroyed");
    CHECK(mrhiDestroySurface(instance, surface) == mrhi_errorStale, "ended");
    CHECK(Make(instance, nullptr, &surface) == mrhi_errorInvalid, "no source");
    mrhiSurfaceSourceTest second = Source(1);
    test.chain.next = &second.chain;
    CHECK(Make(instance, &test.chain, &surface) == mrhi_errorInvalid, "two sources");
    mrhiChain hint = {.next = nullptr, .type = 0x7000u | 0x80000000u};
    test.chain.next = &hint;
    CHECK(Make(instance, &test.chain, &surface) == mrhi_success, "a source and a hint");
    CHECK(mrhiDestroySurface(instance, surface) == mrhi_success, "destroyed");
    mrhiChain critical = {.next = nullptr, .type = 0x7000u};
    test.chain.next = &critical;
    CHECK(Make(instance, &test.chain, &surface) == mrhi_errorUnsupported,
          "an unknown critical extension");
    mrhiSurfaceSourceWin32 win32 = {
        .chain = {.next = nullptr, .type = mrhi_structSurfaceSourceWin32},
    };
    CHECK(Make(instance, &win32.chain, &surface) == mrhi_errorUnsupported,
          "a source the driver cannot use");
    mrhiSurfaceSourceCanvas canvas = {
        .chain = {.next = nullptr, .type = mrhi_structSurfaceSourceCanvas},
    };
    CHECK(Make(instance, &canvas.chain, &surface) == mrhi_errorUnsupported, "a canvas");
    CHECK(mrhiInstance_GetMisuse(instance) == 2, "no source and two sources counted");
    mrhiDestroyInstance(instance);
}

static void TestRefusals(void)
{
    mrhiInstance* instance = Create(1, nullptr);
    mrhiSurfaceSourceTest test = Source(1);
    mrhiSurfaceDef def = mrhiDefaultSurfaceDef();
    def.next = &test.chain;
    mrhiSurfaceId surface;
    CHECK(mrhiCreateSurface(nullptr, &def, &surface) == mrhi_errorInvalid, "no instance");
    CHECK(mrhiCreateSurface(instance, nullptr, &surface) == mrhi_errorInvalid, "no def");
    CHECK(mrhiCreateSurface(instance, &def, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiDestroySurface(nullptr, surface) == mrhi_errorInvalid, "no instance to destroy on");
    CHECK(mrhiDestroySurface(instance, (mrhiSurfaceId){0, 0}) == mrhi_errorStale, "null");
    def.cookie = 0;
    CHECK(mrhiCreateSurface(instance, &def, &surface) == mrhi_errorInvalid, "no cookie");
    def = mrhiDefaultSurfaceDef();
    def.next = &test.chain;
    def.label = "\xFF";
    def.labelLength = 1;
    CHECK(mrhiCreateSurface(instance, &def, &surface) == mrhi_errorInvalid, "a label of FF");
    static char longLabel[MRHI_MAX_LABEL_BYTES + 1];
    memset(longLabel, 's', sizeof(longLabel));
    def.label = longLabel;
    def.labelLength = MRHI_MAX_LABEL_BYTES + 1;
    CHECK(mrhiCreateSurface(instance, &def, &surface) == mrhi_errorLimit, "a label past the bound");
    CHECK(mrhiInstance_GetMisuse(instance) == 5, "each counted");
    test.fail = true;
    CHECK(Make(instance, &test.chain, &surface) == mrhi_errorPlatform, "the driver fails");
    test.fail = false;
    mrhiSurfaceId other;
    CHECK(Make(instance, &test.chain, &surface) == mrhi_success, "no slot lost");
    CHECK(Make(instance, &test.chain, &other) == mrhi_errorCapacity, "the limit");
    mrhiDestroyInstance(instance);
    mrhiInstanceDef bare = mrhiDefaultInstanceDef();
    CHECK(mrhiCreateInstance(&bare, &instance) == mrhi_success, "an instance without a driver");
    CHECK(Make(instance, &test.chain, &surface) == mrhi_errorUnsupported, "no driver");
    mrhiDestroyInstance(instance);
    bare.limits.surfaces = 0;
    CHECK(mrhiCreateInstance(&bare, &instance) == mrhi_errorInvalid, "no surfaces");
}

static void TestCaps(void)
{
    mrhiAdapterId adapters[2];
    mrhiInstance* instance = Create(4, adapters);
    mrhiSurfaceSourceTest test = Source(1);
    test.caps.colorCount = 2;
    test.caps.colors[0] = (mrhiSurfaceColor){
        .format = mrhi_formatRgba16Float,
        .transfer = mrhi_transferLinear,
        .range = mrhi_rangeExtended,
    };
    test.caps.colors[1] = (mrhiSurfaceColor){.format = mrhi_formatBgra8Unorm};
    test.caps.presentModes = mrhi_presentMailboxBit;
    test.caps.usages = mrhi_textureCopySource;
    mrhiSurfaceId surface;
    CHECK(Make(instance, &test.chain, &surface) == mrhi_success, "the surface");
    mrhiSurfaceCaps caps;
    CHECK(mrhiSurface_GetCaps(instance, surface, adapters[0], &caps) == mrhi_success, "read");
    CHECK(caps.presentable, "the discrete adapter presents");
    CHECK(caps.colorCount == 2 && caps.colors[0].format == mrhi_formatRgba16Float &&
              caps.colors[0].range == mrhi_rangeExtended,
          "the preferred color first");
    CHECK(caps.presentModes == (mrhi_presentFifoBit | mrhi_presentMailboxBit), "fifo and mailbox");
    CHECK(caps.alphaModes == mrhi_alphaOpaqueBit, "opaque");
    CHECK(caps.usages == (mrhi_textureRenderTarget | mrhi_textureCopySource), "the usages");
    CHECK(mrhiSurface_GetCaps(instance, surface, adapters[1], &caps) == mrhi_success, "read");
    CHECK(!caps.presentable && caps.colorCount == 0 && caps.presentModes == 0,
          "the integrated adapter does not present, and nothing is filled");
    CHECK(mrhiSurface_GetCaps(instance, surface, (mrhiAdapterId){0, 0}, &caps) == mrhi_errorStale,
          "no adapter");
    CHECK(mrhiSurface_GetCaps(instance, surface, adapters[0], nullptr) == mrhi_errorInvalid,
          "no out");
    CHECK(mrhiSurface_GetCaps(nullptr, surface, adapters[0], &caps) == mrhi_errorInvalid,
          "no instance");
    CHECK(mrhiDestroySurface(instance, surface) == mrhi_success, "destroyed");
    CHECK(mrhiSurface_GetCaps(instance, surface, adapters[0], &caps) == mrhi_errorStale,
          "a surface that ended");
    CHECK(mrhiInstance_GetMisuse(instance) == 1, "the missing out counted");
    // A surface of the most colors.
    test.caps.colorCount = MRHI_SURFACE_COLORS;
    for (uint32_t i = 0; i < MRHI_SURFACE_COLORS; ++i)
    {
        test.caps.colors[i] = (mrhiSurfaceColor){.format = mrhi_formatBgra8Unorm};
    }
    CHECK(Make(instance, &test.chain, &surface) == mrhi_success &&
              mrhiSurface_GetCaps(instance, surface, adapters[0], &caps) == mrhi_success &&
              caps.presentable && caps.colorCount == MRHI_SURFACE_COLORS,
          "the most colors");
    CHECK(mrhiDestroySurface(instance, surface) == mrhi_success, "destroyed");
    mrhiDestroyInstance(instance);
}

// Searches for adapters that present to a surface.
static void TestCompatibleSurface(void)
{
    mrhiInstance* instance = Create(4, nullptr);
    mrhiSurfaceSourceTest test = Source(2);
    mrhiSurfaceId surface;
    CHECK(Make(instance, &test.chain, &surface) == mrhi_success, "the surface");
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    search.compatibleSurface = surface;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    CHECK(mrhiInstance_RequestAdapters(instance, &search, &request) == mrhi_success, "asked");
    CHECK(mrhiInstance_TakeNotification(instance, &record) == mrhi_success &&
              record.outcome == mrhi_success,
          "answered");
    mrhiAdapterId adapter;
    size_t count = 0;
    CHECK(mrhiInstance_GetAdapters(instance, &adapter, 1, &count) == mrhi_success && count == 1,
          "only the adapter that presents");
    mrhiAdapterInfo info;
    CHECK(mrhiAdapter_GetInfo(instance, adapter, &info) == mrhi_success &&
              info.kind == mrhi_adapterIntegrated,
          "the integrated one");
    CHECK(mrhiInstance_RequestAdapters(instance, &search, &request) == mrhi_success, "asked again");
    CHECK(mrhiDestroySurface(instance, surface) == mrhi_success, "then the surface ends");
    CHECK(mrhiInstance_TakeNotification(instance, &record) == mrhi_success &&
              record.outcome == mrhi_errorStale,
          "the answer is stale");
    CHECK(mrhiInstance_GetAdapters(instance, nullptr, 0, &count) == mrhi_success && count == 0,
          "no adapter listed");
    CHECK(mrhiInstance_RequestAdapters(instance, &search, &request) == mrhi_errorStale,
          "a surface that ended");
    mrhiDestroyInstance(instance);
}

// An instance destroyed with surfaces left destroys them first.
static void TestInstanceEnds(void)
{
    mrhiInstance* instance = Create(4, nullptr);
    mrhiSurfaceSourceTest test = Source(1);
    mrhiSurfaceId surface;
    for (int i = 0; i < 3; ++i)
    {
        CHECK(Make(instance, &test.chain, &surface) == mrhi_success, "a surface");
    }
    CHECK(mrhiDestroySurface(instance, surface) == mrhi_success, "one of them destroyed");
    mrhiDestroyInstance(instance);
}

// The fallback order over caps built by hand: the color asked for, then
// linear half floats of extended range, then the first 8-bit sRGB color;
// none of the three is unsupported, and caps past their array invalid.
static void TestSuggestion(void)
{
    // Static, so that their padding is zero as the suggestion's is, and
    // memcmp compares their colors alone.
    static const mrhiSurfaceColor bgra = {mrhi_formatBgra8Unorm, mrhi_primariesBt709,
                                          mrhi_transferSrgb, mrhi_rangeStandard};
    static const mrhiSurfaceColor rgba = {mrhi_formatRgba8Unorm, mrhi_primariesBt709,
                                          mrhi_transferSrgb, mrhi_rangeStandard};
    static const mrhiSurfaceColor hdr = {mrhi_formatRgba16Float, mrhi_primariesBt709,
                                         mrhi_transferLinear, mrhi_rangeExtended};
    static const mrhiSurfaceColor p3 = {mrhi_formatRgba16Float, mrhi_primariesDisplayP3,
                                        mrhi_transferLinear, mrhi_rangeExtended};
    static const mrhiSurfaceColor pq = {mrhi_formatRgba16Float, mrhi_primariesBt2020,
                                        mrhi_transferPq, mrhi_rangeStandard};
    static const mrhiSurfaceColor linear = {mrhi_formatRgba16Float, mrhi_primariesBt709,
                                            mrhi_transferLinear, mrhi_rangeStandard};
    mrhiSurfaceCaps caps = {.presentable = true, .colorCount = 5};
    caps.colors[0] = p3;
    caps.colors[1] = bgra;
    caps.colors[2] = linear;
    caps.colors[3] = hdr;
    caps.colors[4] = rgba;
    mrhiSurfaceColor color = {0};
    CHECK(mrhiSuggestSurfaceColor(&caps, &p3, &color) == mrhi_success &&
              memcmp(&color, &p3, sizeof(color)) == 0,
          "the color asked for");
    CHECK(mrhiSuggestSurfaceColor(&caps, &pq, &color) == mrhi_success &&
              memcmp(&color, &hdr, sizeof(color)) == 0,
          "linear half floats of extended range next, not of standard range");
    caps.colors[3] = p3;
    CHECK(mrhiSuggestSurfaceColor(&caps, &pq, &color) == mrhi_success &&
              memcmp(&color, &bgra, sizeof(color)) == 0,
          "then the first 8-bit sRGB color");
    caps.colors[1] = pq;
    CHECK(mrhiSuggestSurfaceColor(&caps, &hdr, &color) == mrhi_success &&
              memcmp(&color, &rgba, sizeof(color)) == 0,
          "any 8-bit sRGB format");
    mrhiSurfaceColor inPlace = hdr;
    CHECK(mrhiSuggestSurfaceColor(&caps, &inPlace, &inPlace) == mrhi_success &&
              memcmp(&inPlace, &rgba, sizeof(inPlace)) == 0,
          "suggested in place, as into another variable");
    inPlace = p3;
    CHECK(mrhiSuggestSurfaceColor(&caps, &inPlace, &inPlace) == mrhi_success &&
              memcmp(&inPlace, &p3, sizeof(inPlace)) == 0,
          "the color asked for, in place");
    caps.colorCount = 4;
    CHECK(mrhiSuggestSurfaceColor(&caps, &hdr, &color) == mrhi_errorUnsupported &&
              memcmp(&color, &rgba, sizeof(color)) == 0,
          "none of the three, the output left");
    caps.colorCount = MRHI_SURFACE_COLORS + 1;
    CHECK(mrhiSuggestSurfaceColor(&caps, &hdr, &color) == mrhi_errorInvalid &&
              memcmp(&color, &rgba, sizeof(color)) == 0,
          "caps past their array, the output left");
    for (uint32_t i = 0; i < MRHI_SURFACE_COLORS; ++i)
    {
        caps.colors[i] = i + 1 == MRHI_SURFACE_COLORS ? hdr : pq;
    }
    caps.colorCount = MRHI_SURFACE_COLORS;
    CHECK(mrhiSuggestSurfaceColor(&caps, &hdr, &color) == mrhi_success &&
              memcmp(&color, &hdr, sizeof(color)) == 0,
          "the most colors, the one asked for last");
    // Kinds are int32_t: an asked color of negative kinds matches none
    // the caps report, so the fallback order picks.
    static const int32_t negative[] = {-1, INT32_MIN};
    for (size_t i = 0; i < sizeof(negative) / sizeof(negative[0]); ++i)
    {
        int32_t n = negative[i];
        const mrhiSurfaceColor asked = {n, n, n, n};
        CHECK(mrhiSuggestSurfaceColor(&caps, &asked, &color) == mrhi_success &&
                  memcmp(&color, &hdr, sizeof(color)) == 0,
              "a negative color asked for, the fallback picked");
    }
    CHECK(mrhiSuggestSurfaceColor(&caps, &hdr, &caps.colors[MRHI_SURFACE_COLORS - 1]) ==
                  mrhi_success &&
              memcmp(&caps.colors[MRHI_SURFACE_COLORS - 1], &hdr, sizeof(hdr)) == 0,
          "into the caps' own color it picks");
    caps.colors[MRHI_SURFACE_COLORS - 1] = hdr;
    CHECK(mrhiSuggestSurfaceColor(&caps, &hdr, &caps.colors[0]) == mrhi_success &&
              memcmp(&caps.colors[0], &hdr, sizeof(hdr)) == 0,
          "into another of the caps' colors");
    caps.colorCount = 1;
    CHECK(mrhiSuggestSurfaceColor(nullptr, &hdr, &color) == mrhi_errorInvalid &&
              mrhiSuggestSurfaceColor(&caps, nullptr, &color) == mrhi_errorInvalid &&
              mrhiSuggestSurfaceColor(&caps, &hdr, nullptr) == mrhi_errorInvalid,
          "no NULL arguments");
}

int main(void)
{
    TestSources();
    TestRefusals();
    TestCaps();
    TestCompatibleSurface();
    TestInstanceEnds();
    TestSuggestion();
    return s_failures == 0 ? 0 : 1;
}
