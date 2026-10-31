// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The validation layer, in builds with MAUL_RHI_VALIDATION: the test
// driver handed in from outside with one function broken at a time,
// each breach counted by mrhiInstance_GetDriverFaultCount and recorded in the
// instance's diagnostic queue with its rule's code while the call goes
// on, and the edges a driver keeping the contract may reach left alone;
// and a malformed frame, whose faults the walk counts without reading
// past what it checked.

#include "device_core.h"
#include "driver_test.h"
#include "frame_walk.h"
#include "instance_core.h"
#include "test_harness.h"

#include "maul-rhi/device.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/resources.h"

#include <string.h>

static mrhiTestAdapter s_adapter;
static mrhiInstanceDriver s_made;
static mrhiInstanceDriverVtable s_vtable;
static mrhiDeviceDriverVtable s_deviceVtable;
static const mrhiDeviceDriverVtable* s_deviceInner;
// Whether the next driver lists its adapter with a name of the most bytes.
static bool s_longName;
// The kind the next driver lists its adapter with.
static mrhiAdapterKind s_kind = mrhi_adapterDiscrete;
// The name the next driver lists its adapter with, when not NULL.
static const char* s_name;
static size_t s_nameLength;

// The test driver, its vtable copied for the test to break.
static void Open(void)
{
    s_adapter = (mrhiTestAdapter){
        .info = {.driver = mrhi_driverTest, .kind = s_kind},
        .limits = mrhiDefaultLimits(),
    };
    if (s_longName)
    {
        s_adapter.info.nameLength = MRHI_ADAPTER_NAME_BYTES;
        for (uint32_t i = 0; i < MRHI_ADAPTER_NAME_BYTES; ++i)
        {
            s_adapter.info.name[i] = 'a';
        }
    }
    if (s_name != nullptr)
    {
        memcpy(s_adapter.info.name, s_name, s_nameLength);
        s_adapter.info.nameLength = s_nameLength;
    }
    mrhiTestDriverDef test = {
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = &s_adapter,
        .adapterCount = 1,
    };
    mrhiAllocator allocator = {0};
    CHECK(mrhiCreateTestDriver(&allocator, &test, 16, &s_made) == mrhi_success, "a driver");
    s_vtable = *s_made.vtable;
}

static mrhiInstance* Start(void)
{
    mrhiExternalDriverDef external = {
        .chain = {.next = nullptr, .type = mrhi_structExternalDriver},
        .vtable = &s_vtable,
        .driver = s_made.self,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &external.chain;
    def.limits.diagnostics = 8;
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "an instance");
    return instance;
}

// The code of the instance's oldest diagnostic, or 0 when there is none.
static mrhiDiagnosticCode Recorded(mrhiInstance* instance)
{
    mrhiDiagnostic record = {0};
    return mrhiInstance_TakeDiagnostic(instance, &record) == mrhi_success ? record.code : 0;
}

// Searches, answering at the next poll: the adapters listed.
static size_t Search(mrhiInstance* instance, mrhiAdapterId* adapter)
{
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    mrhiInstanceNotification record;
    size_t count = 0;
    CHECK(mrhiInstance_RequestAdapters(instance, &search, &request) == mrhi_success, "a search");
    if (mrhiInstance_TakeNotification(instance, &record) == mrhi_success)
    {
        CHECK(mrhiInstance_GetAdapters(instance, adapter, 1, &count) == mrhi_success, "listed");
    }
    return count;
}

static void TestClean(void)
{
    Open();
    mrhiInstance* instance = Start();
    mrhiAdapterId adapter = {0};
    CHECK(Search(instance, &adapter) == 1, "one adapter");
    CHECK(mrhiInstance_GetDriverFaultCount(instance) == 0,
          "none from a driver that keeps the contract");
    CHECK(mrhiInstance_GetDriverFaultCount(nullptr) == 0, "no instance");
    CHECK(Recorded(instance) == 0, "nothing recorded");
    mrhiDestroyInstance(instance);
}

static mrhiResult AnswerElsewhere(void* self, uint64_t tag)
{
    return s_made.vtable->requestAdapters(self, tag + 1000);
}

static size_t ListTwice(const void* self, mrhiDriverAdapter* adapters, size_t capacity)
{
    size_t total = s_made.vtable->getAdapters(self, adapters, capacity);
    if (total == 1 && capacity >= 2)
    {
        adapters[1] = adapters[0];
        return 2;
    }
    return total;
}

