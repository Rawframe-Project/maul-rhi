// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The instance: the contract version check, the def's extension chain
// and the root's memory, from the def's allocator.

#include "maul-rhi/instance.h"

#include "allocator.h"
#include "chain.h"

#include <stdalign.h>

#define INSTANCE_DEF_COOKIE 0x6D72696Eu

struct mrhiInstance
{
    mrhiAllocator allocator;
    mrhiInstanceLimits limits;
};

mrhiInstanceDef mrhiDefaultInstanceDef(void)
{
    mrhiInstanceDef def = {0};
    def.cookie = INSTANCE_DEF_COOKIE;
    def.contractVersion = MRHI_CONTRACT_VERSION;
    def.limits.chainDepth = 8;
    return def;
}

static mrhiResult CheckDef(const mrhiInstanceDef* def)
{
    if (def->cookie != INSTANCE_DEF_COOKIE || def->limits.chainDepth == 0 ||
        !mrhiIsAllocatorValid(&def->allocator))
    {
        return mrhi_errorInvalid;
    }
    if (def->contractVersion != MRHI_CONTRACT_VERSION)
    {
        return mrhi_errorVersion;
    }
    return mrhiCheckChain(def->next, nullptr, 0, def->limits.chainDepth);
}

mrhiResult mrhiCreateInstance(const mrhiInstanceDef* def, mrhiInstance** instanceOut)
{
    if (instanceOut == nullptr)
    {
        return mrhi_errorInvalid;
    }
    *instanceOut = nullptr;
    if (def == nullptr)
    {
        return mrhi_errorInvalid;
    }
    mrhiResult status = CheckDef(def);
    if (status != mrhi_success)
    {
        return status;
    }
    mrhiInstance* instance =
        mrhiAllocate(&def->allocator, sizeof(mrhiInstance), alignof(mrhiInstance));
    if (instance == nullptr)
    {
        return mrhi_errorCapacity;
    }
    *instance = (mrhiInstance){.allocator = def->allocator, .limits = def->limits};
    *instanceOut = instance;
    return mrhi_success;
}

void mrhiDestroyInstance(mrhiInstance* instance)
{
    if (instance == nullptr)
    {
        return;
    }
    mrhiAllocator allocator = instance->allocator;
    mrhiRelease(&allocator, instance, sizeof(mrhiInstance), alignof(mrhiInstance));
}
