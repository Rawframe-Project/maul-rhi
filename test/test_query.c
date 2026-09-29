// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Query sets on a test driver device: the def's checks, the timestamp
// feature, the device's state, and its two limits: sets, and runs of
// queries. Occlusion queries in render passes: the pass's set, one open
// query at a time, and each query written once a frame.

#include "device_core.h"
#include "test_device_setup.h"

#include "maul-rhi/encoder.h"

// A ready device holding at most sets query sets of queries in all, or
// an opening one; timestamps granted when asked.
static mrhiDevice* Open(uint32_t sets, uint32_t queries, bool timestamps, bool ready)
{
    s_adapter.features.timestampQuery = timestamps;
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.deviceLimits.querySets = sets;
    def.deviceLimits.queries = queries;
    def.features.timestampQuery = timestamps;
    return OpenWith(def, ready);
}

static mrhiQuerySetDef Def(mrhiQueryType type, uint32_t count)
{
    mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
    def.type = type;
    def.count = count;
    return def;
}

static void TestMakeAndDestroy(void)
{
    mrhiDevice* device = Open(4, 4097, true, true);
    mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
    CHECK(def.type == mrhi_queryOcclusion && def.count == 1, "one occlusion query");
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    CHECK(deviceDef.deviceLimits.querySets == 16 && deviceDef.deviceLimits.queries == 4096,
          "the device's defaults");
    mrhiQuerySetId a = {0};
    mrhiQuerySetId b = {0};
    CHECK(mrhiCreateQuerySet(device, &def, &a) == mrhi_success && a.index1 != 0, "a");
    def = Def(mrhi_queryTimestamp, 4096);
    CHECK(mrhiCreateQuerySet(device, &def, &b) == mrhi_success, "all the other queries");
    CHECK(mrhiDestroyQuerySet(device, a) == mrhi_success, "a destroyed");
    CHECK(mrhiDestroyQuerySet(device, a) == mrhi_errorStale, "its id has ended");
    CHECK(mrhiDestroyQuerySet(device, (mrhiQuerySetId){0, 0}) == mrhi_errorStale, "the null id");
    CHECK(mrhiDestroyQuerySet(nullptr, b) == mrhi_errorInvalid, "no device");
    CHECK(mrhiDestroyQuerySet(device, b) == mrhi_success, "b destroyed");
    CHECK(mrhiGetDeviceMisuse(device) == 0, "no misuse");
    Close(device);
}

static void TestInvalidDefs(void)
{
    mrhiDevice* device = Open(4, 8192, false, true);
    mrhiQuerySetId set;
    mrhiQuerySetDef def = Def(mrhi_queryOcclusion, 0);
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorInvalid, "no queries");
    def.count = 4097;
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorInvalid, "past 4096");
    def = Def(2, 1);
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorInvalid, "a type");
    def = mrhiDefaultQuerySetDef();
    def.cookie = 0;
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorInvalid, "no cookie");
    def = mrhiDefaultQuerySetDef();
    def.labelLength = 1;
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorInvalid, "a length, no label");
    def = mrhiDefaultQuerySetDef();
    CHECK(mrhiCreateQuerySet(device, nullptr, &set) == mrhi_errorInvalid, "no def");
    CHECK(mrhiCreateQuerySet(device, &def, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiCreateQuerySet(nullptr, &def, &set) == mrhi_errorInvalid, "no device");
    CHECK(mrhiGetDeviceMisuse(device) == 7, "each counted on the device");
    def = Def(mrhi_queryTimestamp, 1);
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorUnsupported,
          "timestamps without the feature");
    mrhiChain critical = {.next = nullptr, .type = 0x7000u};
    def = mrhiDefaultQuerySetDef();
    def.next = &critical;
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorUnsupported, "an extension");
    def.label = "occlusion";
    def.labelLength = 9;
    def.next = nullptr;
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_success, "a labeled set");
    CHECK(mrhiGetDeviceMisuse(device) == 7, "neither counted");
    Close(device);
}