// Claims more events than the room given: the layer must not let the
// core read past its array.
static size_t OverReport(void* self, mrhiDriverEvent* events, size_t capacity)
{
    for (size_t i = 0; i < capacity; ++i)
    {
        events[i] = (mrhiDriverEvent){0};
    }
    return s_made.vtable->poll(self, events, capacity) > 0 ? capacity + 1 : 0;
}

// Answers a search twice: the second answers nothing asked.
static size_t AnswerTwice(void* self, mrhiDriverEvent* events, size_t capacity)
{
    size_t moved = s_made.vtable->poll(self, events, capacity);
    if (moved == 1 && capacity >= 2)
    {
        events[1] = events[0];
        return 2;
    }
    return moved;
}

static void TestInstanceAnswers(void)
{
    Open();
    s_vtable.requestAdapters = AnswerElsewhere;
    mrhiInstance* instance = Start();
    mrhiAdapterId adapter = {0};
    CHECK(Search(instance, &adapter) == 0, "a search answered for another request");
    CHECK(mrhiInstance_GetDriverFaultCount(instance) == 1, "counted");
    CHECK(Recorded(instance) == mrhi_diagnosticDriverUnaskedAnswer, "recorded");
    mrhiDestroyInstance(instance);
    Open();
    s_vtable.getAdapters = ListTwice;
    instance = Start();
    Search(instance, &adapter);
    CHECK(mrhiInstance_GetDriverFaultCount(instance) == 1 &&
              Recorded(instance) == mrhi_diagnosticDriverAdapter,
          "an adapter listed twice");
    mrhiDestroyInstance(instance);
    Open();
    s_vtable.poll = OverReport;
    instance = Start();
    Search(instance, &adapter);
    CHECK(mrhiInstance_GetDriverFaultCount(instance) >= 1 &&
              Recorded(instance) == mrhi_diagnosticDriverEventsOverrun,
          "more events than room, clamped");
    mrhiDestroyInstance(instance);
    Open();
    s_vtable.poll = AnswerTwice;
    instance = Start();
    Search(instance, &adapter);
    CHECK(mrhiInstance_GetDriverFaultCount(instance) == 1 &&
              Recorded(instance) == mrhi_diagnosticDriverUnaskedAnswer,
          "a search answered twice");
    mrhiDestroyInstance(instance);
    // A name of the most bytes keeps the contract.
    s_longName = true;
    Open();
    s_longName = false;
    instance = Start();
    CHECK(Search(instance, &adapter) == 1 && mrhiInstance_GetDriverFaultCount(instance) == 0,
          "an adapter's name of the most bytes");
    mrhiDestroyInstance(instance);
}

// A device whose acquire, timestamp period and events the test gives.
static uint64_t s_image;
static mrhiResult s_acquired;
static double s_period;
static mrhiDriverEvent s_events[2];
static size_t s_eventCount;
static bool s_inject;
// Set to report the device's loss at the next poll, once.
static bool s_loseOnce;

static mrhiResult AcquireGiven(void* self, uint64_t swapchain, uint64_t* imageOut)
{
    (void)self;
    (void)swapchain;
    *imageOut = s_image;
    return s_acquired;
}

static double PeriodGiven(void* self)
{
    (void)self;
    return s_period;
}

static size_t PollGiven(void* self, mrhiDriverEvent* events, size_t capacity)
{
    if (s_loseOnce && capacity > 0)
    {
        s_loseOnce = false;
        events[0] = (mrhiDriverEvent){0, mrhi_errorDeviceLost};
        return 1;
    }
    if (!s_inject)
    {
        return s_deviceInner->poll(self, events, capacity);
    }
    for (size_t i = 0; i < s_eventCount && i < capacity; ++i)
    {
        events[i] = s_events[i];
    }
    return s_eventCount;
}

static mrhiDeviceLossReport s_report;

static void ReportGiven(void* self, mrhiDeviceLossReport* reportOut)
{
    (void)self;
    *reportOut = s_report;
}

static mrhiResult CreateGivenDevice(void* self, uint64_t adapter, const mrhiDeviceDef* def,
                                    uint64_t tag, mrhiDeviceDriver* deviceOut)
{
    mrhiResult status = s_made.vtable->createDevice(self, adapter, def, tag, deviceOut);
    if (status == mrhi_success)
    {
        s_deviceInner = deviceOut->vtable;
        s_deviceVtable = *deviceOut->vtable;
        s_deviceVtable.acquireImage = AcquireGiven;
        s_deviceVtable.timestampPeriod = PeriodGiven;
        s_deviceVtable.poll = PollGiven;
        s_deviceVtable.lossReport = ReportGiven;
        deviceOut->vtable = &s_deviceVtable;
    }
    return status;
}

