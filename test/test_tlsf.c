// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The TLSF allocator over offsets: fits, alignment, merging back to
// empty blocks, a store out of nodes, and a seeded random run checked
// for overlaps after every step.

#include "test_harness.h"
#include "tlsf.h"

#include <string.h>

#define NODES 512
#define LIVE  128

static mrhiTlsfNode s_nodes[NODES];

static void TestExact(void)
{
    mrhiTlsfNodes store;
    mrhiTlsf pool;
    mrhiTlsfNodesInit(&store, s_nodes, NODES);
    mrhiTlsfInit(&pool);
    uint32_t block = mrhiTlsfAddBlock(&pool, &store, 7, 4096);
    CHECK(block != 0 && mrhiTlsfIsBlockEmpty(&store, block), "a block");
    uint32_t id = 0;
    uint64_t offset = 1;
    uint32_t whole = mrhiTlsfAllocate(&pool, &store, 4096, 256, &id, &offset);
    CHECK(whole != 0 && id == 7 && offset == 0, "the whole block");
    CHECK(mrhiTlsfAllocate(&pool, &store, 1, 1, &id, &offset) == 0, "nothing left");
    uint32_t freed = mrhiTlsfFree(&pool, &store, whole);
    CHECK(mrhiTlsfIsBlockEmpty(&store, freed), "empty again");
    CHECK(mrhiTlsfAllocate(&pool, &store, 4097, 1, &id, &offset) == 0, "too big");
    mrhiTlsfRemoveBlock(&pool, &store, freed);
    CHECK(store.unusedCount == NODES, "every node back");
    CHECK(mrhiTlsfAllocate(&pool, &store, 1, 1, &id, &offset) == 0, "no block");
    // 1000 bytes lie inside a class, whose larger members a search skips.
    uint32_t odd = mrhiTlsfAddBlock(&pool, &store, 1, 1000);
    uint32_t exact = mrhiTlsfAllocate(&pool, &store, 1000, 8, &id, &offset);
    CHECK(exact == odd && id == 1 && offset == 0, "an exact fit inside a class");
    mrhiTlsfRemoveBlock(&pool, &store, mrhiTlsfFree(&pool, &store, exact));
}

typedef struct Range
{
    uint64_t offset;
    uint64_t size;
} Range;

static bool Apart(const Range* a, const Range* b)
{
    return a->offset + a->size <= b->offset || b->offset + b->size <= a->offset;
}

static void TestAlignmentAndMerging(void)
{
    mrhiTlsfNodes store;
    mrhiTlsf pool;
    mrhiTlsfNodesInit(&store, s_nodes, NODES);
    mrhiTlsfInit(&pool);
    (void)mrhiTlsfAddBlock(&pool, &store, 0, 1u << 20);
    uint32_t block = 0;
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t c = 0;
    uint32_t first = mrhiTlsfAllocate(&pool, &store, 100, 1, &block, &a);
    uint32_t second = mrhiTlsfAllocate(&pool, &store, 100, 4096, &block, &b);
    uint32_t third = mrhiTlsfAllocate(&pool, &store, 3, 64, &block, &c);
    CHECK(first != 0 && second != 0 && third != 0, "three");
    CHECK(b % 4096 == 0 && c % 64 == 0, "aligned");
    const Range ranges[3] = {{a, 100}, {b, 100}, {c, 3}};
    CHECK(Apart(&ranges[0], &ranges[1]) && Apart(&ranges[0], &ranges[2]) &&
              Apart(&ranges[1], &ranges[2]),
          "apart");
    (void)mrhiTlsfFree(&pool, &store, second);
    (void)mrhiTlsfFree(&pool, &store, first);
    uint32_t last = mrhiTlsfFree(&pool, &store, third);
    CHECK(mrhiTlsfIsBlockEmpty(&store, last), "merged in any order");
    mrhiTlsfRemoveBlock(&pool, &store, last);
    CHECK(store.unusedCount == NODES, "every node back");
}

