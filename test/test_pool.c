// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The id pool: slots taken and returned, stale ids refused, a full pool
// refused, and generations that wrap without naming a free slot.

#include "pool.h"
#include "test_harness.h"

static void TestAcquireAndRelease(void)
{
    uint32_t generations[3];
    uint32_t next[3];
    mrhiPool pool;
    mrhiPoolInit(&pool, 3, generations, next);
    uint32_t a = 0;
    uint32_t b = 0;
    uint32_t c = 0;
    uint32_t ga = 0;
    uint32_t gb = 0;
    uint32_t gc = 0;
    CHECK(mrhiPoolAcquire(&pool, &a, &ga) && mrhiPoolAcquire(&pool, &b, &gb) &&
              mrhiPoolAcquire(&pool, &c, &gc),
          "three slots");
    CHECK(a != 0 && b != 0 && c != 0 && a != b && b != c && a != c, "three distinct, none null");
    uint32_t d = 0;
    uint32_t gd = 0;
    CHECK(!mrhiPoolAcquire(&pool, &d, &gd), "a full pool refuses");
    CHECK(mrhiPoolIsLive(&pool, b, gb), "b is live");
    mrhiPoolRelease(&pool, b);
    CHECK(!mrhiPoolIsLive(&pool, b, gb), "b's id ends");
    CHECK(mrhiPoolAcquire(&pool, &d, &gd) && d == b && gd != gb, "the slot again, newer");
    CHECK(!mrhiPoolIsLive(&pool, b, gb) && mrhiPoolIsLive(&pool, d, gd), "old stale, new live");
}

static void TestIdsOutsideThePool(void)
{
    uint32_t generations[2];
    uint32_t next[2];
    mrhiPool pool;
    mrhiPoolInit(&pool, 2, generations, next);
    uint32_t index = 0;
    uint32_t generation = 0;
    CHECK(mrhiPoolAcquire(&pool, &index, &generation), "one");
    CHECK(!mrhiPoolIsLive(&pool, 0, generation), "the null index");
    CHECK(!mrhiPoolIsLive(&pool, 3, 1), "past the capacity");
    CHECK(!mrhiPoolIsLive(&pool, 2, 0), "a free slot's even generation");
    CHECK(!mrhiPoolIsLive(&pool, index, generation + 1), "another generation");
}

static void TestGenerationsWrapOdd(void)
{
    uint32_t generations[1];
    uint32_t next[1];
    mrhiPool pool;
    mrhiPoolInit(&pool, 1, generations, next);
    generations[0] = UINT32_MAX - 1;
    uint32_t index = 0;
    uint32_t generation = 0;
    CHECK(mrhiPoolAcquire(&pool, &index, &generation) && generation == UINT32_MAX, "odd");
    mrhiPoolRelease(&pool, index);
    CHECK(mrhiPoolAcquire(&pool, &index, &generation) && generation == 1, "wrapped, still odd");
}

static void TestEmptyPool(void)
{
    mrhiPool pool;
    mrhiPoolInit(&pool, 0, nullptr, nullptr);
    uint32_t index = 0;
    uint32_t generation = 0;
    CHECK(!mrhiPoolAcquire(&pool, &index, &generation), "nothing to take");
}

int main(void)
{
    TestAcquireAndRelease();
    TestIdsOutsideThePool();
    TestGenerationsWrapOdd();
    TestEmptyPool();
    return s_failures == 0 ? 0 : 1;
}