// The layer's answers at their edges, called as the core calls them.
// An adapter kind is int32_t, so a check against the last kind alone
// would miss a driver listing a negative one: -1 and INT32_MIN are each
// a breach.
static void TestNegativeAdapterKind(void)
{
    static const int32_t negative[] = {-1, INT32_MIN};
    for (size_t i = 0; i < sizeof(negative) / sizeof(negative[0]); ++i)
    {
        s_kind = negative[i];
        Open();
        mrhiInstance* instance = Start();
        mrhiAdapterId adapter = {0};
        Search(instance, &adapter);
        CHECK(mrhiInstance_GetDriverFaultCount(instance) == 1 &&
                  Recorded(instance) == mrhi_diagnosticDriverAdapter,
              "a negative adapter kind");
        mrhiDestroyInstance(instance);
    }
    s_kind = mrhi_adapterDiscrete;
}

// A name that is not well-formed UTF-8 without NUL is a breach: a
// malformed byte, a NUL, or a character its length cuts.
static void TestAdapterName(void)
{
    // Octal escapes, since a hex escape would take the 'a' after it.
    static const char* const names[] = {"a\377a", "a\0a", "aa\303"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        s_name = names[i];
        s_nameLength = 3;
        Open();
        mrhiInstance* instance = Start();
        mrhiAdapterId adapter = {0};
        Search(instance, &adapter);
        CHECK(mrhiInstance_GetDriverFaultCount(instance) == 1 &&
                  Recorded(instance) == mrhi_diagnosticDriverAdapter,
              "a name that is not UTF-8");
        mrhiDestroyInstance(instance);
    }
    s_name = nullptr;
}

static void TestDeviceEdges(void)
{
    Open();
    s_vtable.createDevice = CreateGivenDevice;
    mrhiInstance* instance = Start();
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    CHECK(Search(instance, &deviceDef.adapter) == 1, "one adapter");
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    CHECK(mrhiCreateDevice(instance, &deviceDef, &device, &request) == mrhi_success &&
              mrhiInstance_TakeNotification(instance, &record) == mrhi_success,
          "a device");
    const mrhiDeviceDriverVtable* layer = device->driver.vtable;
    void* self = device->driver.self;
    uint64_t image = 1;
    s_image = 0;
    s_acquired = mrhi_errorDeviceLost;
    CHECK(layer->acquireImage(self, 1, &image) == mrhi_errorDeviceLost &&
              mrhiInstance_GetDriverFaultCount(instance) == 0,
          "no image with a failed acquire");
    s_acquired = mrhi_success;
    CHECK(layer->acquireImage(self, 1, &image) == mrhi_success &&
              mrhiInstance_GetDriverFaultCount(instance) == 1 &&
              Recorded(instance) == mrhi_diagnosticDriverZeroHandle,
          "an acquire that gives no image");
    s_period = 0.0;
    CHECK(layer->timestampPeriod(self) == 0.0 && mrhiInstance_GetDriverFaultCount(instance) == 1,
          "a period of zero from a driver that does not say");
    s_period = -1.0;
    CHECK(layer->timestampPeriod(self) == -1.0 && mrhiInstance_GetDriverFaultCount(instance) == 2 &&
              Recorded(instance) == mrhi_diagnosticDriverTimestampPeriod,
          "a negative period");
    // As many events as room, the last the device's loss under tag 0.
    mrhiDriverEvent events[2];
    s_events[0] = (mrhiDriverEvent){5, mrhi_success};
    s_events[1] = (mrhiDriverEvent){0, mrhi_errorDeviceLost};
    s_eventCount = 2;
    s_inject = true;
    CHECK(layer->poll(self, events, 2) == 2 && mrhiInstance_GetDriverFaultCount(instance) == 2,
          "events that fill the room, and the loss under tag 0");
    s_events[0] = (mrhiDriverEvent){0, mrhi_success};
    s_eventCount = 1;
    CHECK(layer->poll(self, events, 2) == 1 && mrhiInstance_GetDriverFaultCount(instance) == 3 &&
              Recorded(instance) == mrhi_diagnosticDriverEventTag,
          "tag 0 for anything but the loss");
    s_inject = false;
    mrhiDestroyDevice(device);
    mrhiDestroyInstance(instance);
}