static void TestStateAndLimits(void)
{
    mrhiDevice* device = Open(2, 8, false, false);
    mrhiQuerySetDef three = Def(mrhi_queryOcclusion, 3);
    mrhiQuerySetId a;
    mrhiQuerySetId b;
    mrhiQuerySetId c;
    CHECK(mrhiCreateQuerySet(device, &three, &a) == mrhi_errorState, "not ready yet");
    mrhiInstanceNotification record;
    CHECK(mrhiNextInstanceNotification(s_instance, &record) == mrhi_success, "ready");
    mrhiQuerySetDef nine = Def(mrhi_queryOcclusion, 9);
    CHECK(mrhiCreateQuerySet(device, &nine, &a) == mrhi_errorCapacity, "more than the device");
    CHECK(mrhiCreateQuerySet(device, &three, &a) == mrhi_success, "queries 0 to 2");
    CHECK(mrhiCreateQuerySet(device, &three, &b) == mrhi_success, "3 to 5");
    CHECK(mrhiCreateQuerySet(device, &three, &c) == mrhi_errorCapacity, "the set limit first");
    CHECK(mrhiDestroyQuerySet(device, a) == mrhi_success, "0 to 2 free");
    mrhiQuerySetDef four = Def(mrhi_queryOcclusion, 4);
    CHECK(mrhiCreateQuerySet(device, &four, &c) == mrhi_errorCapacity,
          "five free, but not four in a run");
    CHECK(mrhiCreateQuerySet(device, &three, &c) == mrhi_success, "three in the first run");
    CHECK(mrhiDestroyQuerySet(device, b) == mrhi_success, "3 to 7 free");
    mrhiQuerySetDef five = Def(mrhi_queryOcclusion, 5);
    CHECK(mrhiCreateQuerySet(device, &five, &b) == mrhi_success, "five after it");
    CHECK(mrhiDestroyQuerySet(device, c) == mrhi_success &&
              mrhiDestroyQuerySet(device, b) == mrhi_success,
          "all free");
    mrhiQuerySetDef eight = Def(mrhi_queryOcclusion, 8);
    CHECK(mrhiCreateQuerySet(device, &eight, &a) == mrhi_success, "all eight");
    mrhiQuerySetDef one = mrhiDefaultQuerySetDef();
    CHECK(mrhiCreateQuerySet(device, &one, &b) == mrhi_errorCapacity, "none left");
    CHECK(mrhiGetDeviceMisuse(device) == 0, "no misuse");
    Close(device);
}

// Runs are found whatever order the sets' slots hold them in: a slot
// earlier in the table may hold a run after the one that moves the
// search.
static void TestRunOrder(void)
{
    mrhiDevice* device = Open(4, 6, false, true);
    mrhiQuerySetDef two = Def(mrhi_queryOcclusion, 2);
    mrhiQuerySetId a;
    mrhiQuerySetId b;
    CHECK(mrhiCreateQuerySet(device, &two, &a) == mrhi_success &&
              mrhiCreateQuerySet(device, &two, &b) == mrhi_success &&
              mrhiDestroyQuerySet(device, a) == mrhi_success &&
              mrhiDestroyQuerySet(device, b) == mrhi_success,
          "two slots used and freed");
    // The second slot is taken first, for queries 0 and 1; the first then
    // takes 2 and 3.
    CHECK(mrhiCreateQuerySet(device, &two, &b) == mrhi_success &&
              mrhiCreateQuerySet(device, &two, &a) == mrhi_success && a.index1 < b.index1,
          "the later run in the earlier slot");
    mrhiQuerySetId c;
    CHECK(mrhiCreateQuerySet(device, &two, &c) == mrhi_success, "4 and 5");
    mrhiQuerySetDef one = mrhiDefaultQuerySetDef();
    CHECK(mrhiCreateQuerySet(device, &one, &c) == mrhi_errorCapacity, "none left");
    Close(device);
}

