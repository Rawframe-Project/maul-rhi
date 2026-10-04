// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Query sets on a test driver device: the def's checks, the timestamp
// and statistics features, the device's state, and its two limits:
// sets, and runs of queries. Occlusion queries in render passes: the
// pass's set, one open query at a time, and each query written once a
// frame. Statistics queries (mrhi-0023) in passes of the graphics class,
// beside them.

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
    def = Def(3, 1);
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
    def = Def(mrhi_queryPipelineStatistics, 1);
    CHECK(mrhiCreateQuerySet(device, &def, &set) == mrhi_errorUnsupported,
          "statistics without theirs");
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
static mrhiBufferId s_results;
static mrhiResourceId s_r;
static mrhiPassId s_render;
static mrhiPassId s_compute;

// Opens a ready device with timestamps, statistics, two views and a
// render target, its commands limited to bytes.
static void OpenFrames(uint32_t commandBytes)
{
    s_adapter.features.timestampQuery = true;
    s_adapter.features.pipelineStatisticsQuery = true;
    s_adapter.features.multiview = true;
    s_adapter.limits.multiviewViews = 2;
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.features.timestampQuery = true;
    def.features.pipelineStatisticsQuery = true;
    def.features.multiview = true;
    def.limits.multiviewViews = 2;
    def.deviceLimits.frameCommandBytes = commandBytes;
    s_device = OpenWith(def, true);
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = 16;
    textureDef.height = 16;
    textureDef.usage = mrhi_textureRenderTarget;
    CHECK(mrhiCreateTexture(s_device, &textureDef, &s_target) == mrhi_success, "a target");
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 1024;
    bufferDef.usage = mrhi_bufferQueryResolve | mrhi_bufferCopySource;
    CHECK(mrhiCreateBuffer(s_device, &bufferDef, &s_results) == mrhi_success, "a results buffer");
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

// Begins a frame and imports the target and the results buffer.
static mrhiResourceId BeginFrame(void)
{
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiBeginFrame(s_device, &frameDef) == mrhi_success, "begun");
    mrhiResourceId target = {0};
    CHECK(mrhiImportTexture(s_device, s_target, &target) == mrhi_success &&
              mrhiImportBuffer(s_device, s_results, &s_r) == mrhi_success,
          "imported");
    return target;
}

// Opens a frame of a render pass naming a set and a graphics pass without
// targets resolving into the results buffer, compiled and begun.
static void Frame(mrhiQuerySetId set)
{
    mrhiPassDef def = RenderDef(BeginFrame(), set);
    CHECK(mrhiAddPass(s_device, &def, &s_render) == mrhi_success, "the render pass");
    mrhiAccess results = {.resource = s_r, .kind = mrhi_accessQueryResolve};
    def = mrhiDefaultPassDef();
    def.accesses = &results;
    def.accessCount = 1;
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
              command->b == s_device->querySetSlots[set.index1 - 1].handle && command->a == 2,
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

// A pass def writing timestamps of a set at its start and end.
static mrhiPassDef TimedDef(mrhiQuerySetId set, uint32_t begin, uint32_t end)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.timestampQuerySet = set;
    def.timestampBegin = begin;
    def.timestampEnd = end;
    return def;
}