// A store of two nodes: a tail it cannot split stays with the
// allocation, and skipped bytes it cannot split join the range before.
static void TestOutOfNodes(void)
{
    mrhiTlsfNode nodes[3];
    mrhiTlsfNodes store;
    mrhiTlsf pool;
    mrhiTlsfNodesInit(&store, nodes, 2);
    mrhiTlsfInit(&pool);
    uint32_t block = 0;
    uint64_t offset = 0;
    (void)mrhiTlsfAddBlock(&pool, &store, 0, 1000);
    uint32_t a = mrhiTlsfAllocate(&pool, &store, 10, 1, &block, &offset);
    CHECK(a != 0 && offset == 0 && store.unusedCount == 0, "split with the last node");
    uint32_t b = mrhiTlsfAllocate(&pool, &store, 10, 512, &block, &offset);
    CHECK(b != 0 && offset == 512, "aligned without a node");
    CHECK(nodes[a - 1].size == 512 && nodes[b - 1].size == 488, "the bytes kept, none lost");
    CHECK(mrhiTlsfAllocate(&pool, &store, 1, 1, &block, &offset) == 0, "nothing free");
    (void)mrhiTlsfFree(&pool, &store, a);
    uint32_t last = mrhiTlsfFree(&pool, &store, b);
    CHECK(mrhiTlsfIsBlockEmpty(&store, last) && nodes[last - 1].size == 1000, "whole again");
}

static uint64_t s_seed = 0x243F6A8885A308D3u;

static uint64_t Next(void)
{
    s_seed = s_seed * 6364136223846793005u + 1442695040888963407u;
    return s_seed >> 33;
}

typedef struct Live
{
    uint32_t id;
    uint32_t block;
    uint64_t offset;
    uint64_t size;
} Live;

static bool Overlaps(const Live* live, uint32_t count)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        for (uint32_t j = i + 1; j < count; ++j)
        {
            if (live[i].block == live[j].block && live[i].offset < live[j].offset + live[j].size &&
                live[j].offset < live[i].offset + live[i].size)
            {
                return true;
            }
        }
    }
    return false;
}

static void TestRandom(void)
{
    static const uint64_t sizes[3] = {1u << 16, 3u << 15, 1u << 20};
    mrhiTlsfNodes store;
    mrhiTlsf pool;
    mrhiTlsfNodesInit(&store, s_nodes, NODES);
    mrhiTlsfInit(&pool);
    for (uint32_t i = 0; i < 3; ++i)
    {
        (void)mrhiTlsfAddBlock(&pool, &store, i, sizes[i]);
    }
    Live live[LIVE];
    uint32_t count = 0;
    bool ok = true;
    for (int step = 0; step < 20000 && ok; ++step)
    {
        if (count < LIVE && (count == 0 || Next() % 3 != 0))
        {
            uint64_t size = 1 + Next() % 20000;
            uint64_t alignment = UINT64_C(1) << (Next() % 13);
            Live added = {.size = size};
            added.id =
                mrhiTlsfAllocate(&pool, &store, size, alignment, &added.block, &added.offset);
            if (added.id != 0)
            {
                ok = added.offset % alignment == 0 && added.offset + size <= sizes[added.block];
                live[count++] = added;
            }
        }
        else
        {
            uint32_t pick = (uint32_t)(Next() % count);
            (void)mrhiTlsfFree(&pool, &store, live[pick].id);
            live[pick] = live[--count];
        }
        ok = ok && !Overlaps(live, count);
    }
    CHECK(ok, "aligned, in bounds and apart at every step");
    while (count > 0)
    {
        (void)mrhiTlsfFree(&pool, &store, live[--count].id);
    }
    uint32_t empty = 0;
    for (uint32_t i = 1; i <= NODES; ++i)
    {
        if (s_nodes[i - 1].free && mrhiTlsfIsBlockEmpty(&store, i))
        {
            CHECK(s_nodes[i - 1].size == sizes[s_nodes[i - 1].block], "a whole block");
            mrhiTlsfRemoveBlock(&pool, &store, i);
            ++empty;
        }
    }
    CHECK(empty == 3 && store.unusedCount == NODES, "every block merged back");
    CHECK(pool.firstMap == 0, "no free list left");
}

int main(void)
{
    TestExact();
    TestAlignmentAndMerging();
    TestOutOfNodes();
    TestRandom();
    return s_failures == 0 ? 0 : 1;
}
