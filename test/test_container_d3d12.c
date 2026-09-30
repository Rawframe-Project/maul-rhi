// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The shader container's D3D12 map and DXIL (docs/contract/container.md):
// every rule broken once against the default container with AddD3d12's
// sections.

#include "container.h"
#include "test_container.h"
#include "test_harness.h"

#include <string.h>

// The offsets in the map of entry index's record, binding index's
// place, and the constant's fixed flag.
#define MAP_ENTRY(index)   (32 + (index) * 16)
#define MAP_BINDING(index) (80 + (index) * 8)
#define MAP_FIXED          128

static uint8_t* Map(void)
{
    return s_sections[s_d3d12Map].bytes;
}

static uint8_t* Dxil(void)
{
    return s_sections[s_d3d12Map + 1].bytes;
}

// Resets and adds the D3D12 sections, then sets the u32 at of the map.
static mrhiResult MapWith(size_t at, uint32_t value)
{
    Reset();
    AddD3d12();
    Put32(Map() + at, value);
    return Built();
}

static void TestParts(void)
{
    Reset();
    AddD3d12();
    Assemble();
    mrhiContainer container;
    CHECK(mrhiParseContainer(s_container, s_size, &container) == mrhi_success, "every D3D12 part");
    mrhiD3d12Entry vertex = mrhiContainerD3d12Entry(&container, 0);
    mrhiD3d12Entry compute = mrhiContainerD3d12Entry(&container, 2);
    mrhiD3d12Place info = mrhiContainerD3d12Buffer(&container, mrhiD3d12VertexInfo);
    mrhiD3d12Place texture = mrhiContainerD3d12Binding(&container, 4);
    CHECK(container.d3d12MapBytes == 129 && container.dxilBytes == 96 && vertex.vertexInfo &&
              vertex.dxilLength == 32 && !compute.vertexInfo && compute.dxilOffset == 64 &&
              info.reg == 2 && info.space == 5 &&
              mrhiContainerD3d12Buffer(&container, mrhiD3d12RootBlock).reg == 0 &&
              texture.reg == 0 && texture.space == 1 &&
              mrhiContainerD3d12Binding(&container, 3).reg == 3 &&
              !mrhiContainerD3d12Fixed(&container, 0),
          "the map read back");
    Reset();
    Assemble();
    CHECK(mrhiParseContainer(s_container, s_size, &container) == mrhi_success &&
              container.d3d12Map == nullptr && container.dxil == nullptr,
          "no D3D12 code");
    Reset();
    AddD3d12();
    DropSection(s_d3d12Map + 1);
    CHECK(Built() == mrhi_errorInvalid, "a map without DXIL");
    Reset();
    AddD3d12();
    s_sections[s_d3d12Map + 1].size = 0;
    CHECK(Built() == mrhi_errorInvalid, "a map with empty DXIL");
    Reset();
    AddD3d12();
    DropSection(s_d3d12Map);
    CHECK(Built() == mrhi_errorInvalid, "DXIL without a map");
    Reset();
    AddD3d12();
    s_sections[s_sectionCount++] = (Section){.type = 14};
    CHECK(Built() == mrhi_errorInvalid, "a repeated map");
    Reset();
    AddD3d12();
    s_sections[s_d3d12Map].size -= 1;
    CHECK(Built() == mrhi_errorInvalid, "a map short of a constant");
    s_sections[s_d3d12Map].size += 2;
    CHECK(Built() == mrhi_errorInvalid, "a map a byte long");
    Reset();
    AddD3d12();
    AddMetal(true, false);
    CHECK(Built() == mrhi_success, "D3D12 and Metal code together");
    Reset();
    Put32(Record(ENTRIES, 2, 48) + 44, mrhi_heapUseStorageBuffers);
    s_sections[WGSL].size = 0;
    CHECK(Built() == mrhi_success, "a heap without D3D12 code");
    AddD3d12();
    CHECK(Built() == mrhi_errorInvalid, "a heap the map gives no range");
    CHECK(MapWith(24, 1) == mrhi_errorInvalid, "a heap range the map has no room for");
    for (size_t at = 28; at < 32; ++at)
    {
        Reset();
        AddD3d12();
        Map()[at] = 1;
        CHECK(Built() == mrhi_errorInvalid, "a map zero that is not zero");
    }
}

