// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Instances: the def's checks, the contract version, the extension
// chain and the caller's allocator.

#include "test_harness.h"

#include "maul-rhi/instance.h"

#include <stdlib.h>

// A hint type no library defines: bit 31 set.
#define UNKNOWN_HINT 0x80000123u
// A critical type no library defines.
#define UNKNOWN_CRITICAL 0x00000123u

typedef struct CountingAllocator
{
    int allocations;
    int frees;
    bool fail;
} CountingAllocator;

static void* CountingAlloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    CountingAllocator* counts = context;
    if (counts->fail)
    {
        return nullptr;
    }
    ++counts->allocations;
    return malloc(size);
}

static void CountingFree(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    CountingAllocator* counts = context;
    ++counts->frees;
    free(memory);
}

static mrhiResult Create(const mrhiInstanceDef* def)
{
    mrhiInstance* instance = nullptr;
    mrhiResult status = mrhiCreateInstance(def, &instance);
    CHECK((status == mrhi_success) == (instance != nullptr), "an instance only on success");
    mrhiDestroyInstance(instance);
    return status;
}

static void TestDefaultDefCreates(void)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    CHECK(def.contractVersion == MRHI_CONTRACT_VERSION, "the def carries the contract version");
    CHECK(def.next == nullptr, "the def has no extensions");
    CHECK(Create(&def) == mrhi_success, "the default def creates");
    mrhiDestroyInstance(nullptr);
}

static void TestInvalidDefs(void)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    CHECK(Create(nullptr) == mrhi_errorInvalid, "a NULL def");
    CHECK(mrhiCreateInstance(&def, nullptr) == mrhi_errorInvalid, "a NULL out-parameter");
    mrhiInstanceDef zeroed = {0};
    CHECK(Create(&zeroed) == mrhi_errorInvalid, "a zeroed def");
    def.cookie ^= 1;
    CHECK(Create(&def) == mrhi_errorInvalid, "a def without its cookie");
    def = mrhiDefaultInstanceDef();
    def.limits.chainDepth = 0;
    CHECK(Create(&def) == mrhi_errorInvalid, "a zero chain depth");
    def = mrhiDefaultInstanceDef();
    def.allocator.alloc = CountingAlloc;
    CHECK(Create(&def) == mrhi_errorInvalid, "an allocator with one function");
}

static void TestFailureClearsTheOutParameter(void)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.contractVersion = 0;
    mrhiInstance* instance = (mrhiInstance*)&def;
    CHECK(mrhiCreateInstance(&def, &instance) == mrhi_errorVersion, "the failure");
    CHECK(instance == nullptr, "the out-parameter is NULL after a failure");
}

static void TestContractVersion(void)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.contractVersion = MRHI_CONTRACT_VERSION + 1;
    CHECK(Create(&def) == mrhi_errorVersion, "a newer contract");
    def.contractVersion = MRHI_CONTRACT_VERSION - 1;
    CHECK(Create(&def) == mrhi_errorVersion, "an older contract");
}

static void TestChain(void)
{
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    mrhiChain hint = {.next = nullptr, .type = UNKNOWN_HINT};
    def.next = &hint;
    CHECK(Create(&def) == mrhi_success, "an unknown hint is skipped");
    mrhiChain critical = {.next = &hint, .type = UNKNOWN_CRITICAL};
    def.next = &critical;
    CHECK(Create(&def) == mrhi_errorUnsupported, "an unknown critical struct is refused");
    mrhiChain none = {.next = nullptr, .type = mrhi_structNone};
    def.next = &none;
    CHECK(Create(&def) == mrhi_errorInvalid, "a struct without a type");
    none.type = 0x80000000u;
    CHECK(Create(&def) == mrhi_errorInvalid, "a hint without a type");
}

static void TestChainDepth(void)
{
    mrhiChain nodes[3];
    for (int i = 0; i < 3; ++i)
    {
        nodes[i] = (mrhiChain){.next = i < 2 ? &nodes[i + 1] : nullptr, .type = UNKNOWN_HINT};
    }
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &nodes[0];
    def.limits.chainDepth = 3;
    CHECK(Create(&def) == mrhi_success, "a chain as deep as the limit");
    def.limits.chainDepth = 2;
    CHECK(Create(&def) == mrhi_errorCapacity, "a chain deeper than the limit");
    nodes[2].next = &nodes[0];
    def.limits.chainDepth = 8;
    CHECK(Create(&def) == mrhi_errorCapacity, "a cycle ends at the limit");
}

static void TestAllocator(void)
{
    CountingAllocator counts = {0};
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.allocator = (mrhiAllocator){CountingAlloc, CountingFree, &counts};
    CHECK(Create(&def) == mrhi_success, "a caller allocator creates");
    CHECK(counts.allocations == 1 && counts.frees == 1, "the instance uses and returns it");
    counts.fail = true;
    CHECK(Create(&def) == mrhi_errorCapacity, "a failing allocator");
    CHECK(counts.frees == 1, "nothing to return after a failure");
}

int main(void)
{
    TestDefaultDefCreates();
    TestInvalidDefs();
    TestFailureClearsTheOutParameter();
    TestContractVersion();
    TestChain();
    TestChainDepth();
    TestAllocator();
    return s_failures == 0 ? 0 : 1;
}