static void TestTimestamps(void)
{
    OpenFrames(1u << 20);
    // The timestamps' run starts past the occlusion set's.
    mrhiQuerySetId occlusion = MakeSet(mrhi_queryOcclusion, 8);
    mrhiQuerySetId set = MakeSet(mrhi_queryTimestamp, 8);
    mrhiQuerySetId gone = MakeSet(mrhi_queryTimestamp, 8);
    CHECK(mrhiDestroyQuerySet(s_device, gone) == mrhi_success, "one destroyed");
    mrhiPassDef def = mrhiDefaultPassDef();
    CHECK(def.timestampQuerySet.index1 == 0 && def.timestampBegin == MRHI_NO_QUERY &&
              def.timestampEnd == MRHI_NO_QUERY,
          "none by default");
    mrhiResourceId target = BeginFrame();
    mrhiPassId pass;
    def = TimedDef(set, 0, 1);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success, "both ends");
    const mrhiFramePass* kept = &s_device->framePasses[pass.index1 - 1];
    CHECK(kept->timestampSet == s_device->querySetSlots[set.index1 - 1].handle &&
              kept->timestampBegin == 0 && kept->timestampEnd == 1,
          "kept with the set's handle");
    def = TimedDef(set, 2, MRHI_NO_QUERY);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success, "its start only");
    def = TimedDef(set, MRHI_NO_QUERY, 3);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success, "its end only");
    def = RenderDef(target, occlusion);
    def.timestampQuerySet = set;
    def.timestampBegin = 4;
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success, "a render pass with both sets");
    uint32_t misuse = 0;
    def = TimedDef(set, 0, MRHI_NO_QUERY);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "a start written");
    def = TimedDef(set, MRHI_NO_QUERY, 1);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "an end written");
    def = TimedDef(set, 5, 5);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "the same query");
    def = TimedDef(set, MRHI_NO_QUERY, MRHI_NO_QUERY);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "neither");
    def = TimedDef(set, 8, MRHI_NO_QUERY);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "a start past the set");
    def = TimedDef(set, 5, 8);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "an end past it");
    def = TimedDef(occlusion, 5, 6);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "an occlusion set");
    def = TimedDef(set, 5, 6);
    def.passClass = mrhi_passAsyncCompute;
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "async compute");
    def.passClass = mrhi_passTransfer;
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "a transfer pass");
    def = TimedDef((mrhiQuerySetId){0}, 5, MRHI_NO_QUERY);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "a start without a set");
    def = TimedDef((mrhiQuerySetId){0}, MRHI_NO_QUERY, 5);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "an end without a set");
    misuse += 11;
    def = TimedDef(set, 6, 0);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "a start with a written end");
    def = TimedDef(set, 7, 1);
    def.occlusionQuerySet = occlusion;
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorInvalid, "an occlusion set refused");
    misuse += 2;
    CHECK(mrhiGetDeviceMisuse(s_device) == misuse, "each counted");
    def = TimedDef(gone, 5, 6);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_errorStale, "a destroyed set");
    def = TimedDef(set, 6, 7);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success,
          "queries a refused pass named, unwritten");
    CHECK(mrhiGetDeviceMisuse(s_device) == misuse, "the stale one not counted");
    Drop();
    BeginFrame();
    def = TimedDef(set, 0, 1);
    CHECK(mrhiAddPass(s_device, &def, &pass) == mrhi_success, "written again in the next frame");
    Drop();
    Close(s_device);
}

static void TestPeriod(void)
{
    s_adapter.timestampPeriod = 83.333;
    OpenFrames(1u << 20);
    double period = 0.0;
    CHECK(mrhiGetDeviceTimestampPeriod(s_device, &period) == mrhi_success && period == 83.333,
          "the driver's period");
    CHECK(mrhiGetDeviceTimestampPeriod(s_device, nullptr) == mrhi_errorInvalid &&
              mrhiGetDeviceMisuse(s_device) == 1,
          "no out, counted");
    CHECK(mrhiGetDeviceTimestampPeriod(nullptr, &period) == mrhi_errorInvalid, "no device");
    Close(s_device);
    s_adapter.timestampPeriod = 0.0;
    OpenFrames(1u << 20);
    CHECK(mrhiGetDeviceTimestampPeriod(s_device, &period) == mrhi_success && period == 1.0,
          "nanoseconds when the driver says none");
    Close(s_device);
    mrhiDevice* device = Open(1, 1, false, true);
    CHECK(mrhiGetDeviceTimestampPeriod(device, &period) == mrhi_errorUnsupported,
          "a device without timestamps");
    Close(device);
    device = Open(1, 1, true, false);
    CHECK(mrhiGetDeviceTimestampPeriod(device, &period) == mrhi_errorState, "not ready yet");
    Close(device);
}