static void TestEntries(void)
{
    CHECK(MapWith(MAP_ENTRY(1), 2) == mrhi_errorInvalid, "DXIL off 4-byte alignment");
    CHECK(MapWith(MAP_ENTRY(1), 64) == mrhi_success, "two entries on one DXIL");
    CHECK(MapWith(MAP_ENTRY(2), 68) == mrhi_errorInvalid, "DXIL past the section");
    CHECK(MapWith(MAP_ENTRY(2), 0xFFFFFFF0u) == mrhi_errorInvalid, "DXIL far past the section");
    CHECK(MapWith(MAP_ENTRY(2) + 4, 31) == mrhi_errorInvalid, "DXIL shorter than its header");
    CHECK(MapWith(MAP_ENTRY(2) + 4, 0) == mrhi_errorInvalid, "an entry without DXIL");
    CHECK(MapWith(MAP_ENTRY(0) + 8, 2) == mrhi_errorInvalid, "a vertex information flag of 2");
    CHECK(MapWith(MAP_ENTRY(1) + 8, 1) == mrhi_errorInvalid,
          "vertex information read by a fragment entry");
    CHECK(MapWith(MAP_ENTRY(2) + 8, 1) == mrhi_errorInvalid,
          "vertex information read by a compute entry");
    for (size_t at = 12; at < 16; ++at)
    {
        Reset();
        AddD3d12();
        Map()[MAP_ENTRY(0) + at] = 1;
        CHECK(Built() == mrhi_errorInvalid, "an entry's zero that is not zero");
    }
    Reset();
    AddD3d12();
    Dxil()[32 + 3] = 'D';
    CHECK(Built() == mrhi_errorInvalid, "DXIL without its magic");
    Reset();
    AddD3d12();
    Put32(Dxil() + 64 + 24, 36);
    CHECK(Built() == mrhi_errorInvalid, "a DXIL size past its range");
    Put32(Dxil() + 64 + 24, 28);
    CHECK(Built() == mrhi_errorInvalid, "a DXIL size short of its range");
    for (uint32_t offset = 34; offset <= 36; offset += 2)
    {
        Reset();
        AddD3d12();
        uint8_t* dxil = Record(s_d3d12Map + 1, 0, offset + 32) + offset;
        memcpy(dxil, "DXBC", 4);
        Put32(dxil + 24, 32);
        Put32(Map() + MAP_ENTRY(1), offset);
        CHECK(Built() == (offset == 36 ? mrhi_success : mrhi_errorInvalid),
              offset == 36 ? "DXIL at a 4-byte offset" : "DXIL at a 2-byte offset");
    }
    Reset();
    AddD3d12();
    Put32(Map() + MAP_ENTRY(2) + 4, 28);
    Put32(Dxil() + 64 + 24, 28);
    CHECK(Built() == mrhi_errorInvalid, "DXIL shorter than its header, as its size says");
    Reset();
    AddD3d12();
    Put32(Map() + MAP_ENTRY(1) + 4, 64);
    Put32(Dxil() + 32 + 24, 64);
    CHECK(Built() == mrhi_success, "DXIL over two containers' bytes, as its size says");
}

