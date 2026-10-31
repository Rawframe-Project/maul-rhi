// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A driver's answers the core keeps in range on every build, with or
// without the validation layer: the test driver handed in from outside
// with one answer broken at a time. A poll that claims more events than
// the room it was given, a pipeline cache too large for its envelope, a
// memory alignment that would place resources over each other, a
// refused submission naming a code that is not a budget, and an adapter
// of an unknown kind or with a name that is too long or not UTF-8.

#include "driver_test.h"
#include "test_harness.h"

#include "maul-rhi/device.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/instance.h"

#include <string.h>

static mrhiTestAdapter s_adapter;
// What the driver lists its adapter as.
static mrhiAdapterInfo s_info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete};
static mrhiInstanceDriver s_made;
static mrhiInstanceDriverVtable s_vtable;
static const mrhiDeviceDriverVtable* s_deviceInner;
static mrhiDeviceDriverVtable s_deviceVtable;

// Set to make the next poll claim more events than its room, once:
// SIZE_MAX of them when s_claimAll is set, one past the room otherwise.
static bool s_claiming;
static bool s_claimAll;
// The cache size the driver gives when asked for it; 0 for its own.
static size_t s_cacheBytes;
// Whether the driver gives s_alignment for a buffer's memory, and
// whether it keeps a texture on chip: 0 bytes at an alignment of 0.
static bool s_giveAlignment;
static uint64_t s_alignment;
static bool s_onChip;
// The code a refused submission names; 0 to submit.
static mrhiDiagnosticCode s_budget;

// The claim: the room filled with answers to a tag nothing asked, the
// driver's own events left for the next poll, and a count past the room.
static size_t Claim(mrhiDriverEvent* events, size_t capacity, uint64_t unasked)
{
    s_claiming = false;
    for (size_t i = 0; i < capacity; ++i)
    {
        events[i] = (mrhiDriverEvent){unasked, mrhi_success};
    }
    return s_claimAll ? SIZE_MAX : capacity + 1;
}

static size_t PollInstance(void* self, mrhiDriverEvent* events, size_t capacity)
{
    return s_claiming ? Claim(events, capacity, UINT64_MAX)
                      : s_made.vtable->poll(self, events, capacity);
}

// Device tags at most UINT32_MAX are frames; none has that token.
static size_t PollDevice(void* self, mrhiDriverEvent* events, size_t capacity)
{
    return s_claiming ? Claim(events, capacity, UINT32_MAX)
                      : s_deviceInner->poll(self, events, capacity);
}

static size_t ExportCache(void* self, void* bytes, size_t capacity)
{
    if (s_cacheBytes != 0 && bytes == nullptr)
    {
        return s_cacheBytes;
    }
    return s_deviceInner->exportPipelineCache(self, bytes, capacity);
}

static void BufferMemory(const void* self, const mrhiBufferDef* def, uint64_t* bytesOut,
                         uint64_t* alignmentOut)
{
    s_deviceInner->bufferMemory(self, def, bytesOut, alignmentOut);
    if (s_giveAlignment)
    {
        *alignmentOut = s_alignment;
    }
}

static void TextureMemory(const void* self, const mrhiTextureDef* def, uint64_t* bytesOut,
                          uint64_t* alignmentOut)
{
    s_deviceInner->textureMemory(self, def, bytesOut, alignmentOut);
    if (s_onChip)
    {
        *bytesOut = 0;
        *alignmentOut = 0;
    }
}

static mrhiResult SubmitFrame(void* self, const mrhiDriverFrame* frame, uint64_t tag,
                              mrhiDiagnosticCode* budgetOut)
{
    if (s_budget != 0)
    {
        *budgetOut = s_budget;
        return mrhi_errorCapacity;
    }
    return s_deviceInner->submitFrame(self, frame, tag, budgetOut);
}