static void TestResolveAccess(void)
{
    OpenFrames(1u << 20);
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 256;
    bufferDef.usage = mrhi_bufferCopyDestination;
    mrhiBufferId plain;
    CHECK(mrhiCreateBuffer(s_device, &bufferDef, &plain) == mrhi_success, "a copy buffer");
    mrhiResourceId target = BeginFrame();
    mrhiResourceId imported = {0};
    mrhiResourceId declared = {0};
    bufferDef.usage = 0;
    CHECK(mrhiImportBuffer(s_device, plain, &imported) == mrhi_success &&
              mrhiDeclareBuffer(s_device, &bufferDef, &declared) == mrhi_success,
          "buffers");
    mrhiAccess access = {.resource = declared, .kind = mrhi_accessQueryResolve};
    mrhiPassDef def = mrhiDefaultPassDef();
    def.accesses = &access;
    def.accessCount = 1;
    mrhiPassId resolving;
    CHECK(mrhiAddPass(s_device, &def, &resolving) == mrhi_success, "a transient written");
    access.kind = mrhi_accessCopySource;
    def.neverCull = true;
    mrhiPassId reading;
    CHECK(mrhiAddPass(s_device, &def, &reading) == mrhi_success, "and read");
    access.kind = mrhi_accessQueryResolve;
    def.passClass = mrhi_passAsyncCompute;
    mrhiPassId refused;
    CHECK(mrhiAddPass(s_device, &def, &refused) == mrhi_errorInvalid, "async compute");
    def.passClass = mrhi_passTransfer;
    CHECK(mrhiAddPass(s_device, &def, &refused) == mrhi_errorInvalid, "a transfer pass");
    def.passClass = mrhi_passGraphics;
    access.resource = target;
    CHECK(mrhiAddPass(s_device, &def, &refused) == mrhi_errorInvalid, "a texture");
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = 4;
    textureDef.height = 4;
    CHECK(mrhiDeclareTexture(s_device, &textureDef, &access.resource) == mrhi_success,
          "a transient texture");
    access.range = (mrhiTextureRange){.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING};
    CHECK(mrhiAddPass(s_device, &def, &refused) == mrhi_errorInvalid, "a transient texture");
    access.resource = imported;
    CHECK(mrhiAddPass(s_device, &def, &refused) == mrhi_errorInvalid, "a buffer made without it");
    access.kind = 11;
    access.resource = declared;
    CHECK(mrhiAddPass(s_device, &def, &refused) == mrhi_errorInvalid, "a kind past it");
    CHECK(mrhiGetDeviceMisuse(s_device) == 6, "each counted");
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    mrhiResourcePlan plan;
    CHECK(mrhiGetResourcePlan(s_device, declared, &plan) == mrhi_success &&
              plan.usage == (mrhi_bufferQueryResolve | mrhi_bufferCopySource),
          "the transient's usage");
    mrhiBarrier barriers[8];
    size_t count = 0;
    CHECK(mrhiGetFrameBarriers(s_device, barriers, 8, &count) == mrhi_success, "barriers");
    bool found = false;
    for (size_t i = 0; i < count; ++i)
    {
        found = found || (barriers[i].resource.index1 == declared.index1 &&
                          barriers[i].before == mrhi_stateQueryResolve &&
                          barriers[i].after == mrhi_stateCopySource);
    }
    CHECK(found, "a barrier from the resolve to the read");
    Drop();
    Close(s_device);
}