static void TestBuffers(void)
{
    Reset();
    Put32(Record(META, 0, 16), 0);
    AddD3d12();
    CHECK(Built() == mrhi_errorInvalid, "a place for an empty root block");
    Put32(Map() + 4, 0);
    CHECK(Built() == mrhi_success, "no root block, no place");
    Reset();
    s_sections[CONSTANTS].size = 0;
    AddD3d12();
    s_sections[s_d3d12Map].size -= 1;
    CHECK(Built() == mrhi_errorInvalid, "a place for absent constants");
    Put32(Map() + 8, 0);
    Put32(Map() + 12, 0);
    CHECK(Built() == mrhi_success, "no constants, no place");
    Reset();
    AddD3d12();
    Put32(Map() + MAP_ENTRY(0) + 8, 0);
    CHECK(Built() == mrhi_errorInvalid, "a place for vertex information nobody reads");
    Put32(Map() + 16, 0);
    Put32(Map() + 20, 0);
    CHECK(Built() == mrhi_success, "no reader, no place");
    CHECK(MapWith(4, 15) == mrhi_success, "the root block in the last space");
    CHECK(MapWith(4, 16) == mrhi_errorInvalid, "the root block in a heap's space");
    CHECK(MapWith(8, 0) == mrhi_errorInvalid, "the constants on the root block");
    CHECK(MapWith(16, 1) == mrhi_errorInvalid, "the vertex information on the constants");
    CHECK(MapWith(20, 6) == mrhi_success, "the vertex information in another space");
}

static void TestBindings(void)
{
    CHECK(MapWith(MAP_BINDING(0) + 4, 15) == mrhi_success, "a binding in the last space");
    CHECK(MapWith(MAP_BINDING(5) + 4, 16) == mrhi_errorInvalid, "a binding in a heap's space");
    Reset();
    AddD3d12();
    Put32(Map() + MAP_BINDING(0) + 4, 5);
    CHECK(Built() == mrhi_errorInvalid, "a uniform buffer on the root block");
    Put32(Map() + MAP_BINDING(0), 2);
    CHECK(Built() == mrhi_errorInvalid, "a uniform buffer on the vertex information");
    Put32(Map() + MAP_BINDING(0), 3);
    CHECK(Built() == mrhi_success, "a uniform buffer beside them");
    Reset();
    AddD3d12();
    Put32(Map() + MAP_BINDING(1), 0);
    Put32(Map() + MAP_BINDING(1) + 4, 5);
    CHECK(Built() == mrhi_success, "a storage buffer on the root block's register");
    Reset();
    AddD3d12();
    Put32(Map() + MAP_BINDING(2), 0);
    Put32(Map() + MAP_BINDING(2) + 4, 1);
    CHECK(Built() == mrhi_errorInvalid, "a read-only storage buffer on a sampled texture");
    Reset();
    AddD3d12();
    Put32(Map() + MAP_BINDING(2), 1);
    CHECK(Built() == mrhi_success, "a read-only storage buffer on a storage buffer's register");
    Reset();
    AddD3d12();
    Put32(Map() + MAP_BINDING(5), 1);
    Put32(Map() + MAP_BINDING(5) + 4, 0);
    CHECK(Built() == mrhi_errorInvalid, "a storage texture on a storage buffer");
    Reset();
    AddD3d12();
    Put32(Map() + MAP_BINDING(3), 0);
    Put32(Map() + MAP_BINDING(3) + 4, 1);
    CHECK(Built() == mrhi_success, "a sampler on a texture's register");
    Reset();
    AddD3d12();
    Put32(Map() + MAP_BINDING(4) + 4, 0);
    CHECK(Built() == mrhi_success, "a sampled texture on a uniform buffer's register");
}

static void TestConstants(void)
{
    Reset();
    AddD3d12();
    Map()[MAP_FIXED] = 1;
    Assemble();
    mrhiContainer container;
    CHECK(mrhiParseContainer(s_container, s_size, &container) == mrhi_success &&
              mrhiContainerD3d12Fixed(&container, 0),
          "a fixed constant");
    Map()[MAP_FIXED] = 2;
    CHECK(Built() == mrhi_errorInvalid, "a fixed flag of 2");
    Reset();
    uint8_t* constant = Record(CONSTANTS, 0, 16);
    Put32(constant + 8, 0);
    constant[12] = 1;
    AddD3d12();
    CHECK(Built() == mrhi_success, "a required constant read");
    Map()[MAP_FIXED] = 1;
    CHECK(Built() == mrhi_errorInvalid, "a required constant fixed");
}

