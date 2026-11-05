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

// Counts a layout is sized by: exact while they fit size_t or 32 bits,
// and 0 with the layout overflowed past them, which stays so, every
// count after it 0.
static void TestCounts(void)
{
    mrhiLayout layout = {0};
    CHECK(mrhiLayoutTimes(&layout, SIZE_MAX, 1) == SIZE_MAX &&
              mrhiLayoutPlus(&layout, SIZE_MAX - 1, 1) == SIZE_MAX && !layout.overflow,
          "the largest counts of size_t");
    CHECK(mrhiLayoutTimes32(&layout, 65535, 65537) == UINT32_MAX &&
              mrhiLayoutPlus32(&layout, UINT32_MAX - 1, 1) == UINT32_MAX && !layout.overflow,
          "the largest counts of 32 bits");
    CHECK(mrhiLayoutTimes(&layout, SIZE_MAX, 2) == 0 && layout.overflow, "a product past size_t");
    layout = (mrhiLayout){0};
    CHECK(mrhiLayoutPlus(&layout, SIZE_MAX, 1) == 0 && layout.overflow, "a sum past size_t");
    layout = (mrhiLayout){0};
    CHECK(mrhiLayoutTimes32(&layout, 65536, 65536) == 0 && layout.overflow,
          "a product past 32 bits");
    layout = (mrhiLayout){0};
    CHECK(mrhiLayoutPlus32(&layout, UINT32_MAX, 1) == 0 && layout.overflow, "a sum past 32 bits");
    CHECK(mrhiLayoutPlus32(&layout, 1, 1) == 0 && layout.overflow,
          "it stays overflowed, its counts 0");
    // Two 32-bit counts' product fits a 64-bit size_t, not a 32-bit one.
    layout = (mrhiLayout){0};
    CHECK((uint64_t)mrhiLayoutTimes(&layout, 65536, 65536) ==
                  (SIZE_MAX > UINT32_MAX ? (uint64_t)1 << 32 : 0) &&
              layout.overflow == (SIZE_MAX == UINT32_MAX),
          "two 32-bit counts");
}

int main(void)
{
    TestPartsAreAligned();
    TestOverflowIsRefused();
    TestCounts();
    return s_failures == 0 ? 0 : 1;
}