// A report a driver gives: its reason; its message, every byte 'a' but
// tailLength bytes of tail at the offset at; and its length. Then what
// the program reads, and whether the layer counts a breach.
typedef struct GivenReport
{
    mrhiDeviceLossReason reason;
    size_t at;
    const char* tail;
    size_t tailLength;
    size_t length;
    mrhiDeviceLossReason readReason;
    size_t readLength;
    bool breach;
} GivenReport;

// A device lost by its driver with each report the test gives. A reason
// the contract does not list (-1, INT32_MIN or one past the last) reads
// as mrhi_lossUnknown. A message ends within its room before the first
// byte that is not well-formed UTF-8: a character the room cuts (a
// lead byte at the last byte, two of a character's three), a malformed
// byte or a NUL. Each is a breach; a character that ends at the room's
// last byte, and no message, keep the contract.
static void TestLossReports(void)
{
    enum
    {
        ROOM = MRHI_LOSS_MESSAGE_BYTES
    };
    static const GivenReport given[] = {
        {-1, 0, "", 0, 4, mrhi_lossUnknown, 4, true},
        {INT32_MIN, 0, "", 0, 4, mrhi_lossUnknown, 4, true},
        {mrhi_lossSimulated + 1, 0, "", 0, 4, mrhi_lossUnknown, 4, true},
        {mrhi_lossHung, 0, "", 0, ROOM + 1, mrhi_lossHung, ROOM, true},
        {mrhi_lossHung, ROOM - 1, "\xC3", 1, ROOM + 1, mrhi_lossHung, ROOM - 1, true},
        {mrhi_lossHung, ROOM - 2, "\xE2\x82", 2, ROOM + 1, mrhi_lossHung, ROOM - 2, true},
        {mrhi_lossHung, ROOM - 1, "\xC3", 1, ROOM, mrhi_lossHung, ROOM - 1, true},
        {mrhi_lossHung, 2, "\xFF", 1, 5, mrhi_lossHung, 2, true},
        {mrhi_lossHung, 2, "\0", 1, 5, mrhi_lossHung, 2, true},
        {mrhi_lossSimulated, ROOM - 2, "\xC3\xA9", 2, ROOM, mrhi_lossSimulated, ROOM, false},
        {mrhi_lossHung, 0, "", 0, 0, mrhi_lossHung, 0, false},
    };
    for (size_t i = 0; i < sizeof(given) / sizeof(given[0]); ++i)
    {
        const GivenReport* row = &given[i];
        s_report = (mrhiDeviceLossReport){.reason = row->reason, .messageLength = row->length};
        memset(s_report.message, 'a', ROOM);
        memcpy(s_report.message + row->at, row->tail, row->tailLength);
        Open();
        s_vtable.createDevice = CreateGivenDevice;
        mrhiInstance* instance = Start();
        mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
        CHECK(Search(instance, &deviceDef.adapter) == 1, "one adapter");
        mrhiDevice* device = nullptr;
        mrhiRequestId request;
        mrhiInstanceNotification record;
        CHECK(mrhiCreateDevice(instance, &deviceDef, &device, &request) == mrhi_success &&
                  mrhiInstance_TakeNotification(instance, &record) == mrhi_success,
              "a device");
        s_loseOnce = true;
        mrhiDeviceNotification notice;
        CHECK(mrhiDevice_TakeNotification(device, &notice) == mrhi_success &&
                  notice.kind == mrhi_deviceLostNotice,
              "lost by the driver");
        mrhiDeviceLossReport report;
        CHECK(mrhiDevice_GetLossReport(device, &report) == mrhi_success &&
                  report.reason == row->readReason && report.messageLength == row->readLength &&
                  memcmp(report.message, s_report.message, report.messageLength) == 0,
              "the report the program reads");
        CHECK(mrhiInstance_GetDriverFaultCount(instance) == (row->breach ? 1 : 0) &&
                  Recorded(instance) == (row->breach ? mrhi_diagnosticDriverLossReport : 0),
              "each breach counted, and the edges left alone");
        mrhiDestroyDevice(device);
        mrhiDestroyInstance(instance);
    }
    s_report = (mrhiDeviceLossReport){0};
}

static mrhiSurfaceCaps s_caps;

static void CapsGiven(const void* self, uint64_t surface, uint64_t adapter,
                      mrhiSurfaceCaps* capsOut)
{
    (void)self;
    (void)surface;
    (void)adapter;
    *capsOut = s_caps;
}

