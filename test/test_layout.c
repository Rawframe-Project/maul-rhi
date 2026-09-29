// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Blocks laid out in parts: aligned offsets and refused overflows.

#include "allocator.h"
#include "test_harness.h"

#include <stdint.h>

static void TestPartsAreAligned(void)
{
    mrhiLayout layout = {.size = 3};
    size_t a = mrhiLayoutAdd(&layout, 2, 8, 8);
    size_t b = mrhiLayoutAdd(&layout, 3, 1, 1);
    size_t c = mrhiLayoutAdd(&layout, 1, 4, 16);
    CHECK(a == 8 && b == 24 && c == 32, "each part at its alignment");
    CHECK(layout.size == 36 && !layout.overflow, "the total");
    size_t empty = mrhiLayoutAdd(&layout, 0, 64, 64);
    CHECK(empty == 64 && layout.size == 64, "an empty part still aligns");
}

static void TestOverflowIsRefused(void)
{
    mrhiLayout layout = {.size = 16};
    CHECK(mrhiLayoutAdd(&layout, SIZE_MAX / 2, 4, 4) == 0 && layout.overflow, "a product");
    layout = (mrhiLayout){.size = SIZE_MAX - 2};
    CHECK(mrhiLayoutAdd(&layout, 1, 1, 8) == 0 && layout.overflow, "the padding");
    layout = (mrhiLayout){.size = 16};
    CHECK(mrhiLayoutAdd(&layout, 1, SIZE_MAX - 8, 1) == 0 && layout.overflow, "the sum");
    CHECK(mrhiLayoutAdd(&layout, 1, 1, 1) == 0 && layout.overflow, "it stays overflowed");
}

int main(void)
{
    TestPartsAreAligned();
    TestOverflowIsRefused();
    return s_failures == 0 ? 0 : 1;
}
