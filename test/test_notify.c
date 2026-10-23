// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Request ids: never zero, even when the counter wraps.

#include "instance_core.h"
#include "test_harness.h"

static void TestRequestIdsSkipZero(void)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    mrhiInstance* instance = nullptr;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_success, "the instance");
    instance->nextRequest = UINT32_MAX - 1;
    CHECK(mrhiNextRequest(instance) == UINT32_MAX, "the last id");
    CHECK(mrhiNextRequest(instance) == 1, "zero is skipped");
    mrhiDestroyInstance(instance);
}

int main(void)
{
    TestRequestIdsSkipZero();
    return s_failures == 0 ? 0 : 1;
}