// The layer's surface caps, called as the core calls them: a color whose
// format, primaries, transfer or range the contract does not list (-1,
// INT32_MIN or one past the last), or more colors than the array holds,
// is a breach, and so are presentable caps missing a floor; the most
// colors, each of the last values, keep the
// contract.
static void TestSurfaceCaps(void)
{
    Open();
    s_vtable.getSurfaceCaps = CapsGiven;
    mrhiInstance* instance = Start();
    const mrhiInstanceDriver* layer = &instance->driver;
    const mrhiSurfaceColor last = {
        .format = mrhi_formatAstc12x12UnormSrgb,
        .primaries = mrhi_primariesDisplayP3,
        .transfer = mrhi_transferPq,
        .range = mrhi_rangeExtended,
    };
    // 8-bit sRGB in Rec. 709 first, the floor, then the last values.
    s_caps = (mrhiSurfaceCaps){
        .presentable = true,
        .colorCount = MRHI_SURFACE_COLORS,
        .presentModes = mrhi_presentFifoBit,
        .alphaModes = mrhi_alphaOpaqueBit,
        .usages = mrhi_textureRenderTarget,
        .twinViews = true,
    };
    s_caps.colors[0] = (mrhiSurfaceColor){.format = mrhi_formatBgra8Unorm};
    for (uint32_t i = 1; i < MRHI_SURFACE_COLORS; ++i)
    {
        s_caps.colors[i] = last;
    }
    mrhiSurfaceCaps caps;
    layer->vtable->getSurfaceCaps(layer->self, 1, 1, &caps);
    CHECK(mrhiInstance_GetDriverFaultCount(instance) == 0,
          "the most colors, each of the last values");
    uint64_t faults = 0;
    s_caps.colorCount = MRHI_SURFACE_COLORS + 1;
    layer->vtable->getSurfaceCaps(layer->self, 1, 1, &caps);
    CHECK(mrhiInstance_GetDriverFaultCount(instance) == ++faults &&
              Recorded(instance) == mrhi_diagnosticDriverSurfaceCaps,
          "more colors than the array holds");
    s_caps.colorCount = MRHI_SURFACE_COLORS;
    const int32_t pastLast[] = {last.format + 1, last.primaries + 1, last.transfer + 1,
                                last.range + 1};
    for (uint32_t part = 0; part < 4; ++part)
    {
        const int32_t unknown[] = {-1, INT32_MIN, pastLast[part]};
        for (size_t i = 0; i < sizeof(unknown) / sizeof(unknown[0]); ++i)
        {
            mrhiSurfaceColor* color = &s_caps.colors[MRHI_SURFACE_COLORS - 1];
            int32_t* parts[] = {&color->format, &color->primaries, &color->transfer, &color->range};
            *color = last;
            *parts[part] = unknown[i];
            layer->vtable->getSurfaceCaps(layer->self, 1, 1, &caps);
            CHECK(mrhiInstance_GetDriverFaultCount(instance) == ++faults &&
                      Recorded(instance) == mrhi_diagnosticDriverSurfaceCaps,
                  "an unknown part of the last color");
        }
    }
    s_caps.colors[MRHI_SURFACE_COLORS - 1] = last;
    // Presentable caps without a floor: FIFO, opaque alpha, render target
    // usage, a twin, or 8-bit sRGB in Rec. 709. Caps that do not present
    // need none.
    const mrhiSurfaceCaps floors = s_caps;
    for (int floor = 0; floor < 5; ++floor)
    {
        s_caps = floors;
        s_caps.colors[0] = floor == 4 ? last : s_caps.colors[0];
        s_caps.presentModes = floor == 0 ? mrhi_presentMailboxBit : s_caps.presentModes;
        s_caps.alphaModes = floor == 1 ? mrhi_alphaPremultipliedBit : s_caps.alphaModes;
        s_caps.usages = floor == 2 ? mrhi_textureCopySource : s_caps.usages;
        s_caps.twinViews = floor != 3;
        layer->vtable->getSurfaceCaps(layer->self, 1, 1, &caps);
        CHECK(mrhiInstance_GetDriverFaultCount(instance) == ++faults &&
                  Recorded(instance) == mrhi_diagnosticDriverSurfaceFloors,
              "presentable caps without a floor");
    }
    // A count past the array with no 8-bit sRGB in the colors it holds:
    // both breaches, the colors read no further than the array.
    s_caps = floors;
    s_caps.colors[0] = last;
    s_caps.colorCount = UINT32_MAX;
    layer->vtable->getSurfaceCaps(layer->self, 1, 1, &caps);
    faults += 2;
    CHECK(mrhiInstance_GetDriverFaultCount(instance) == faults &&
              Recorded(instance) == mrhi_diagnosticDriverSurfaceCaps &&
              Recorded(instance) == mrhi_diagnosticDriverSurfaceFloors,
          "too many colors, none of them 8-bit sRGB");
    s_caps.colorCount = MRHI_SURFACE_COLORS;
    s_caps.presentable = false;
    layer->vtable->getSurfaceCaps(layer->self, 1, 1, &caps);
    CHECK(mrhiInstance_GetDriverFaultCount(instance) == faults,
          "caps that do not present, without the floors");
    s_caps = floors;
    s_caps.twinViews = false;
    s_caps.twinImages = true;
    layer->vtable->getSurfaceCaps(layer->self, 1, 1, &caps);
    CHECK(mrhiInstance_GetDriverFaultCount(instance) == faults, "twin images alone");
    s_caps = (mrhiSurfaceCaps){0};
    mrhiDestroyInstance(instance);
}