static void TestNoQueries(void)
{
    mrhiDevice* device = Open(0, 0, false, true);
    mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
    mrhiQuerySetId set;
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorCapacity, "a device without");
    Close(device);
    device = Open(1, 0, false, true);
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorCapacity, "sets but no queries");
    Close(device);
    device = Open(0, 1, false, true);
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorCapacity, "queries but no sets");
    Close(device);
}

static void TestDriverFailure(void)
{
    s_adapter.objectsBeforeFailure = 1;
    mrhiDevice* device = Open(2, 2, false, true);
    mrhiQuerySetDef def = mrhiDefaultQuerySetDef();
    mrhiQuerySetId a;
    mrhiQuerySetId b;
    CHECK(mrhiCreateQuerySet(device, &def, &a) == mrhi_success, "the one the driver makes");
    CHECK(mrhiCreateQuerySet(device, &def, &b) == mrhi_errorPlatform, "the driver fails");
    CHECK(mrhiDestroyQuerySet(device, a) == mrhi_success, "one freed");
    CHECK(mrhiCreateQuerySet(device, &def, &b) == mrhi_errorPlatform &&
              mrhiCreateQuerySet(device, &def, &b) == mrhi_errorPlatform,
          "and again, no slot lost");
    s_adapter.objectsBeforeFailure = 0;
    Close(device);
}

static mrhiDevice* s_device;
static mrhiTextureId s_target;
static mrhiPassId s_render;
static mrhiPassId s_compute;

// Opens a ready device with timestamps and a render target, its
// commands limited to bytes.
static void OpenFrames(uint32_t commandBytes)
{
    s_adapter.features.timestampQuery = true;
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.features.timestampQuery = true;
    def.deviceLimits.frameCommandBytes = commandBytes;
    s_device = OpenWith(def, true);
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = 16;
    textureDef.height = 16;
    textureDef.usage = mrhi_textureRenderTarget;
    CHECK(mrhiCreateTexture(s_device, &textureDef, &s_target) == mrhi_success, "a target");
}

static mrhiQuerySetId MakeSet(mrhiQueryType type, uint32_t count)
{
    mrhiQuerySetDef def = Def(type, count);
    mrhiQuerySetId set = {0};
    CHECK(mrhiCreateQuerySet(s_device, &def, &set) == mrhi_success, "a set");
    return set;
}

// A render pass def drawing into the target, naming a set.
static mrhiPassDef RenderDef(mrhiResourceId target, mrhiQuerySetId set)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.colorTargets[0] = (mrhiColorTarget){.resource = target, .load = mrhi_loadClear};
    def.colorTargetCount = 1;
    def.occlusionQuerySet = set;
    return def;
}

// Begins a frame and imports the target.
static mrhiResourceId BeginFrame(void)
{
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &frameDef) == mrhi_success, "begun");
    mrhiResourceId target = {0};
    CHECK(mrhiImportTexture(s_device, s_target, &target) == mrhi_success, "imported");
    return target;
}

// Opens a frame of a render pass naming a set and a compute pass,
// compiled and begun.
static void Frame(mrhiQuerySetId set)
{
    mrhiPassDef def = RenderDef(BeginFrame(), set);
    CHECK(mrhiAddPass(s_device, &def, &s_render) == mrhi_success, "the render pass");
    def = mrhiDefaultPassDef();
    def.neverCull = true;
    CHECK(mrhiAddPass(s_device, &def, &s_compute) == mrhi_success, "the compute pass");
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    CHECK(mrhiBeginPass(s_device, s_render) == mrhi_success &&
              mrhiBeginPass(s_device, s_compute) == mrhi_success,
          "begun");
}

static void Drop(void)
{
    CHECK(mrhiDropFrame(s_device) == mrhi_success, "dropped");
}

// The n-th record of a pass, walking its chunks: or NULL past them.
static const mrhiCommand* Nth(mrhiPassId id, uint32_t n)
{
    const mrhiFramePass* pass = &s_device->framePasses[id.index1 - 1];
    for (uint32_t chunk = pass->firstChunk; chunk != 0;
         chunk = s_device->frameChunks[chunk - 1].next)
    {
        const mrhiCommandChunk* at = &s_device->frameChunks[chunk - 1];
        if (n < at->count)
        {
            return &at->commands[n];
        }
        n -= at->count;
    }
    return nullptr;
}

