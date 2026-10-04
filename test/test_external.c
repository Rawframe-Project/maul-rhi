// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// External drivers (mrhi-0024): the test driver handed to an instance
// through mrhiExternalDriverDef as a program would hand its own. Its
// adapters are reported as external; the handshake refuses another SPI
// version, a short vtable or a missing function, of the instance
// driver and of its devices; a refused driver stays the program's, and
// an accepted one is destroyed with the instance (the sanitizers see
// either mistake); its own surface sources reach it unexamined.

#include "driver_test.h"
#include "test_harness.h"

#include "maul-rhi/device.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/surface.h"

#include <string.h>

static mrhiTestAdapter s_adapter;
static mrhiInstanceDriver s_made;
static mrhiInstanceDriverVtable s_vtable;

// A test driver as an outside driver's code makes it, with a copy of
// its vtable for the test to change.
static mrhiExternalDriverDef MakeDriver(void)
{
    s_adapter = (mrhiTestAdapter){
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .limits = mrhiDefaultLimits(),
    };
    mrhiTestDriverDef def = {
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = &s_adapter,
        .adapterCount = 1,
    };
    mrhiAllocator allocator = {0};
    CHECK(mrhiCreateTestDriver(&allocator, &def, 16, &s_made) == mrhi_success, "a driver");
    s_vtable = *s_made.vtable;
    return (mrhiExternalDriverDef){
        .chain = {.next = nullptr, .type = mrhi_structExternalDriver},
        .vtable = &s_vtable,
        .driver = s_made.self,
    };
}

static mrhiResult Create(const mrhiExternalDriverDef* external, mrhiInstance** instanceOut)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &external->chain;
    return mrhiCreateInstance(&def, instanceOut);
}

// Lists the instance's one adapter.
static mrhiAdapterId FindAdapter(mrhiInstance* instance)
{
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    mrhiInstanceNotification record;
    mrhiAdapterId adapter = {0};
    size_t count = 0;
    CHECK(mrhiRequestAdapters(instance, &search, &request) == mrhi_success &&
              mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
              mrhiGetAdapters(instance, &adapter, 1, &count) == mrhi_success && count == 1,
          "one adapter");
    return adapter;
}

static void TestAccepted(void)
{
    mrhiExternalDriverDef external = MakeDriver();
    mrhiInstance* instance = nullptr;
    CHECK(Create(&external, &instance) == mrhi_success, "taken");
    mrhiAdapterId adapter = FindAdapter(instance);
    mrhiAdapterInfo info;
    CHECK(mrhiGetAdapterInfo(instance, adapter, &info) == mrhi_success &&
              info.driver == mrhi_driverExternal && info.kind == mrhi_adapterDiscrete,
          "reported as external");
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    mrhiDevice* device = nullptr;
    mrhiRequestId request;
    mrhiInstanceNotification record;
    CHECK(mrhiCreateDevice(instance, &deviceDef, &device, &request) == mrhi_success &&
              mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
              record.outcome == mrhi_success,
          "a device on it");
    mrhiDestroyDevice(device);
    // The instance destroys the driver.
    mrhiDestroyInstance(instance);
}

static void TestRefused(void)
{
    mrhiExternalDriverDef external = MakeDriver();
    mrhiInstance* instance = nullptr;
    s_vtable.spiVersion = MRHI_SPI_VERSION - 1;
    CHECK(Create(&external, &instance) == mrhi_errorVersion && instance == nullptr,
          "another SPI version");
    s_vtable.spiVersion = MRHI_SPI_VERSION + 1;
    CHECK(Create(&external, &instance) == mrhi_errorVersion, "a later one");
    s_vtable = *s_made.vtable;
    s_vtable.size = sizeof(s_vtable) - 1;
    CHECK(Create(&external, &instance) == mrhi_errorInvalid, "a short vtable");
    s_vtable = *s_made.vtable;
    s_vtable.getSurfaceCaps = nullptr;
    CHECK(Create(&external, &instance) == mrhi_errorInvalid, "a missing function");
    s_vtable = *s_made.vtable;
    external.vtable = nullptr;
    CHECK(Create(&external, &instance) == mrhi_errorInvalid, "no vtable");
    external.vtable = &s_vtable;
    mrhiTestDriverDef test = {.chain = {.next = nullptr, .type = mrhi_structTestDriver}};
    external.chain.next = &test.chain;
    CHECK(Create(&external, &instance) == mrhi_errorUnsupported, "beside the test driver");
    // Still the program's.
    s_made.vtable->destroy(s_made.self);
}

// The test driver's device vtable, copied for the test to change, and
// the mismatch the next device gets.
static mrhiDeviceDriverVtable s_deviceVtable;
static int s_breaking;

static mrhiResult CreateBrokenDevice(void* self, uint64_t adapter, const mrhiDeviceDef* def,
                                     uint64_t tag, mrhiDeviceDriver* deviceOut)
{
    mrhiResult status = s_made.vtable->createDevice(self, adapter, def, tag, deviceOut);
    if (status == mrhi_success)
    {
        s_deviceVtable = *deviceOut->vtable;
        s_deviceVtable.spiVersion += s_breaking == 0 ? 1u : 0u;
        s_deviceVtable.size -= s_breaking == 1 ? 1u : 0u;
        s_deviceVtable.waitFrame = s_breaking == 2 ? nullptr : s_deviceVtable.waitFrame;
        deviceOut->vtable = &s_deviceVtable;
    }
    return status;
}

