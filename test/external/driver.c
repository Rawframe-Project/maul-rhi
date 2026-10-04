// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A driver built outside Maul RHI's tree against its installed headers
// alone, the SPI's among them (mrhi-0024), as a console driver would
// be: one adapter, found at the next poll, on which devices are not
// made. The program hands it to an instance, which reports the adapter
// as external, and destroys it with the instance.

#include <maul-rhi/instance.h>
#include <maul-rhi/spi/driver.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Driver
{
    uint64_t tag;
    bool answered;
    bool* destroyed;
} Driver;

static mrhiResult RequestAdapters(void* self, uint64_t tag)
{
    Driver* driver = self;
    driver->tag = tag;
    driver->answered = false;
    return mrhi_success;
}

static size_t Poll(void* self, mrhiDriverEvent* events, size_t capacity)
{
    Driver* driver = self;
    if (driver->answered || driver->tag == 0 || capacity == 0)
    {
        return 0;
    }
    driver->answered = true;
    events[0] = (mrhiDriverEvent){.tag = driver->tag, .outcome = mrhi_success};
    return 1;
}

static size_t GetAdapters(const void* self, mrhiDriverAdapter* adapters, size_t capacity)
{
    (void)self;
    if (capacity > 0)
    {
        adapters[0] = (mrhiDriverAdapter){
            .handle = 1,
            .info = {.kind = mrhi_adapterIntegrated, .name = "outside", .nameLength = 7},
            .limits = mrhiDefaultLimits(),
        };
    }
    return 1;
}

static void GetFormatCaps(const void* self, uint64_t adapter, mrhiFormat format,
                          mrhiFormatCaps* capsOut)
{
    (void)self;
    (void)adapter;
    (void)format;
    *capsOut = (mrhiFormatCaps){true, true, true, true, true, 0x7};
}

static mrhiResult CreateSurface(void* self, const mrhiChain* source, const mrhiSurfaceDef* def,
                                uint64_t* handleOut)
{
    (void)self;
    (void)source;
    (void)def;
    (void)handleOut;
    return mrhi_errorUnsupported;
}

static void DestroySurface(void* self, uint64_t handle)
{
    (void)self;
    (void)handle;
}

static void GetSurfaceCaps(const void* self, uint64_t surface, uint64_t adapter,
                           mrhiSurfaceCaps* capsOut)
{
    (void)self;
    (void)surface;
    (void)adapter;
    *capsOut = (mrhiSurfaceCaps){0};
}

static mrhiResult CreateDevice(void* self, uint64_t adapter, const mrhiDeviceDef* def, uint64_t tag,
                               mrhiDeviceDriver* deviceOut)
{
    (void)self;
    (void)adapter;
    (void)def;
    (void)tag;
    (void)deviceOut;
    return mrhi_errorUnsupported;
}

static void Destroy(void* self)
{
    Driver* driver = self;
    *driver->destroyed = true;
    free(driver);
}

static const mrhiInstanceDriverVtable s_vtable = {
    .spiVersion = MRHI_SPI_VERSION,
    .size = sizeof(mrhiInstanceDriverVtable),
    .requestAdapters = RequestAdapters,
    .poll = Poll,
    .getAdapters = GetAdapters,
    .getFormatCaps = GetFormatCaps,
    .createSurface = CreateSurface,
    .destroySurface = DestroySurface,
    .getSurfaceCaps = GetSurfaceCaps,
    .createDevice = CreateDevice,
    .destroy = Destroy,
};

static int s_failures;

static void Check(bool condition, const char* what)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", what);
        ++s_failures;
    }
}

int main(void)
{
    bool destroyed = false;
    Driver* driver = calloc(1, sizeof(Driver));
    Check(driver != nullptr, "a driver");
    if (driver == nullptr)
    {
        return 1;
    }
    driver->destroyed = &destroyed;
    mrhiExternalDriverDef external = {
        .chain = {.next = nullptr, .type = mrhi_structExternalDriver},
        .vtable = &s_vtable,
        .driver = driver,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &external.chain;
    mrhiInstance* instance = nullptr;
    Check(mrhiCreateInstance(&def, &instance) == mrhi_success, "an instance on it");
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    mrhiInstanceNotification record;
    mrhiAdapterId adapter = {0};
    size_t count = 0;
    Check(mrhiRequestAdapters(instance, &search, &request) == mrhi_success &&
              mrhiNextInstanceNotification(instance, &record) == mrhi_success &&
              mrhiGetAdapters(instance, &adapter, 1, &count) == mrhi_success && count == 1,
          "its adapter");
    mrhiAdapterInfo info;
    Check(mrhiGetAdapterInfo(instance, adapter, &info) == mrhi_success &&
              info.driver == mrhi_driverExternal && info.nameLength == 7 &&
              memcmp(info.name, "outside", 7) == 0,
          "reported as external, by its name");
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.adapter = adapter;
    mrhiDevice* device = nullptr;
    Check(mrhiCreateDevice(instance, &deviceDef, &device, &request) == mrhi_errorUnsupported,
          "the driver's refusal");
    mrhiDestroyInstance(instance);
    Check(destroyed, "destroyed with the instance");
    return s_failures == 0 ? 0 : 1;
}