static void TestPassSets(void)
{
    OpenFrames(1u << 20);
    mrhiQuerySetId occlusion = MakeSet(mrhi_queryOcclusion, 4);
    mrhiQuerySetId timestamps = MakeSet(mrhi_queryTimestamp, 4);
    mrhiQuerySetId gone = MakeSet(mrhi_queryOcclusion, 4);
    CHECK(mrhiDestroyQuerySet(s_device, gone) == mrhi_success, "one destroyed");
    mrhiResourceId target = BeginFrame();
    mrhiPassId pass;
    mrhiPassDef def = RenderDef(target, timestamps);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "a timestamp set");
    def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.occlusionQuerySet = occlusion;
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "a pass without targets");
    CHECK(mrhiGetDeviceMisuse(s_device) == 2, "each counted");
    def = RenderDef(target, gone);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorStale, "a destroyed set");
    def.depthTarget.resource = target;
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorStale, "stale before the targets");
    def = RenderDef(target, occlusion);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success, "an occlusion set");
    CHECK(s_device->framePasses[pass.index1 - 1].occlusionSet == occlusion.index1 &&
              s_device->framePasses[pass.index1 - 1].occlusionGeneration == occlusion.generation,
          "kept by the pass");
    CHECK(mrhiGetDeviceMisuse(s_device) == 2, "the stale ones not counted");
    Drop();
    Close(s_device);
}

static void TestOcclusion(void)
{
    OpenFrames(1u << 20);
    mrhiQuerySetId set = MakeSet(mrhi_queryOcclusion, 4);
    Frame(set);
    CHECK(mrhiBeginOcclusionQuery(s_device, s_render, 2) == mrhi_success, "begun");
    const mrhiCommand* command = Nth(s_render, 0);
    CHECK(command != nullptr && command->type == mrhiCommandBeginOcclusionQuery &&
              command->a == s_device->querySetSlots[set.index1 - 1].handle && command->b == 2,
          "recorded with the set's handle");
    CHECK(mrhiBeginOcclusionQuery(s_device, s_render, 3) == mrhi_errorInvalid, "one open");
    CHECK(mrhiEndPass(s_device, s_render) == mrhi_errorInvalid, "no end while open");
    CHECK(mrhiEndOcclusionQuery(s_device, s_render) == mrhi_success, "ended");
    command = Nth(s_render, 1);
    CHECK(command != nullptr && command->type == mrhiCommandEndOcclusionQuery, "recorded");
    CHECK(mrhiEndOcclusionQuery(s_device, s_render) == mrhi_errorInvalid, "none open");
    CHECK(mrhiBeginOcclusionQuery(s_device, s_render, 2) == mrhi_errorInvalid,
          "written this frame");
    CHECK(mrhiBeginOcclusionQuery(s_device, s_render, 4) == mrhi_errorInvalid, "past the set");
    CHECK(mrhiBeginOcclusionQuery(s_device, s_compute, 0) == mrhi_errorInvalid &&
              mrhiEndOcclusionQuery(s_device, s_compute) == mrhi_errorInvalid,
          "a pass without a set");
    CHECK(mrhiGetDeviceMisuse(s_device) == 7, "each counted");
    CHECK(mrhiBeginOcclusionQuery(s_device, s_render, 3) == mrhi_success &&
              mrhiEndOcclusionQuery(s_device, s_render) == mrhi_success,
          "another query");
    CHECK(Nth(s_render, 4) == nullptr, "the refused ones recorded nothing");
    CHECK(mrhiBeginOcclusionQuery(nullptr, s_render, 0) == mrhi_errorInvalid &&
              mrhiEndOcclusionQuery(nullptr, s_render) == mrhi_errorInvalid,
          "no device");
    CHECK(mrhiEndPass(s_device, s_render) == mrhi_success, "the pass ended");
    CHECK(mrhiBeginOcclusionQuery(s_device, s_render, 0) == mrhi_errorState &&
              mrhiEndOcclusionQuery(s_device, s_render) == mrhi_errorState,
          "not recording");
    Drop();
    Frame(set);
    CHECK(mrhiBeginOcclusionQuery(s_device, s_render, 2) == mrhi_success &&
              mrhiEndOcclusionQuery(s_device, s_render) == mrhi_success,
          "written again in the next frame");
    CHECK(mrhiDestroyQuerySet(s_device, set) == mrhi_success, "destroyed while recording");
    CHECK(mrhiBeginOcclusionQuery(s_device, s_render, 0) == mrhi_errorStale, "stale");
    CHECK(mrhiGetDeviceMisuse(s_device) == 7, "neither counted");
    Drop();
    Close(s_device);
}

