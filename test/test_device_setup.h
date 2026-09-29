// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A device on the test driver for suites that test what a device owns:
// one discrete adapter at the floor, found and opened.

#ifndef MAUL_RHI_TEST_DEVICE_SETUP_H
#define MAUL_RHI_TEST_DEVICE_SETUP_H

#include "test_harness.h"

#include "maul-rhi/test.h"

// The adapter the next Open describes; tests may change it first.
static mrhiTestAdapter s_adapter;
static mrhiTestDriverDef s_driver;
static mrhiInstance* s_instance;

// Resets s_adapter to a discrete adapter at the floor.
static void ResetAdapter(void)
{
    s_adapter = (mrhiTestAdapter){
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .limits = mrhiDefaultLimits(),
    };
}

// Opens a device on s_adapter with the def's device limits and grants;
// ready when asked, else still opening.
static mrhiDevice* OpenWith(mrhiDeviceDef deviceDef, bool ready)
{
    s_driver = (mrhiTestDriverDef){
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = &s_adapter,
        .adapterCount = 1,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &s_driver.chain;
    CHECK(mrhiCreateInstance(&def, &s_instance) == mrhi_success, "the instance");
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    mrhiInstanceNotification record;
    CHECK(mrhiRequestAdapters(s_instance, &search, &request) == mrhi_success, "the search");
    CHECK(mrhiNextInstanceNotification(s_instance, &record) == mrhi_success, "found");
    size_t count = 0;
    CHECK(mrhiGetAdapters(s_instance, &deviceDef.adapter, 1, &count) == mrhi_success, "one");
    mrhiDevice* device = nullptr;
    CHECK(mrhiCreateDevice(s_instance, &deviceDef, &device, &request) == mrhi_success, "made");
    if (ready)
    {
        CHECK(mrhiNextInstanceNotification(s_instance, &record) == mrhi_success, "ready");
    }
    return device;
}

static void Close(mrhiDevice* device)
{
    mrhiDestroyDevice(device);
    mrhiDestroyInstance(s_instance);
    ResetAdapter();
}

#endif // MAUL_RHI_TEST_DEVICE_SETUP_H