static void TestResolve(void)
{
    OpenFrames(1u << 20);
    mrhiQuerySetId other = MakeSet(mrhi_queryOcclusion, 8);
    mrhiQuerySetId set = MakeSet(mrhi_queryOcclusion, 64);
    Frame(set);
    CHECK(mrhiResolveQueries(s_device, s_compute, set, 0, 64, s_r, 0) == mrhi_success,
          "every query");
    const mrhiCommand* command = Nth(s_compute, 0);
    CHECK(command != nullptr && command->type == mrhiCommandResolveQueries &&
              command->b == s_device->querySetSlots[set.index1 - 1].handle &&
              command->c == 0 + (64ull << 32) && command->a == s_r.index1 && command->d == 0,
          "recorded");
    CHECK(mrhiResolveQueries(s_device, s_compute, set, 63, 1, s_r, 768) == mrhi_success,
          "the last into the last 256 bytes");
    command = Nth(s_compute, 1);
    CHECK(command != nullptr && command->c == 63 + (1ull << 32) && command->d == 768,
          "recorded with its first query and offset");
    CHECK(mrhiResolveQueries(s_device, s_compute, set, 0, 32, s_r, 768) == mrhi_success &&
              mrhiResolveQueries(s_device, s_compute, set, 0, 0, s_r, 1024) == mrhi_success,
          "the buffer's end");
    CHECK(mrhiResolveQueries(s_device, s_compute, other, 0, 8, s_r, 0) == mrhi_success,
          "another set");
    uint32_t misuse = 0;
    CHECK(mrhiResolveQueries(s_device, s_compute, set, 0, 33, s_r, 768) == mrhi_errorInvalid,
          "a value past the buffer");
    CHECK(mrhiResolveQueries(s_device, s_compute, set, 0, 0, s_r, 1280) == mrhi_errorInvalid,
          "an offset past it");
    CHECK(mrhiResolveQueries(s_device, s_compute, set, 0, 1, s_r, 128) == mrhi_errorInvalid,
          "an offset off 256");
    CHECK(mrhiResolveQueries(s_device, s_compute, set, 0, 65, s_r, 0) == mrhi_errorInvalid &&
              mrhiResolveQueries(s_device, s_compute, set, 60, 5, s_r, 0) == mrhi_errorInvalid &&
              mrhiResolveQueries(s_device, s_compute, set, 1, UINT32_MAX, s_r, 0) ==
                  mrhi_errorInvalid,
          "queries past the set");
    CHECK(mrhiResolveQueries(s_device, s_compute, set, 64, 0, s_r, 0) == mrhi_errorInvalid,
          "a first query at its end");
    CHECK(mrhiResolveQueries(s_device, s_render, set, 0, 1, s_r, 0) == mrhi_errorInvalid,
          "a pass with targets");
    misuse += 8;
    CHECK(mrhiGetDeviceMisuse(s_device) == misuse, "each counted");
    CHECK(mrhiDestroyQuerySet(s_device, other) == mrhi_success &&
              mrhiResolveQueries(s_device, s_compute, other, 0, 1, s_r, 0) == mrhi_errorStale,
          "a destroyed set");
    mrhiResourceId none = {0};
    CHECK(mrhiResolveQueries(s_device, s_compute, set, 0, 1, none, 0) == mrhi_errorStale,
          "no resource");
    CHECK(mrhiResolveQueries(nullptr, s_compute, set, 0, 1, s_r, 0) == mrhi_errorInvalid,
          "no device");
    CHECK(mrhiGetDeviceMisuse(s_device) == misuse, "none counted");
    CHECK(mrhiEndPass(s_device, s_compute) == mrhi_success &&
              mrhiResolveQueries(s_device, s_compute, set, 0, 1, s_r, 0) == mrhi_errorState,
          "not recording");
    Drop();
    // A pass that does not declare the buffer, one of async compute, and
    // a render pass that does.
    mrhiResourceId target = BeginFrame();
    mrhiAccess resolved = {.resource = s_r, .kind = mrhi_accessQueryResolve};
    mrhiPassDef def = RenderDef(target, (mrhiQuerySetId){0});
    def.accesses = &resolved;
    def.accessCount = 1;
    mrhiPassId drawing;
    CHECK(mrhiAddPass(s_device, &def, &drawing) == mrhi_success, "a render pass declaring it");
    def = mrhiDefaultPassDef();
    def.neverCull = true;
    mrhiPassId bare;
    CHECK(mrhiAddPass(s_device, &def, &bare) == mrhi_success, "a pass declaring nothing");
    mrhiAccess copy = {.resource = s_r, .kind = mrhi_accessCopySource};
    def.accesses = &copy;
    def.accessCount = 1;
    mrhiPassId copying;
    CHECK(mrhiAddPass(s_device, &def, &copying) == mrhi_success, "one reading it");
    def.passClass = mrhi_passAsyncCompute;
    def.accessCount = 0;
    mrhiPassId async;
    CHECK(mrhiAddPass(s_device, &def, &async) == mrhi_success, "an async compute pass");
    CHECK(mrhiCompileFrame(s_device) == mrhi_success &&
              mrhiBeginPass(s_device, bare) == mrhi_success &&
              mrhiBeginPass(s_device, drawing) == mrhi_success &&
              mrhiBeginPass(s_device, copying) == mrhi_success &&
              mrhiBeginPass(s_device, async) == mrhi_success,
          "begun");
    CHECK(mrhiResolveQueries(s_device, bare, set, 0, 0, s_r, 0) == mrhi_errorInvalid &&
              mrhiResolveQueries(s_device, copying, set, 0, 1, s_r, 0) == mrhi_errorInvalid,
          "not declared for resolves");
    CHECK(mrhiResolveQueries(s_device, async, set, 0, 1, s_r, 0) == mrhi_errorInvalid,
          "async compute");
    CHECK(mrhiResolveQueries(s_device, drawing, set, 0, 0, s_r, 0) == mrhi_errorInvalid,
          "a render pass declaring it");
    CHECK(mrhiGetDeviceMisuse(s_device) == misuse + 4, "each counted");
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
    CHECK(mrhiResolveQueries(s_device, s_compute, set, 0, 1, s_r, 0) == mrhi_errorCapacity,
          "a resolve too");
    CHECK(mrhiGetDeviceMisuse(s_device) == 0, "no misuse");
    Drop();
    Close(s_device);
}