// Each set's queries have their own marks: two sets' first queries are
// written in one frame.
static void TestSetsApart(void)
{
    OpenFrames(1u << 20);
    mrhiQuerySetId first = MakeSet(mrhi_queryOcclusion, 4);
    mrhiQuerySetId second = MakeSet(mrhi_queryOcclusion, 4);
    mrhiResourceId target = BeginFrame();
    mrhiPassDef def = RenderDef(target, first);
    mrhiPassId a;
    mrhiPassId b;
    CHECK(mrhiAddPass(s_device, &def, &a) == mrhi_success, "a pass on the first set");
    def.occlusionQuerySet = second;
    CHECK(mrhiAddPass(s_device, &def, &b) == mrhi_success, "one on the second");
    CHECK(mrhiCompileFrame(s_device) == mrhi_success &&
              mrhiBeginPass(s_device, a) == mrhi_success &&
              mrhiBeginPass(s_device, b) == mrhi_success,
          "begun");
    CHECK(mrhiBeginOcclusionQuery(s_device, b, 0) == mrhi_success &&
              mrhiBeginOcclusionQuery(s_device, a, 0) == mrhi_success,
          "query 0 of each");
    CHECK(mrhiEndOcclusionQuery(s_device, a) == mrhi_success &&
              mrhiEndOcclusionQuery(s_device, b) == mrhi_success,
          "ended");
    Drop();
    Close(s_device);
}

// A query refused for capacity still opens and closes, so the pass ends.
static void TestOcclusionCapacity(void)
{
    OpenFrames(MRHI_CHUNK_BYTES);
    mrhiQuerySetId set = MakeSet(mrhi_queryOcclusion, 128);
    Frame(set);
    for (uint32_t i = 0; i < 63; ++i)
    {
        CHECK(mrhiBeginOcclusionQuery(s_device, s_render, i) == mrhi_success &&
                  mrhiEndOcclusionQuery(s_device, s_render) == mrhi_success,
              "fits");
    }
    CHECK(mrhiBeginOcclusionQuery(s_device, s_render, 63) == mrhi_success, "the last record");
    CHECK(mrhiEndOcclusionQuery(s_device, s_render) == mrhi_errorCapacity, "full");
    CHECK(mrhiBeginOcclusionQuery(s_device, s_render, 64) == mrhi_errorCapacity &&
              mrhiEndOcclusionQuery(s_device, s_render) == mrhi_errorCapacity,
          "still full");
    CHECK(mrhiEndPass(s_device, s_render) == mrhi_success, "the pass ends");
    CHECK(mrhiGetDeviceMisuse(s_device) == 0, "no misuse");
    Drop();
    Close(s_device);
}

int main(void)
{
    ResetAdapter();
    TestMakeAndDestroy();
    TestInvalidDefs();
    TestStateAndLimits();
    TestRunOrder();
    TestNoQueries();
    TestDriverFailure();
    TestPassSets();
    TestOcclusion();
    TestSetsApart();
    TestOcclusionCapacity();
    return s_failures == 0 ? 0 : 1;
}
