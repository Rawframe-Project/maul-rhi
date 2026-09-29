// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Shader containers built in memory for the suites that need them: a
// default container of every kind of record, sections a test may break
// before assembling, and the digest sealed over the result.

#ifndef MAUL_RHI_TEST_CONTAINER_H
#define MAUL_RHI_TEST_CONTAINER_H

#include "sha256.h"

#include "maul-rhi/shader.h"

#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTION_ROOM   4096
#define CONTAINER_ROOM 65536

enum
{
    META,
    STRINGS,
    ENTRIES,
    BINDINGS,
    INPUTS,
    OUTPUTS,
    CONSTANTS,
    SPIRV,
    WGSL,
    VARIABLES,
    DEFAULT_SECTIONS,
    // The Metal sections AddMetal appends.
    METAL_MAP = DEFAULT_SECTIONS,
    MSL,
    METALLIB,
};

typedef struct Section
{
    uint32_t type;
    uint8_t bytes[SECTION_ROOM];
    size_t size;
} Section;

static Section s_sections[65];
static uint32_t s_sectionCount;
static alignas(8) uint8_t s_container[CONTAINER_ROOM];
static size_t s_size;

static inline void Put16(uint8_t* at, uint16_t value)
{
    at[0] = (uint8_t)value;
    at[1] = (uint8_t)(value >> 8);
}

static inline void Put32(uint8_t* at, uint32_t value)
{
    Put16(at, (uint16_t)value);
    Put16(at + 2, (uint16_t)(value >> 16));
}

static inline void Put64(uint8_t* at, uint64_t value)
{
    Put32(at, (uint32_t)value);
    Put32(at + 4, (uint32_t)(value >> 32));
}

// A record of a section, which grows to hold it.
static inline uint8_t* Record(uint32_t section, uint32_t index, size_t bytes)
{
    size_t end = (index + 1) * bytes;
    if (end > SECTION_ROOM)
    {
        printf("FAIL: a record past the section's room\n");
        abort();
    }
    if (s_sections[section].size < end)
    {
        s_sections[section].size = end;
    }
    return s_sections[section].bytes + index * bytes;
}

static inline void Entry(uint32_t index, mrhiShaderStages stage, uint32_t nameOffset,
                         uint32_t nameLength)
{
    uint8_t* at = Record(ENTRIES, index, 48);
    Put32(at, stage);
    Put32(at + 4, nameOffset);
    Put32(at + 8, nameLength);
}

// Sets interface record index of a section.
static inline uint8_t* Variable(uint32_t section, uint32_t index, uint32_t location,
                                mrhiScalarType type, uint8_t components,
                                mrhiInterpolation interpolation, mrhiSampling sampling)
{
    uint8_t* at = Record(section, index, 8);
    Put32(at, location);
    at[4] = type;
    at[5] = components;
    at[6] = interpolation;
    at[7] = sampling;
    return at;
}

static inline uint8_t* Binding(uint32_t index, uint8_t table, uint16_t slot, mrhiBindingKind kind,
                               mrhiShaderStages stages)
{
    uint8_t* at = Record(BINDINGS, index, 24);
    at[0] = table;
    at[1] = kind;
    Put16(at + 2, slot);
    Put32(at + 4, stages);
    return at;
}