// In a render pass beside an occlusion query and in a pass without
// targets, one open at a time, each query written once a frame; a
// resolve writes 88 bytes a query.
static void TestStatistics(void)
{
    OpenFrames(1u << 20);
    mrhiQuerySetId occlusion = MakeSet(mrhi_queryOcclusion, 4);
    mrhiQuerySetId set = MakeSet(mrhi_queryPipelineStatistics, 4);
    uint64_t handle = s_device->querySetSlots[set.index1 - 1].handle;
    Frame(occlusion);
    CHECK(mrhiBeginStatisticsQuery(s_device, s_render, set, 1) == mrhi_success &&
              mrhiBeginOcclusionQuery(s_device, s_render, 0) == mrhi_success,
          "begun beside an occlusion query");
    const mrhiCommand* command = Nth(s_render, 0);
    CHECK(command != nullptr && command->type == mrhiCommandBeginStatisticsQuery &&
              command->a == 1 && command->b == handle,
          "recorded with the set's handle");
    uint32_t misuse = 0;
    CHECK(mrhiBeginStatisticsQuery(s_device, s_render, set, 2) == mrhi_errorInvalid, "one open");
    CHECK(mrhiEndOcclusionQuery(s_device, s_render) == mrhi_success &&
              mrhiEndPass(s_device, s_render) == mrhi_errorInvalid,
          "no end while open");
    misuse += 2;
    CHECK(mrhiEndStatisticsQuery(s_device, s_render) == mrhi_success, "ended");
    command = Nth(s_render, 3);
    CHECK(command != nullptr && command->type == mrhiCommandEndStatisticsQuery && command->a == 1 &&
              command->b == handle,
          "recorded with its query");
    CHECK(mrhiEndStatisticsQuery(s_device, s_render) == mrhi_errorInvalid, "none open");
    CHECK(mrhiBeginStatisticsQuery(s_device, s_compute, set, 1) == mrhi_errorInvalid,
          "written this frame");
    CHECK(mrhiBeginStatisticsQuery(s_device, s_compute, set, 4) == mrhi_errorInvalid,
          "past the set");
    CHECK(mrhiBeginStatisticsQuery(s_device, s_compute, occlusion, 3) == mrhi_errorInvalid,
          "an occlusion set");
    misuse += 4;
    CHECK(mrhiGetDeviceMisuse(s_device) == misuse, "each counted");
    CHECK(Nth(s_render, 4) == nullptr, "the refused ones recorded nothing");
    CHECK(mrhiBeginStatisticsQuery(s_device, s_compute, set, 0) == mrhi_success &&
              mrhiEndStatisticsQuery(s_device, s_compute) == mrhi_success,
          "in a pass without targets");
    CHECK(mrhiEndPass(s_device, s_render) == mrhi_success, "the render pass ended");
    CHECK(mrhiBeginStatisticsQuery(s_device, s_render, set, 2) == mrhi_errorState &&
              mrhiEndStatisticsQuery(s_device, s_render) == mrhi_errorState,
          "not recording");
    CHECK(mrhiBeginStatisticsQuery(nullptr, s_compute, set, 2) == mrhi_errorInvalid &&
              mrhiEndStatisticsQuery(nullptr, s_compute) == mrhi_errorInvalid,
          "no device");
    CHECK(mrhiResolveQueries(s_device, s_compute, set, 0, 4, s_r, 0) == mrhi_success &&
              mrhiResolveQueries(s_device, s_compute, set, 2, 2, s_r, 768) == mrhi_success,
          "88 bytes a query");
    CHECK(mrhiResolveQueries(s_device, s_compute, set, 1, 3, s_r, 768) == mrhi_errorInvalid,
          "not three in the last 256 bytes");
    misuse += 1;
    CHECK(mrhiGetDeviceMisuse(s_device) == misuse, "counted");
    Drop();
    Frame(occlusion);
    CHECK(mrhiBeginStatisticsQuery(s_device, s_render, set, 1) == mrhi_success &&
              mrhiEndStatisticsQuery(s_device, s_render) == mrhi_success,
          "written again in the next frame");
    CHECK(mrhiDestroyQuerySet(s_device, set) == mrhi_success &&
              mrhiBeginStatisticsQuery(s_device, s_compute, set, 0) == mrhi_errorStale,
          "a destroyed set");
    CHECK(mrhiGetDeviceMisuse(s_device) == misuse, "not counted");
    Drop();
    Close(s_device);
}