static mrhiResult MakeNothing(void* self, const mrhiBufferDef* def, uint64_t* handleOut)
{
    mrhiResult status = s_deviceInner->createBuffer(self, def, handleOut);
    *handleOut = 0;
    return status;
}

// What TakeAnything answers, and the budget it names.
static mrhiResult s_submitStatus;
static mrhiDiagnosticCode s_submitBudget;

// Takes any frame without the test driver's own walk, which would trap.
static mrhiResult TakeAnything(void* self, const mrhiDriverFrame* frame, uint64_t tag,
                               mrhiDiagnosticCode* budgetOut)
{
    (void)self;
    (void)frame;
    (void)tag;
    *budgetOut = s_submitBudget;
    return s_submitStatus;
}

static mrhiResult CreateBreakingDevice(void* self, uint64_t adapter, const mrhiDeviceDef* def,
                                       uint64_t tag, mrhiDeviceDriver* deviceOut)
{
    mrhiResult status = s_made.vtable->createDevice(self, adapter, def, tag, deviceOut);
    if (status == mrhi_success)
    {
        s_deviceInner = deviceOut->vtable;
        s_deviceVtable = *deviceOut->vtable;
        s_deviceVtable.createBuffer = MakeNothing;
        s_deviceVtable.submitFrame = TakeAnything;
        deviceOut->vtable = &s_deviceVtable;
    }
    return status;
}

static void TestDeviceAnswers(void)
{
    Open();
    s_vtable.createDevice = CreateBreakingDevice;
    mrhiInstance* instance = Start();
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    CHECK(Search(instance, &deviceDef.adapter) == 1, "one adapter");
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    CHECK(mrhiCreateDevice(instance, &deviceDef, &device, &request) == mrhi_success &&
              mrhiInstance_TakeNotification(instance, &record) == mrhi_success,
          "a device");
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = 256;
    bufferDef.usage = mrhi_bufferCopyDestination;
    mrhiBufferId buffer;
    CHECK(mrhiCreateBuffer(device, &bufferDef, &buffer) == mrhi_success, "the call goes on");
    CHECK(mrhiInstance_GetDriverFaultCount(instance) == 1 &&
              Recorded(instance) == mrhi_diagnosticDriverZeroHandle,
          "a buffer whose handle is zero");
    // A frame no driver could translate, handed to the layer as the core
    // hands frames: one pass whose chunk the frame lacks.
    mrhiDriverPass pass = {.id = {1, 1}, .firstChunk = 1};
    mrhiDriverFrame frame = {.passes = &pass, .passCount = 1};
    mrhiDiagnosticCode budget = 0;
    CHECK(device->driver.vtable->submitFrame(device->driver.self, &frame, 1, &budget) ==
                  mrhi_success &&
              mrhiInstance_GetDriverFaultCount(instance) == 2 &&
              Recorded(instance) == mrhi_diagnosticDriverFrameWalk,
          "a frame walked and found wanting");
    // A refusal for capacity names a submit budget, and only it does.
    mrhiDriverFrame empty = {0};
    s_submitStatus = mrhi_errorCapacity;
    s_submitBudget = mrhi_diagnosticSubmitScratch;
    CHECK(device->driver.vtable->submitFrame(device->driver.self, &empty, 2, &budget) ==
                  mrhi_errorCapacity &&
              budget == mrhi_diagnosticSubmitScratch &&
              mrhiInstance_GetDriverFaultCount(instance) == 2,
          "a budget named");
    s_submitBudget = 0;
    budget = 0;
    CHECK(device->driver.vtable->submitFrame(device->driver.self, &empty, 3, &budget) ==
                  mrhi_errorCapacity &&
              mrhiInstance_GetDriverFaultCount(instance) == 3 &&
              Recorded(instance) == mrhi_diagnosticDriverSubmitBudget,
          "no budget named");
    s_submitBudget = mrhi_diagnosticFramePasses;
    CHECK(device->driver.vtable->submitFrame(device->driver.self, &empty, 4, &budget) ==
                  mrhi_errorCapacity &&
              mrhiInstance_GetDriverFaultCount(instance) == 4 &&
              Recorded(instance) == mrhi_diagnosticDriverSubmitBudget,
          "a code that is no submit budget");
    s_submitStatus = mrhi_success;
    s_submitBudget = mrhi_diagnosticSubmitMemory;
    budget = 0;
    CHECK(device->driver.vtable->submitFrame(device->driver.self, &empty, 5, &budget) ==
                  mrhi_success &&
              mrhiInstance_GetDriverFaultCount(instance) == 5 &&
              Recorded(instance) == mrhi_diagnosticDriverSubmitBudget,
          "a budget named with success");
    s_submitBudget = 0;
    mrhiDestroyDevice(device);
    mrhiDestroyInstance(instance);
}