static mrhiResult CreateDevice(void* self, uint64_t adapter, const mrhiDeviceDef* def, uint64_t tag,
                               mrhiDeviceDriver* deviceOut)
{
    mrhiResult status = s_made.vtable->createDevice(self, adapter, def, tag, deviceOut);
    if (status == mrhi_success)
    {
        s_deviceInner = deviceOut->vtable;
        s_deviceVtable = *deviceOut->vtable;
        s_deviceVtable.poll = PollDevice;
        s_deviceVtable.exportPipelineCache = ExportCache;
        s_deviceVtable.bufferMemory = BufferMemory;
        s_deviceVtable.textureMemory = TextureMemory;
        s_deviceVtable.submitFrame = SubmitFrame;
        deviceOut->vtable = &s_deviceVtable;
    }
    return status;
}

// An instance on the test driver as an outside driver's code makes it,
// its poll and devices changed.
static mrhiInstance* Start(void)
{
    s_adapter = (mrhiTestAdapter){
        .info = s_info,
        .limits = mrhiDefaultLimits(),
    };
    mrhiTestDriverDef test = {
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = &s_adapter,
        .adapterCount = 1,
    };
    mrhiAllocator allocator = {0};
    CHECK(mrhiCreateTestDriver(&allocator, &test, 16, &s_made) == mrhi_success, "a driver");
    s_vtable = *s_made.vtable;
    s_vtable.poll = PollInstance;
    s_vtable.createDevice = CreateDevice;
    mrhiExternalDriverDef external = {
        .chain = {.next = nullptr, .type = mrhi_structExternalDriver},
        .vtable = &s_vtable,
        .driver = s_made.self,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &external.chain;
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "an instance");
    return instance;
}

// Lists the instance's one adapter.
static mrhiAdapterId FindAdapter(mrhiInstance* instance)
{
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    search.allowSoftware = true;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    mrhiAdapterId adapter = {0};
    size_t count = 0;
    CHECK(mrhiInstance_RequestAdapters(instance, &search, &request) == mrhi_success &&
              mrhiInstance_TakeNotification(instance, &record) == mrhi_success &&
              record.request.index1 == request.index1 && record.outcome == mrhi_success &&
              mrhiInstance_GetAdapters(instance, &adapter, 1, &count) == mrhi_success && count == 1,
          "one adapter");
    return adapter;
}

static mrhiDevice* Open(mrhiInstance* instance)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.adapter = FindAdapter(instance);
    def.budgets.diagnostics = 8;
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    CHECK(mrhiCreateDevice(instance, &def, &device, &request) == mrhi_success &&
              mrhiInstance_TakeNotification(instance, &record) == mrhi_success &&
              record.outcome == mrhi_success,
          "a device");
    return device;
}

// A poll that claims one event past its room, or SIZE_MAX of them,
// while a search and a frame wait: the core reads only the room, and
// both are answered at the next poll as the driver moved them.
static void TestPollClaims(void)
{
    for (int all = 0; all < 2; ++all)
    {
        s_claimAll = all == 1;
        mrhiInstance* instance = Start();
        s_claiming = true;
        FindAdapter(instance);
        CHECK(!s_claiming, "the search waited through the claim");
        mrhiDevice* device = Open(instance);
        mrhiFrameDef frameDef = mrhiDefaultFrameDef();
        mrhiRequestId token = {0};
        CHECK(mrhiDevice_BeginFrame(device, &frameDef) == mrhi_success &&
                  mrhiDevice_SubmitFrame(device, &token) == mrhi_success,
              "a frame submitted");
        s_claiming = true;
        mrhiDeviceNotification record;
        CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_success && !s_claiming &&
                  record.kind == mrhi_deviceFrameDone && record.request.index1 == token.index1 &&
                  record.outcome == mrhi_success,
              "the frame answered after the claim");
        CHECK(mrhiDevice_TakeNotification(device, &record) == mrhi_empty &&
                  mrhiDevice_GetState(device) == mrhi_deviceReady,
              "and nothing else");
        mrhiDestroyDevice(device);
        mrhiDestroyInstance(instance);
    }
    s_claimAll = false;
}