// The default container's sections: a vertex entry "vs" with one input
// and one output variable, a fragment entry "fs" reading it, reading
// front_facing and writing one color, a compute entry "cs" with
// workgroup storage, one binding of each kind, a constant, and minimal
// code.
static inline void Reset(void)
{
    memset(s_sections, 0, sizeof(s_sections));
    s_sectionCount = DEFAULT_SECTIONS;
    for (uint32_t i = 0; i < DEFAULT_SECTIONS; ++i)
    {
        s_sections[i].type = i + 1;
    }
    Put32(Record(META, 0, 16), 16);
    memcpy(Record(STRINGS, 0, 6), "vsfscs", 6);
    Entry(0, mrhi_stageVertex, 0, 2);
    Put16(Record(ENTRIES, 0, 48) + 26, 1);
    Put16(Record(ENTRIES, 0, 48) + 34, 1);
    Entry(1, mrhi_stageFragment, 2, 2);
    uint8_t* fragment = Record(ENTRIES, 1, 48);
    Put16(fragment + 30, 1);
    Put16(fragment + 32, 1);
    Put16(fragment + 34, 1);
    Put32(fragment + 36, mrhi_builtinFrontFacing);
    Entry(2, mrhi_stageCompute, 4, 2);
    uint8_t* compute = Record(ENTRIES, 2, 48);
    Put32(compute + 12, 8);
    Put32(compute + 16, 8);
    Put32(compute + 20, 1);
    Put32(compute + 40, 1024);
    Put64(Binding(0, 0, 0, mrhi_bindingUniformBuffer, mrhi_stageVertex | mrhi_stageFragment) + 16,
          64);
    Binding(1, 0, 1, mrhi_bindingStorageBuffer, mrhi_stageCompute);
    Binding(2, 0, 2, mrhi_bindingReadOnlyStorageBuffer, mrhi_stageVertex);
    Binding(3, 0, 3, mrhi_bindingSampler, mrhi_stageFragment)[8] = mrhi_samplerFiltering;
    uint8_t* sampled = Binding(4, 1, 0, mrhi_bindingSampledTexture, mrhi_stageFragment);
    sampled[9] = mrhi_sampleFloat;
    sampled[10] = mrhi_textureCube;
    uint8_t* storage = Binding(5, 1, 1, mrhi_bindingStorageTexture, mrhi_stageCompute);
    storage[10] = mrhi_texture2dArray;
    storage[11] = mrhi_storageWriteOnly;
    Put16(storage + 12, mrhi_formatRgba8Unorm);
    Variable(INPUTS, 0, 3, mrhi_scalarFloat32, 3, 0, 0);
    Variable(OUTPUTS, 0, 0, mrhi_scalarFloat32, 4, 0, 0);
    for (uint32_t i = 0; i < 2; ++i)
    {
        Variable(VARIABLES, i, 0, mrhi_scalarFloat32, 2, mrhi_interpolationPerspective,
                 mrhi_samplingCenter);
    }
    uint8_t* constant = Record(CONSTANTS, 0, 16);
    Put32(constant, 7);
    constant[4] = mrhi_constantFloat32;
    Put32(constant + 8, 0x3F800000u);
    uint8_t* spirv = Record(SPIRV, 0, 20);
    Put32(spirv, 0x07230203u);
    Put32(spirv + 4, 0x00010300u);
    memcpy(Record(WGSL, 0, 8), "fn x(){}", 8);
}

// Appends the Metal map and, as asked, MSL and a metallib to the default
// container: the root block at buffer 0, the three buffers at 1 to 3,
// the sampler and both textures from 0, and a source per entry.
static inline void AddMetal(bool msl, bool metallib)
{
    static const char* const sources[] = {"vertex void vs(){}", "fragment void fs(){}",
                                          "kernel void cs(){}"};
    s_sections[METAL_MAP] = (Section){.type = 11};
    Record(METAL_MAP, 0, 8)[0] = 0;
    uint32_t offset = 0;
    for (uint32_t i = 0; i < 3; ++i)
    {
        uint32_t length = (uint32_t)strlen(sources[i]);
        uint8_t* entry = Record(METAL_MAP, 0, 8 + (i + 1) * 16) + 8 + i * 16;
        Put32(entry, msl ? offset : 0);
        Put32(entry + 4, msl ? length : 0);
        entry[8] = 255;
        offset += length;
    }
    static const uint8_t indices[] = {1, 2, 3, 0, 0, 1};
    memcpy(Record(METAL_MAP, 0, 56 + sizeof(indices)) + 56, indices, sizeof(indices));
    s_sectionCount = METAL_MAP + 1;
    s_sections[s_sectionCount] = (Section){.type = 12};
    for (uint32_t i = 0, at = 0; i < 3 && msl; ++i)
    {
        size_t length = strlen(sources[i]);
        memcpy(Record(s_sectionCount, 0, at + length) + at, sources[i], length);
        at += (uint32_t)length;
    }
    s_sectionCount += msl ? 1 : 0;
    s_sections[s_sectionCount] = (Section){.type = 13};
    if (metallib)
    {
        memcpy(Record(s_sectionCount, 0, 8), "MTLB\1\0\0\0", 8);
        ++s_sectionCount;
    }
}

// Writes the digest of the container's bytes past it.
static inline void Seal(void)
{
    mrhiSha256(s_container + 48, s_size - 48, s_container + 16);
}

// Lays the sections out after the header and table, 8-byte aligned,
// and seals the container.
static inline void Assemble(void)
{
    memset(s_container, 0, sizeof(s_container));
    memcpy(s_container, "MRSC", 4);
    Put32(s_container + 4, 1);
    Put32(s_container + 48, s_sectionCount);
    size_t offset = 64 + (size_t)s_sectionCount * 24;
    for (uint32_t i = 0; i < s_sectionCount; ++i)
    {
        uint8_t* record = s_container + 64 + (size_t)i * 24;
        Put32(record, s_sections[i].type);
        Put64(record + 8, offset);
        Put64(record + 16, s_sections[i].size);
        memcpy(s_container + offset, s_sections[i].bytes, s_sections[i].size);
        offset += (s_sections[i].size + 7) & ~(size_t)7;
    }
    s_size = offset;
    Put64(s_container + 8, s_size);
    Seal();
}

#endif // MAUL_RHI_TEST_CONTAINER_H
