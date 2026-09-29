// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The extension chain check with struct types a def accepts.

#include "chain.h"
#include "test_harness.h"

#define KNOWN_A 0x00000011u
#define KNOWN_B 0x00000012u

static void TestKnownTypesPass(void)
{
    const mrhiStructType known[] = {KNOWN_A, KNOWN_B};
    mrhiChain b = {.next = nullptr, .type = KNOWN_B};
    mrhiChain a = {.next = &b, .type = KNOWN_A};
    CHECK(mrhiCheckChain(&a, known, 2, 8) == mrhi_success, "known critical structs pass");
    CHECK(mrhiCheckChain(&a, known, 1, 8) == mrhi_errorUnsupported, "one type not accepted");
    CHECK(mrhiCheckChain(&a, nullptr, 0, 8) == mrhi_errorUnsupported, "none accepted");
    CHECK(mrhiCheckChain(nullptr, nullptr, 0, 8) == mrhi_success, "an empty chain");
}

static void TestHintsAreSkipped(void)
{
    const mrhiStructType known[] = {KNOWN_A};
    mrhiChain hint = {.next = nullptr, .type = KNOWN_B | MRHI_CHAIN_HINT};
    mrhiChain a = {.next = &hint, .type = KNOWN_A};
    CHECK(mrhiCheckChain(&a, known, 1, 8) == mrhi_success, "an unknown hint after a known type");
    CHECK(mrhiCheckChain(&a, known, 1, 1) == mrhi_errorCapacity, "hints count toward the depth");
}

int main(void)
{
    TestKnownTypesPass();
    TestHintsAreSkipped();
    return s_failures == 0 ? 0 : 1;
}