static bool AnyHandle(const void* handles, uint64_t handle)
{
    (void)handles;
    return handle != 0;
}

static void TestWalk(void)
{
    // A pass whose chunk is past the frame's, then one whose command's
    // payload runs past its chunk: the walk stops at each.
    mrhiCommandChunk chunk = {.count = 1};
    chunk.commands[0] = (mrhiCommand){.type = mrhiCommandDraw, .payload = 5};
    mrhiDriverPass passes[2] = {
        {.id = {1, 1}, .firstChunk = 2},
        {.id = {2, 1}, .firstChunk = 1},
    };
    mrhiDriverFrame frame = {
        .passes = passes,
        .passCount = 2,
        .chunks = &chunk,
        .chunkCount = 1,
    };
    // The two, and the chunk no walked pass took.
    CHECK(mrhiWalkFrame(&frame, AnyHandle, nullptr, nullptr) == 3, "each fault counted");
    chunk.commands[0].payload = 0;
    passes[0].firstChunk = 0;
    CHECK(mrhiWalkFrame(&frame, AnyHandle, nullptr, nullptr) == 0, "a frame that holds");
}

// Walks one pass over one chunk of a frame of a buffer (1) and a
// texture (2): its faults.
static uint64_t WalkOne(const mrhiCommandChunk* chunk, const mrhiDriverPass* pass)
{
    static const mrhiTextureDef texture = {.width = 4, .height = 4};
    static const mrhiDriverResource resources[2] = {
        {.kind = mrhiDriverDeviceBuffer, .handle = 1, .size = 256},
        {.kind = mrhiDriverDeviceTexture, .handle = 2, .texture = &texture},
    };
    mrhiDriverFrame frame = {
        .resources = resources,
        .resourceCount = 2,
        .passes = pass,
        .passCount = 1,
        .chunks = chunk,
        .chunkCount = 1,
    };
    return mrhiWalkFrame(&frame, AnyHandle, nullptr, nullptr);
}

// An instance with room for two answers over a driver with room for
// sixteen: the instance itself refuses the third request.
static void TestInstanceRoom(void)
{
    Open();
    mrhiExternalDriverDef external = {
        .chain = {.next = nullptr, .type = mrhi_structExternalDriver},
        .vtable = &s_vtable,
        .driver = s_made.self,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &external.chain;
    def.limits.notifications = 2;
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "an instance of two answers");
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    CHECK(mrhiInstance_RequestAdapters(instance, &search, &request) == mrhi_success &&
              mrhiInstance_RequestAdapters(instance, &search, &request) == mrhi_success,
          "two searches");
    CHECK(mrhiInstance_RequestAdapters(instance, &search, &request) == mrhi_errorCapacity,
          "no room for a third answer");
    mrhiDestroyInstance(instance);
}