static void TestDeviceHandshake(void)
{
    mrhiExternalDriverDef external = MakeDriver();
    s_vtable.createDevice = CreateBrokenDevice;
    mrhiInstance* instance = nullptr;
    CHECK(Create(&external, &instance) == mrhi_success, "taken");
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = FindAdapter(instance);
    const mrhiResult expected[3] = {mrhi_errorVersion, mrhi_errorInvalid, mrhi_errorInvalid};
    for (s_breaking = 0; s_breaking < 3; ++s_breaking)
    {
        // The refused device is destroyed through its vtable's first
        // function.
        mrhiDevice* device = nullptr;
        mrhiRequestId request;
        CHECK(mrhiCreateDevice(instance, &deviceDef, &device, &request) == expected[s_breaking] &&
                  device == nullptr,
              "refused");
    }
    mrhiInstanceNotification record;
    CHECK(mrhiNextInstanceNotification(instance, &record) == mrhi_empty,
          "the refused devices' answers dropped");
    mrhiDestroyInstance(instance);
}

// A source struct an outside driver defines: a type with bit 30 set.
#define OWN_SOURCE (MRHI_STRUCT_DRIVER_DEFINED | 1u)

typedef struct OwnSource
{
    mrhiChain chain;
    uint32_t presentingAdapters;
} OwnSource;

// The outside driver reads its own source, here by making the test
// driver's from it, and refuses any other it cannot use.
static mrhiResult CreateOwnSurface(void* self, const mrhiChain* source, const mrhiSurfaceDef* def,
                                   uint64_t* handleOut)
{
    if (source->type != OWN_SOURCE)
    {
        return mrhi_errorUnsupported;
    }
    const OwnSource* own = (const OwnSource*)source;
    mrhiSurfaceSourceTest test = {
        .chain = {.next = nullptr, .type = mrhi_structSurfaceSourceTest},
        .presentingAdapters = own->presentingAdapters,
    };
    return s_made.vtable->createSurface(self, &test.chain, def, handleOut);
}

static mrhiResult MakeSurface(mrhiInstance* instance, const mrhiChain* source,
                              mrhiSurfaceId* surfaceOut)
{
    mrhiSurfaceDef def = mrhiDefaultSurfaceDef();
    def.next = source;
    return mrhiCreateSurface(instance, &def, surfaceOut);
}

// Driver-defined sources (mrhi-0024): an outside driver's instance hands
// its own to it unexamined; every other instance refuses them as
// unknown critical structs.
static void TestOwnSources(void)
{
    mrhiExternalDriverDef external = MakeDriver();
    s_vtable.createSurface = CreateOwnSurface;
    mrhiInstance* instance = nullptr;
    CHECK(Create(&external, &instance) == mrhi_success, "taken");
    mrhiAdapterId adapter = FindAdapter(instance);
    OwnSource own = {.chain = {.next = nullptr, .type = OWN_SOURCE}, .presentingAdapters = 1};
    mrhiSurfaceId surface = {0};
    CHECK(MakeSurface(instance, &own.chain, &surface) == mrhi_success, "its own source");
    mrhiSurfaceCaps caps;
    CHECK(mrhiGetSurfaceCaps(instance, surface, adapter, &caps) == mrhi_success && caps.presentable,
          "presenting there");
    CHECK(mrhiDestroySurface(instance, surface) == mrhi_success, "destroyed");
    own.chain.type = MRHI_STRUCT_DRIVER_DEFINED | 2u;
    CHECK(MakeSurface(instance, &own.chain, &surface) == mrhi_errorUnsupported,
          "one the driver cannot use");
    uint64_t misuse = mrhiGetInstanceMisuse(instance);
    own.chain.type = OWN_SOURCE | 0x80000000u;
    CHECK(MakeSurface(instance, &own.chain, &surface) == mrhi_errorInvalid,
          "a hint, skipped: no source");
    own.chain.type = OWN_SOURCE;
    OwnSource second = {.chain = {.next = nullptr, .type = OWN_SOURCE | 2u}};
    own.chain.next = &second.chain;
    CHECK(MakeSurface(instance, &own.chain, &surface) == mrhi_errorInvalid, "two sources");
    CHECK(mrhiGetInstanceMisuse(instance) == misuse + 2, "both counted");
    mrhiDestroyInstance(instance);
    // The test driver's own instance knows no driver-defined struct.
    mrhiTestDriverDef test = {
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = &s_adapter,
        .adapterCount = 1,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &test.chain;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "a test driver instance");
    own.chain.next = nullptr;
    CHECK(MakeSurface(instance, &own.chain, &surface) == mrhi_errorUnsupported,
          "refused as unknown");
    mrhiDestroyInstance(instance);
}

int main(void)
{
    TestAccepted();
    TestRefused();
    TestDeviceHandshake();
    TestOwnSources();
    return s_failures == 0 ? 0 : 1;
}