// A cache whose size cannot hold the envelope's 64-byte header with it
// fails as the driver's; the largest that can is taken.
static void TestCacheSize(void)
{
    mrhiInstance* instance = Start();
    mrhiDevice* device = Open(instance);
    const size_t given[] = {SIZE_MAX, SIZE_MAX - 63, SIZE_MAX - 64};
    const mrhiResult expected[] = {mrhi_errorPlatform, mrhi_errorPlatform, mrhi_success};
    for (size_t i = 0; i < sizeof(given) / sizeof(given[0]); ++i)
    {
        s_cacheBytes = given[i];
        size_t size = 1;
        CHECK(mrhiDevice_GetPipelineCache(device, nullptr, 0, &size) == expected[i] &&
                  size == (expected[i] == mrhi_success ? SIZE_MAX : 0),
              "the cache's size");
    }
    s_cacheBytes = 0;
    mrhiDestroyDevice(device);
    mrhiDestroyInstance(instance);
}

// A declared buffer whose memory alignment is zero or not a power of two
// fails the compile as the driver's, the frame staying open; a power of
// two places it, and so does no memory at all.
static void TestAlignments(void)
{
    mrhiInstance* instance = Start();
    mrhiDevice* device = Open(instance);
    const uint64_t given[] = {0, 3, 96, (UINT64_C(1) << 63) + 1, 1, 1u << 16, UINT64_C(1) << 63};
    for (size_t i = 0; i < sizeof(given) / sizeof(given[0]); ++i)
    {
        s_giveAlignment = true;
        s_alignment = given[i];
        bool power = given[i] != 0 && (given[i] & (given[i] - 1)) == 0;
        mrhiFrameDef frameDef = mrhiDefaultFrameDef();
        CHECK(mrhiDevice_BeginFrame(device, &frameDef) == mrhi_success, "begun");
        mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
        bufferDef.size = 256;
        mrhiResourceId buffer = {0};
        CHECK(mrhiDeclareBuffer(device, &bufferDef, &buffer) == mrhi_success, "declared");
        mrhiAccess access = {.resource = buffer, .kind = mrhi_accessCopyDestination};
        mrhiPassDef passDef = mrhiDefaultPassDef();
        passDef.neverCull = true;
        passDef.accesses = &access;
        passDef.accessCount = 1;
        mrhiPassId pass = {0};
        CHECK(mrhiAddPass(device, &passDef, &pass) == mrhi_success, "a pass");
        CHECK(mrhiDevice_CompileFrame(device) == (power ? mrhi_success : mrhi_errorPlatform),
              "compiled only with a power of two");
        CHECK(mrhiDevice_DropFrame(device) == mrhi_success, "the frame still open to drop");
    }
    s_giveAlignment = false;
    // A texture kept on chip takes no memory, so its alignment is not
    // read.
    s_onChip = true;
    mrhiFrameDef frameDef = mrhiDefaultFrameDef();
    CHECK(mrhiDevice_BeginFrame(device, &frameDef) == mrhi_success, "begun");
    mrhiTextureDef textureDef = mrhiDefaultTextureDef();
    textureDef.format = mrhi_formatRgba8Unorm;
    textureDef.width = 4;
    textureDef.height = 4;
    mrhiResourceId texture = {0};
    CHECK(mrhiDeclareTexture(device, &textureDef, &texture) == mrhi_success, "declared");
    mrhiAccess access = {
        .resource = texture,
        .kind = mrhi_accessCopyDestination,
        .range = {.mipCount = MRHI_REMAINING, .layerCount = MRHI_REMAINING},
    };
    mrhiPassDef passDef = mrhiDefaultPassDef();
    passDef.neverCull = true;
    passDef.accesses = &access;
    passDef.accessCount = 1;
    mrhiPassId pass = {0};
    CHECK(mrhiAddPass(device, &passDef, &pass) == mrhi_success &&
              mrhiDevice_CompileFrame(device) == mrhi_success &&
              mrhiDevice_DropFrame(device) == mrhi_success,
          "a texture on chip compiled");
    s_onChip = false;
    mrhiDestroyDevice(device);
    mrhiDestroyInstance(instance);
}

