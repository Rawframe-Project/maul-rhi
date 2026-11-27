// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Indirect dispatches past the workgroup limit doing nothing on Vulkan,
// which leaves such a dispatch undefined, as WebGPU's
// dispatchWorkgroupsIndirect does (guarded_dispatch.h), under the
// validation layer with synchronization validation on CI. Skipped where
// there is no Vulkan adapter, unless MAUL_RHI_REQUIRE_VULKAN is set.

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "guarded_dispatch.h"
#include "test_harness.h"

#include "maul-rhi/instance.h"

#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "an instance");
    mrhiAdapterRequestDef request = mrhiDefaultAdapterRequestDef();
    request.allowSoftware = true;
    mrhiRequestId searched = {0};
    mrhiInstanceNotification record;
    mrhiAdapterId adapter = {0};
    size_t count = 0;
    bool found = instance != nullptr &&
                 mrhiInstance_RequestAdapters(instance, &request, &searched) == mrhi_success &&
                 mrhiInstance_TakeNotification(instance, &record) == mrhi_success &&
                 mrhiInstance_GetAdapters(instance, &adapter, 1, &count) == mrhi_success &&
                 count >= 1;
    mrhiDevice* device = nullptr;
    if (found)
    {
        mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
        deviceDef.adapter = adapter;
        mrhiRequestId opened = {0};
        CHECK(mrhiCreateDevice(instance, &deviceDef, &device, &opened) == mrhi_success &&
                  mrhiInstance_TakeNotification(instance, &record) == mrhi_success &&
                  record.outcome == mrhi_success,
              "a device");
    }
    if (device != nullptr)
    {
        CheckGuardedDispatch(device);
        CheckGuardedTables(device);
        CheckGuardedIndices(device);
        mrhiDestroyDevice(device);
    }
    else
    {
        const char* required = getenv("MAUL_RHI_REQUIRE_VULKAN");
        CHECK(required == nullptr || required[0] == '\0', "Vulkan where required");
        printf("skip: no Vulkan adapter\n");
    }
    mrhiDestroyInstance(instance);
    return s_failures == 0 ? 0 : 1;
}