// The walk at its edges: the last binding kind, both objects of a
// counted draw, a chunk of the most commands, a payload one past its
// chunk, and a pass of the most color targets.
static void TestWalkEdges(void)
{
    static mrhiCommandChunk chunk;
    mrhiDriverPass pass = {.id = {1, 1}, .firstChunk = 1};
    mrhiCommandBinding binding = {.object = 2, .kind = mrhi_bindingStorageTexture};
    chunk = (mrhiCommandChunk){.count = 2};
    chunk.commands[0] = (mrhiCommand){.type = mrhiCommandBindings, .payload = 1};
    memcpy(&chunk.commands[1], &binding, sizeof(binding));
    CHECK(WalkOne(&chunk, &pass) == 0, "a storage texture binding");
    chunk = (mrhiCommandChunk){.count = 1};
    chunk.commands[0] = (mrhiCommand){.type = mrhiCommandDrawIndirectCount, .a = 1, .b = 2};
    CHECK(WalkOne(&chunk, &pass) == 1, "a counted draw whose count is a texture");
    chunk.commands[0].a = 2;
    chunk.commands[0].b = 1;
    CHECK(WalkOne(&chunk, &pass) == 1, "and one whose arguments are");
    chunk = (mrhiCommandChunk){.count = MRHI_CHUNK_COMMANDS};
    for (uint32_t i = 0; i < MRHI_CHUNK_COMMANDS; ++i)
    {
        chunk.commands[i] = (mrhiCommand){.type = mrhiCommandDraw};
    }
    CHECK(WalkOne(&chunk, &pass) == 0, "a chunk of the most commands");
    chunk = (mrhiCommandChunk){.count = 2};
    chunk.commands[0] = (mrhiCommand){.type = mrhiCommandDraw};
    chunk.commands[1] = (mrhiCommand){.type = mrhiCommandDraw, .payload = 1};
    // The walk stops there, so the chunk counts as no pass's too.
    CHECK(WalkOne(&chunk, &pass) == 2, "a payload one record past its chunk");
    chunk = (mrhiCommandChunk){.count = 1};
    chunk.commands[0] = (mrhiCommand){.type = mrhiCommandDraw};
    pass.colorTargetCount = MRHI_MAX_COLOR_TARGETS;
    for (uint32_t i = 0; i < MRHI_MAX_COLOR_TARGETS; ++i)
    {
        pass.colorTargets[i].resource = (mrhiResourceId){2, 1};
    }
    CHECK(WalkOne(&chunk, &pass) == 0, "a pass of the most color targets");
}

// Kinds are int32_t, so a walk check against a kind's last value alone
// would pass a negative one: a pass's class, an access's state and a
// barrier's states of -1 or INT32_MIN are each a fault.
static void TestWalkNegativeKinds(void)
{
    static mrhiCommandChunk chunk;
    chunk = (mrhiCommandChunk){.count = 1};
    chunk.commands[0] = (mrhiCommand){.type = mrhiCommandDraw};
    static const mrhiDriverResource resources[1] = {
        {.kind = mrhiDriverDeviceBuffer, .handle = 1, .size = 256},
    };
    mrhiDriverAccess access = {.resource = 1, .state = mrhi_stateSealed};
    mrhiDriverPass pass = {.id = {1, 1}, .firstChunk = 1, .accesses = &access, .accessCount = 1};
    mrhiBarrier barrier = {
        .resource = {1, 1}, .before = mrhi_stateUndefined, .after = mrhi_stateSealed};
    mrhiDriverFrame frame = {
        .resources = resources,
        .resourceCount = 1,
        .passes = &pass,
        .passCount = 1,
        .chunks = &chunk,
        .chunkCount = 1,
        .barriers = &barrier,
        .barrierCount = 1,
    };
    CHECK(mrhiWalkFrame(&frame, AnyHandle, nullptr, nullptr) == 0, "a frame that holds");
    static const int32_t negative[] = {-1, INT32_MIN};
    for (size_t i = 0; i < sizeof(negative) / sizeof(negative[0]); ++i)
    {
        pass.passClass = negative[i];
        CHECK(mrhiWalkFrame(&frame, AnyHandle, nullptr, nullptr) >= 1, "a negative class");
        pass.passClass = mrhi_passGraphics;
        access.state = negative[i];
        CHECK(mrhiWalkFrame(&frame, AnyHandle, nullptr, nullptr) == 1, "a negative access state");
        access.state = mrhi_stateSealed;
        barrier.before = negative[i];
        CHECK(mrhiWalkFrame(&frame, AnyHandle, nullptr, nullptr) == 1, "a negative state before");
        barrier.before = mrhi_stateUndefined;
        barrier.after = negative[i];
        CHECK(mrhiWalkFrame(&frame, AnyHandle, nullptr, nullptr) == 1, "a negative state after");
        barrier.after = mrhi_stateSealed;
    }
}

int main(void)
{
    TestClean();
    TestInstanceAnswers();
    TestNegativeAdapterKind();
    TestAdapterName();
    TestDeviceAnswers();
    TestDeviceEdges();
    TestLossReports();
    TestSurfaceCaps();
    TestInstanceRoom();
    TestWalk();
    TestWalkEdges();
    TestWalkNegativeKinds();
    return s_failures == 0 ? 0 : 1;
}