// Not in a pass of async compute, of transfers, or of several views.
static void TestStatisticsPasses(void)
{
    OpenFrames(1u << 20);
    mrhiQuerySetId set = MakeSet(mrhi_queryPipelineStatistics, 4);
    BeginFrame();
    mrhiTextureDef layers = mrhiDefaultTextureDef();
    layers.kind = mrhi_texture2dArray;
    layers.format = mrhi_formatRgba8Unorm;
    layers.width = 16;
    layers.height = 16;
    layers.depthOrLayers = 2;
    mrhiResourceId target = {0};
    CHECK(mrhiDeclareTexture(s_device, &layers, &target) == mrhi_success, "two layers");
    mrhiPassDef def = RenderDef(target, (mrhiQuerySetId){0});
    def.viewCount = 2;
    def.neverCull = true;
    mrhiPassId passes[3];
    CHECK(mrhiAddPass(s_device, &def, &passes[0]) == mrhi_success, "two views");
    def = mrhiDefaultPassDef();
    def.neverCull = true;
    def.passClass = mrhi_passAsyncCompute;
    CHECK(mrhiAddPass(s_device, &def, &passes[1]) == mrhi_success, "async compute");
    def.passClass = mrhi_passTransfer;
    CHECK(mrhiAddPass(s_device, &def, &passes[2]) == mrhi_success, "transfers");
    CHECK(mrhiCompileFrame(s_device) == mrhi_success, "compiled");
    for (uint32_t i = 0; i < 3; ++i)
    {
        CHECK(mrhiBeginPass(s_device, passes[i]) == mrhi_success &&
                  mrhiBeginStatisticsQuery(s_device, passes[i], set, i) == mrhi_errorInvalid,
              "refused");
    }
    CHECK(mrhiGetDeviceMisuse(s_device) == 3, "each counted");
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
    TestTimestamps();
    TestPeriod();
    TestResolveAccess();
    TestResolve();
    TestStatistics();
    TestStatisticsPasses();
    return s_failures == 0 ? 0 : 1;
}