// A submission refused for capacity names its budget with a submit code;
// any other code, or none the contract lists, is not recorded.
static void TestSubmitBudget(void)
{
    mrhiInstance* instance = Start();
    mrhiDevice* device = Open(instance);
    const mrhiDiagnosticCode given[] = {
        -1,
        INT32_MIN,
        INT32_MAX,
        mrhi_diagnosticNullArgument,
        mrhi_diagnosticSubmitMemory,
        mrhi_diagnosticSubmitDescriptors,
        mrhi_diagnosticSubmitScratch,
        mrhi_diagnosticSubmitCommandLists,
    };
    for (size_t i = 0; i < sizeof(given) / sizeof(given[0]); ++i)
    {
        s_budget = given[i];
        mrhiFrameDef frameDef = mrhiDefaultFrameDef();
        mrhiRequestId token = {0};
        CHECK(mrhiDevice_BeginFrame(device, &frameDef) == mrhi_success &&
                  mrhiDevice_SubmitFrame(device, &token) == mrhi_errorCapacity,
              "refused for capacity");
        mrhiDiagnostic record = {0};
        mrhiResult taken = mrhiDevice_TakeDiagnostic(device, &record);
        CHECK(i < 4 ? taken == mrhi_empty : taken == mrhi_success && record.code == given[i],
              "only a submit code recorded");
    }
    s_budget = 0;
    mrhiDestroyDevice(device);
    mrhiDestroyInstance(instance);
}

// An adapter listed with a kind the contract does not list (-1,
// INT32_MIN or one past the last) reads as mrhi_adapterUnknown, and the
// last kind as itself. Its name ends within its room before the first
// byte that is not well-formed UTF-8: a character the room cuts, a
// malformed byte or a NUL; a character that ends at the room's last byte
// is kept.
static void TestAdapterInfo(void)
{
    const mrhiAdapterKind kinds[] = {-1, INT32_MIN, mrhi_adapterSoftware + 1, mrhi_adapterSoftware};
    for (size_t i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i)
    {
        s_info.kind = kinds[i];
        mrhiInstance* instance = Start();
        mrhiAdapterInfo info;
        CHECK(mrhiAdapter_GetInfo(instance, FindAdapter(instance), &info) == mrhi_success &&
                  info.kind == (i < 3 ? mrhi_adapterUnknown : mrhi_adapterSoftware),
              "the kind the program reads");
        mrhiDestroyInstance(instance);
    }
    s_info.kind = mrhi_adapterDiscrete;
    enum
    {
        ROOM = MRHI_ADAPTER_NAME_BYTES
    };
    // The offset and bytes put over a name of 'a's, its length, and the
    // length the program reads.
    static const struct
    {
        size_t at;
        const char* bytes;
        size_t count;
        size_t length;
        size_t read;
    } names[] = {
        {0, "", 0, ROOM + 1, ROOM},
        {0, "", 0, SIZE_MAX, ROOM},
        {ROOM - 1, "\xC3", 1, ROOM + 1, ROOM - 1},
        {ROOM - 2, "\xE2\x82", 2, ROOM, ROOM - 2},
        {2, "\xFF", 1, 5, 2},
        {2, "\0", 1, 5, 2},
        {ROOM - 2, "\xC3\xA9", 2, ROOM, ROOM},
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        memset(s_info.name, 'a', ROOM);
        memcpy(s_info.name + names[i].at, names[i].bytes, names[i].count);
        s_info.nameLength = names[i].length;
        mrhiInstance* instance = Start();
        mrhiAdapterInfo info;
        CHECK(mrhiAdapter_GetInfo(instance, FindAdapter(instance), &info) == mrhi_success &&
                  info.nameLength == names[i].read &&
                  memcmp(info.name, s_info.name, info.nameLength) == 0,
              "the name the program reads");
        mrhiDestroyInstance(instance);
    }
    s_info = (mrhiAdapterInfo){.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete};
}

int main(void)
{
    TestPollClaims();
    TestCacheSize();
    TestAlignments();
    TestSubmitBudget();
    TestAdapterInfo();
    return s_failures == 0 ? 0 : 1;
}