// Resets and adds the D3D12 sections with heap ranges before the fixed
// flag, each a class, register and space, the compute entry reading the
// heaps as uses says.
static void AddRanges(mrhiShaderHeapUses uses, const uint32_t (*ranges)[3], uint32_t count)
{
    Reset();
    Put32(Record(ENTRIES, 2, 48) + 44, uses);
    s_sections[WGSL].size = 0;
    AddD3d12();
    uint8_t fixed = Map()[MAP_FIXED];
    Put32(Map() + 24, count);
    for (uint32_t i = 0; i < count; ++i)
    {
        uint8_t* range = Record(s_d3d12Map, 0, MAP_FIXED + (i + 1) * 16) + MAP_FIXED + i * 16;
        for (uint32_t j = 0; j < 3; ++j)
        {
            Put32(range + j * 4, ranges[i][j]);
        }
        Put32(range + 12, 0);
    }
    Record(s_d3d12Map, 0, MAP_FIXED + count * 16 + 1)[MAP_FIXED + count * 16] = fixed;
}

static void TestHeapRanges(void)
{
    const mrhiShaderHeapUses sampling = mrhi_heapUseSampledTextures | mrhi_heapUseSamplers;
    const uint32_t both[3][3] = {{0, 0, 16}, {2, 0, 17}, {1, 0, 18}};
    AddRanges(sampling | mrhi_heapUseStorageBuffers, both, 3);
    Map()[MAP_FIXED + 48] = 1;
    Assemble();
    mrhiContainer container;
    CHECK(mrhiParseContainer(s_container, s_size, &container) == mrhi_success &&
              mrhiContainerD3d12HeapRangeCount(&container) == 3 &&
              mrhiContainerD3d12HeapRange(&container, 1).rangeClass == mrhiD3d12HeapSampler &&
              mrhiContainerD3d12HeapRange(&container, 1).space == 17 &&
              mrhiContainerD3d12HeapRange(&container, 2).rangeClass == mrhiD3d12HeapStorage &&
              mrhiContainerD3d12Fixed(&container, 0),
          "heap ranges read back, the fixed flag after them");
    AddRanges(sampling, both, 2);
    CHECK(Built() == mrhi_success, "a resource range and a sampler range");
    AddRanges(mrhi_heapUseSampledTextures, both, 2);
    CHECK(Built() == mrhi_errorInvalid, "a sampler range for an entry reading no samplers");
    AddRanges(sampling, both, 1);
    CHECK(Built() == mrhi_errorInvalid, "samplers read without a sampler range");
    AddRanges(mrhi_heapUseSamplers, both + 1, 1);
    CHECK(Built() == mrhi_success, "only samplers");
    AddRanges(mrhi_heapUseSamplers, both, 2);
    CHECK(Built() == mrhi_errorInvalid, "a resource range for an entry reading no resources");
    const uint32_t cases[][2][3] = {
        {{3, 0, 16}, {2, 0, 17}},
        {{0, 0, 15}, {2, 0, 17}},
        {{0, 0, 0xFFFFFFF0u}, {2, 0, 17}},
        {{0, 1, 17}, {0, 1, 17}},
    };
    const char* names[] = {"an unknown range class", "a range below the heaps' spaces",
                           "a range in a reserved space", "two ranges on one place"};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        AddRanges(sampling, cases[i], 2);
        CHECK(Built() == mrhi_errorInvalid, names[i]);
    }
    const uint32_t shared[3][3] = {{0, 0, 16}, {1, 0, 16}, {2, 0, 16}};
    AddRanges(sampling | mrhi_heapUseStorageBuffers, shared, 3);
    CHECK(Built() == mrhi_success, "ranges of three classes on one place");
    AddRanges(sampling, both, 2);
    Map()[MAP_FIXED + 12] = 1;
    CHECK(Built() == mrhi_errorInvalid, "a range's zero that is not zero");
}

int main(void)
{
    TestParts();
    TestEntries();
    TestBuffers();
    TestBindings();
    TestConstants();
    TestHeapRanges();
    return s_failures == 0 ? 0 : 1;
}
