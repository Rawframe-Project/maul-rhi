// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Shader containers: every rule of docs/contract/container.md broken
// once against a container built here, the shaders a device makes from
// them, and, given a file tools/mrhi_container.py wrote and what it
// should hold, a check that the library reads it the same way.

// fopen is standard C; MSVC's runtime deprecates it for its own.
#define _CRT_SECURE_NO_WARNINGS

#include "capabilities_core.h"
#include "container.h"
#include "test_container.h"
#include "test_device_setup.h"

#include "maul-rhi/shader.h"

#include <stdlib.h>
#include <string.h>

static void TestDefault(void)
{
    Reset();
    Assemble();
    mrhiContainer container;
    CHECK(mrhiParseContainer(s_container, s_size, &container) == mrhi_success, "it parses");
    CHECK(memcmp(container.digest, s_container + 16, MRHI_DIGEST_BYTES) == 0, "its digest");
    CHECK(container.rootBlockBytes == 16 && container.entryCount == 3 &&
              container.bindingCount == 6 && container.inputCount == 1 &&
              container.outputCount == 1 && container.variableCount == 2 &&
              container.constantCount == 1 && !container.float16 &&
              container.builtins == mrhi_builtinFrontFacing && container.spirvBytes == 20 &&
              container.wgslBytes == 8 && container.stringBytes == 6,
          "its counts");
    mrhiShaderEntry compute = mrhiContainerEntry(&container, 2);
    CHECK(compute.stage == mrhi_stageCompute && compute.nameOffset == 4 &&
              compute.nameLength == 2 && compute.workgroup[0] == 8 && compute.workgroup[2] == 1 &&
              compute.workgroupStorageBytes == 1024,
          "an entry");
    mrhiShaderEntry vertex = mrhiContainerEntry(&container, 0);
    CHECK(vertex.inputCount == 1 && vertex.outputCount == 0 && vertex.variableCount == 1,
          "the vertex entry's input and output");
    mrhiShaderEntry fragment = mrhiContainerEntry(&container, 1);
    CHECK(fragment.firstVariable == 1 && fragment.variableCount == 1 &&
              fragment.builtins == mrhi_builtinFrontFacing,
          "the fragment entry's input and builtin");
    mrhiShaderBinding uniform = mrhiContainerBinding(&container, 0);
    CHECK(uniform.kind == mrhi_bindingUniformBuffer && uniform.minSize == 64 &&
              uniform.stages == (mrhi_stageVertex | mrhi_stageFragment),
          "a buffer binding");
    mrhiShaderBinding storage = mrhiContainerBinding(&container, 5);
    CHECK(storage.table == 1 && storage.slot == 1 && storage.access == mrhi_storageWriteOnly &&
              storage.format == mrhi_formatRgba8Unorm &&
              storage.viewDimension == mrhi_texture2dArray,
          "a storage texture binding");
    CHECK(mrhiContainerInput(&container, 0).location == 3 &&
              mrhiContainerInput(&container, 0).type == mrhi_scalarFloat32 &&
              mrhiContainerInput(&container, 0).components == 3,
          "the input");
    CHECK(mrhiContainerOutput(&container, 0).components == 4, "the output");
    mrhiShaderVariable variable = mrhiContainerVariable(&container, 1);
    CHECK(variable.type == mrhi_scalarFloat32 && variable.components == 2 &&
              variable.interpolation == mrhi_interpolationPerspective &&
              variable.sampling == mrhi_samplingCenter,
          "an inter-stage variable");
    mrhiShaderConstant constant = mrhiContainerConstant(&container, 0);
    CHECK(constant.id == 7 && constant.type == mrhi_constantFloat32 && constant.bits == 0x3F800000u,
          "the constant");
    uint32_t optional[] = {BINDINGS, INPUTS, OUTPUTS, CONSTANTS, VARIABLES};
    for (size_t i = 0; i < sizeof(optional) / sizeof(optional[0]); ++i)
    {
        Reset();
        // The counts naming records of the section go first.
        Put16(Record(ENTRIES, 0, 48) + 26, optional[i] == INPUTS ? 0 : 1);
        Put16(Record(ENTRIES, 1, 48) + 30, optional[i] == OUTPUTS ? 0 : 1);
        Put16(Record(ENTRIES, 0, 48) + 34, optional[i] == VARIABLES ? 0 : 1);
        Put16(Record(ENTRIES, 1, 48) + 34, optional[i] == VARIABLES ? 0 : 1);
        Put16(Record(ENTRIES, 1, 48) + 32, optional[i] == VARIABLES ? 0 : 1);
        DropSection(optional[i]);
        CHECK(Built() == mrhi_success, "an optional section left out");
    }
    Reset();
    s_sections[s_sectionCount++] = (Section){.type = 100, .size = 3};
    s_sections[s_sectionCount++] = (Section){.type = 100, .size = 0};
    CHECK(Built() == mrhi_success, "unknown sections skipped, even repeated");
}

static void TestHeader(void)
{
    Reset();
    Assemble();
    CHECK(mrhiParseContainer(s_container, 63, &(mrhiContainer){0}) == mrhi_errorInvalid,
          "shorter than a header");
    s_container[0] = 'm';
    CHECK(Parse() == mrhi_errorInvalid, "another magic");
    Assemble();
    Put32(s_container + 4, 2);
    CHECK(Parse() == mrhi_errorVersion, "another version");
    Assemble();
    Put32(s_container + 4, 0);
    CHECK(Parse() == mrhi_errorVersion, "version 0");
    Assemble();
    Put64(s_container + 8, s_size + 8);
    CHECK(Parse() == mrhi_errorInvalid, "a size past the bytes");
    Assemble();
    CHECK(mrhiParseContainer(s_container, s_size - 8, &(mrhiContainer){0}) == mrhi_errorInvalid,
          "fewer bytes than its size");
    Assemble();
    s_container[s_size - 1] ^= 1;
    CHECK(Parse() == mrhi_errorInvalid, "a damaged byte");
    Assemble();
    s_container[20] ^= 0x80;
    CHECK(Parse() == mrhi_errorInvalid, "a damaged digest");
    for (size_t at = 52; at < 64; at += 4)
    {
        Assemble();
        s_container[at] = 1;
        Seal();
        CHECK(Parse() == mrhi_errorInvalid, "a header zero that is not zero");
    }
    Assemble();
    Put32(s_container + 48, 65);
    Seal();
    CHECK(Parse() == mrhi_errorInvalid, "too many sections");
    Assemble();
    Put32(s_container + 48, 30);
    Seal();
    CHECK(Parse() == mrhi_errorInvalid, "a table past the end");
}

// Sets section index's offset, or size, in the assembled table.
static void Move(uint32_t index, uint64_t offset, uint64_t size)
{
    Put64(s_container + 64 + (size_t)index * 24 + 8, offset);
    Put64(s_container + 64 + (size_t)index * 24 + 16, size);
    Seal();
}

static uint64_t OffsetOf(uint32_t index)
{
    const uint8_t* at = s_container + 64 + (size_t)index * 24 + 8;
    uint64_t value = 0;
    for (int i = 7; i >= 0; --i)
    {
        value = value << 8 | at[i];
    }
    return value;
}

static void TestSections(void)
{
    Reset();
    Assemble();
    Move(WGSL, OffsetOf(WGSL) + 4, 4);
    CHECK(Parse() == mrhi_errorInvalid, "a misaligned section");
    Assemble();
    Move(META, 64, 16);
    CHECK(Parse() == mrhi_errorInvalid, "a section in the table");
    Reset();
    s_sections[s_sectionCount++] = (Section){.type = 100};
    Assemble();
    Move(s_sectionCount - 1, 64 + (uint64_t)s_sectionCount * 24 - 8, 0);
    CHECK(Parse() == mrhi_errorInvalid, "an empty section in the table's last record");
    Move(s_sectionCount - 1, s_size, 0);
    CHECK(Parse() == mrhi_success, "an empty section at the end");
    s_sections[s_sectionCount - 1].size = 8;
    Assemble();
    Move(s_sectionCount - 1, OffsetOf(s_sectionCount - 1), 9);
    CHECK(Parse() == mrhi_errorInvalid, "an unknown section a byte past the end");
    Move(s_sectionCount - 1, OffsetOf(s_sectionCount - 1), 0 - OffsetOf(s_sectionCount - 1));
    CHECK(Parse() == mrhi_errorInvalid, "an unknown section whose end wraps to 0");
    Reset();
    while (s_sectionCount < 64)
    {
        s_sections[s_sectionCount++] = (Section){.type = 100};
    }
    CHECK(Built() == mrhi_success, "64 sections");
    s_sections[s_sectionCount++] = (Section){.type = 100};
    CHECK(Built() == mrhi_errorInvalid, "65 sections");
    Reset();
    Assemble();
    uint32_t fit = (uint32_t)((s_size - 64) / 24);
    Put32(s_container + 48, fit + 1);
    Seal();
    CHECK(Parse() == mrhi_errorInvalid, "a table a record past the end");
    Assemble();
    Move(WGSL, OffsetOf(SPIRV), 8);
    CHECK(Parse() == mrhi_errorInvalid, "overlapping sections");
    Assemble();
    Move(WGSL, OffsetOf(WGSL), 9);
    CHECK(Parse() == mrhi_errorInvalid, "a section a byte past the end");
    Assemble();
    uint8_t first[24];
    memcpy(first, s_container + 64, 24);
    memmove(s_container + 64, s_container + 88, 24);
    memcpy(s_container + 88, first, 24);
    Seal();
    CHECK(Parse() == mrhi_success, "touching sections listed out of order");
    Assemble();
    Move(WGSL, OffsetOf(WGSL), UINT64_MAX);
    CHECK(Parse() == mrhi_errorInvalid, "a size that wraps");
    Assemble();
    s_container[64 + 4] = 1;
    Seal();
    CHECK(Parse() == mrhi_errorInvalid, "a table zero that is not zero");
    Reset();
    s_sections[s_sectionCount++] = s_sections[CONSTANTS];
    CHECK(Built() == mrhi_errorInvalid, "a known section twice");
    Reset();
    s_sections[s_sectionCount++] = s_sections[WGSL];
    CHECK(Built() == mrhi_errorInvalid, "the last known section twice");
    Reset();
    s_sections[s_sectionCount++] = (Section){.type = 100, .size = 8};
    Assemble();
    Move(s_sectionCount - 1, OffsetOf(WGSL), 8);
    CHECK(Parse() == mrhi_errorInvalid, "an unknown section overlapping");
    uint32_t required[] = {META, STRINGS, ENTRIES, SPIRV, WGSL};
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i)
    {
        Reset();
        DropSection(required[i]);
        CHECK(Built() == mrhi_errorInvalid, "a required section left out");
    }
    Reset();
    s_sections[ENTRIES].size = 0;
    CHECK(Built() == mrhi_errorInvalid, "no entries");
    uint32_t arrays[] = {ENTRIES, BINDINGS, INPUTS, OUTPUTS, CONSTANTS, VARIABLES};
    for (size_t i = 0; i < sizeof(arrays) / sizeof(arrays[0]); ++i)
    {
        Reset();
        s_sections[arrays[i]].size += 4;
        CHECK(Built() == mrhi_errorInvalid, "a part of a record");
    }
}

static void TestMeta(void)
{
    Reset();
    s_sections[META].size = 12;
    CHECK(Built() == mrhi_errorInvalid, "a short meta");
    Reset();
    Put32(s_sections[META].bytes, 18);
    CHECK(Built() == mrhi_errorInvalid, "a root block not a multiple of 4");
    Reset();
    Put32(s_sections[META].bytes, 260);
    CHECK(Built() == mrhi_errorInvalid, "a root block past 256");
    Reset();
    Put32(s_sections[META].bytes, 256);
    CHECK(Built() == mrhi_success, "a root block of 256");
    Reset();
    s_sections[META].bytes[15] = 1;
    CHECK(Built() == mrhi_errorInvalid, "a meta zero that is not zero");
}

static void TestEntries(void)
{
    mrhiShaderStages stages[] = {0, mrhi_stageVertex | mrhi_stageFragment, 8};
    for (size_t i = 0; i < sizeof(stages) / sizeof(stages[0]); ++i)
    {
        Reset();
        Put32(Record(ENTRIES, 2, 48), stages[i]);
        CHECK(Built() == mrhi_errorInvalid, "not one stage");
    }
    Reset();
    Entry(0, mrhi_stageVertex, 0, 0);
    CHECK(Built() == mrhi_errorInvalid, "an empty name");
    Reset();
    Entry(0, mrhi_stageVertex, 5, 2);
    CHECK(Built() == mrhi_errorInvalid, "a name past the strings");
    Reset();
    Entry(0, mrhi_stageVertex, UINT32_MAX, 2);
    CHECK(Built() == mrhi_errorInvalid, "a name offset that wraps");
    Reset();
    Entry(0, mrhi_stageVertex, 2, 2);
    CHECK(Built() == mrhi_errorInvalid, "a repeated name");
    Reset();
    Entry(1, mrhi_stageFragment, 0, 1);
    CHECK(Built() == mrhi_success, "a name that is the start of one before it");
    Reset();
    s_sections[STRINGS].bytes[0] = 0xFF;
    CHECK(Built() == mrhi_errorInvalid, "a name that is not UTF-8");
    Reset();
    s_sections[STRINGS].bytes[1] = 0;
    CHECK(Built() == mrhi_errorInvalid, "a name with NUL");
    Reset();
    memset(Record(STRINGS, 0, 263) + 6, 'a', 257);
    Entry(0, mrhi_stageVertex, 6, 257);
    CHECK(Built() == mrhi_errorInvalid, "a name past 256 bytes");
    Entry(0, mrhi_stageVertex, 6, 256);
    CHECK(Built() == mrhi_success, "a name of 256 bytes");
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        Reset();
        Put32(Record(ENTRIES, 2, 48) + 12 + axis * 4, 0);
        CHECK(Built() == mrhi_errorInvalid, "a compute entry with no workgroup");
        Reset();
        Put32(Record(ENTRIES, 1, 48) + 12 + axis * 4, 1);
        CHECK(Built() == mrhi_errorInvalid, "a fragment entry with a workgroup");
    }
    Reset();
    Put16(Record(ENTRIES, 1, 48) + 26, 1);
    CHECK(Built() == mrhi_errorInvalid, "inputs on a fragment entry");
    Reset();
    Put16(Record(ENTRIES, 0, 48) + 30, 1);
    CHECK(Built() == mrhi_errorInvalid, "outputs on a vertex entry");
    Reset();
    Put16(Record(ENTRIES, 0, 48) + 24, 1);
    CHECK(Built() == mrhi_errorInvalid, "inputs past their section");
    Reset();
    Put16(Record(ENTRIES, 1, 48) + 28, UINT16_MAX);
    CHECK(Built() == mrhi_errorInvalid, "outputs far past their section");
}

// Resets, then sets byte at of interface record index in section.
static mrhiResult VariableWith(uint32_t section, uint32_t index, size_t at, uint8_t value)
{
    Reset();
    Record(section, index, 8)[at] = value;
    return Built();
}

static void TestInterfaces(void)
{
    CHECK(VariableWith(INPUTS, 0, 4, mrhi_scalarNone) == mrhi_errorInvalid, "an input of no type");
    CHECK(VariableWith(INPUTS, 0, 4, mrhi_scalarUint32 + 1) == mrhi_errorInvalid,
          "an input of an unknown type");
    CHECK(VariableWith(INPUTS, 0, 4, mrhi_scalarFloat16) == mrhi_errorInvalid,
          "a 16-bit float input");
    CHECK(VariableWith(INPUTS, 0, 4, mrhi_scalarUint32) == mrhi_success, "an unsigned input");
    CHECK(VariableWith(INPUTS, 0, 5, 0) == mrhi_errorInvalid, "an input of no components");
    CHECK(VariableWith(INPUTS, 0, 5, 5) == mrhi_errorInvalid, "an input of five components");
    CHECK(VariableWith(INPUTS, 0, 5, 4) == mrhi_success, "an input of four components");
    CHECK(VariableWith(INPUTS, 0, 6, 1) == mrhi_errorInvalid, "an interpolated input");
    CHECK(VariableWith(INPUTS, 0, 7, 1) == mrhi_errorInvalid, "a sampled input");
    CHECK(VariableWith(INPUTS, 1, 4, mrhi_scalarNone) == mrhi_errorInvalid,
          "an input no entry names, of no type");
    Reset();
    Variable(INPUTS, 1, 3, mrhi_scalarUint32, 1, 0, 0);
    Put16(Record(ENTRIES, 0, 48) + 26, 2);
    CHECK(Built() == mrhi_errorInvalid, "a repeated input location");
    Put32(Record(INPUTS, 1, 8), 4);
    CHECK(Built() == mrhi_success, "two input locations");

    CHECK(VariableWith(OUTPUTS, 0, 4, mrhi_scalarNone) == mrhi_errorInvalid,
          "an output of no type");
    CHECK(VariableWith(OUTPUTS, 0, 5, 5) == mrhi_errorInvalid, "an output of five components");
    CHECK(VariableWith(OUTPUTS, 0, 6, 1) == mrhi_errorInvalid, "an interpolated output");
    CHECK(VariableWith(OUTPUTS, 0, 7, 1) == mrhi_errorInvalid, "a sampled output");
    Reset();
    Record(OUTPUTS, 0, 8)[4] = mrhi_scalarFloat16;
    Assemble();
    mrhiContainer container;
    CHECK(mrhiParseContainer(s_container, s_size, &container) == mrhi_success && container.float16,
          "a 16-bit float output, noted");
    Reset();
    Put32(Record(OUTPUTS, 0, 8), MRHI_COLOR_TARGETS);
    CHECK(Built() == mrhi_errorInvalid, "an output past the color targets");
    Put32(Record(OUTPUTS, 0, 8), MRHI_COLOR_TARGETS - 1);
    CHECK(Built() == mrhi_success, "the last color target");
    Reset();
    Variable(OUTPUTS, 1, MRHI_COLOR_TARGETS, mrhi_scalarFloat32, 4, 0, 0);
    CHECK(Built() == mrhi_errorInvalid, "an output no entry names, past the targets");
    Reset();
    Variable(OUTPUTS, 1, 0, mrhi_scalarSint32, 4, 0, 0);
    Put16(Record(ENTRIES, 1, 48) + 30, 2);
    CHECK(Built() == mrhi_errorInvalid, "a repeated output location");
    Put32(Record(OUTPUTS, 1, 8), 1);
    CHECK(Built() == mrhi_success, "two output locations");
}

// Resets, then gives inter-stage variable 1 a type and interpolation.
static mrhiResult Interpolated(mrhiScalarType type, mrhiInterpolation interpolation,
                               mrhiSampling sampling)
{
    Reset();
    Variable(VARIABLES, 1, 0, type, 1, interpolation, sampling);
    return Built();
}

static void TestVariables(void)
{
    CHECK(Interpolated(mrhi_scalarFloat16, mrhi_interpolationPerspective, mrhi_samplingSample) ==
              mrhi_success,
          "a 16-bit float sampled per sample");
    CHECK(Interpolated(mrhi_scalarFloat32, mrhi_interpolationLinear, mrhi_samplingCentroid) ==
              mrhi_success,
          "linear at the centroid");
    CHECK(Interpolated(mrhi_scalarSint32, mrhi_interpolationFlat, mrhi_samplingFirst) ==
              mrhi_success,
          "a flat integer from the first vertex");
    CHECK(Interpolated(mrhi_scalarUint32, mrhi_interpolationFlat, mrhi_samplingEither) ==
              mrhi_success,
          "a flat integer from either vertex");
    CHECK(Interpolated(mrhi_scalarSint32, mrhi_interpolationPerspective, mrhi_samplingCenter) ==
              mrhi_errorInvalid,
          "an interpolated signed integer");
    CHECK(Interpolated(mrhi_scalarUint32, mrhi_interpolationLinear, mrhi_samplingCenter) ==
              mrhi_errorInvalid,
          "an interpolated unsigned integer");
    CHECK(Interpolated(mrhi_scalarFloat32, mrhi_interpolationFlat, mrhi_samplingCenter) ==
              mrhi_errorInvalid,
          "flat at the center");
    CHECK(Interpolated(mrhi_scalarFloat32, mrhi_interpolationPerspective, mrhi_samplingFirst) ==
              mrhi_errorInvalid,
          "perspective from the first vertex");
    CHECK(Interpolated(mrhi_scalarFloat32, mrhi_interpolationLinear, mrhi_samplingNone) ==
              mrhi_errorInvalid,
          "linear with no sampling");
    CHECK(Interpolated(mrhi_scalarFloat32, mrhi_interpolationNone, mrhi_samplingCenter) ==
              mrhi_errorInvalid,
          "no interpolation");
    CHECK(Interpolated(mrhi_scalarFloat32, mrhi_interpolationFlat + 1, mrhi_samplingFirst) ==
              mrhi_errorInvalid,
          "an unknown interpolation");
    CHECK(Interpolated(mrhi_scalarFloat32, mrhi_interpolationFlat, mrhi_samplingEither + 1) ==
              mrhi_errorInvalid,
          "an unknown sampling");
    CHECK(VariableWith(VARIABLES, 1, 5, 0) == mrhi_errorInvalid, "a variable of no components");
    CHECK(VariableWith(VARIABLES, 1, 4, mrhi_scalarNone) == mrhi_errorInvalid,
          "a variable of no type");
    Reset();
    Variable(VARIABLES, 2, 0, mrhi_scalarFloat32, 1, mrhi_interpolationLinear, mrhi_samplingCenter);
    Put16(Record(ENTRIES, 1, 48) + 34, 2);
    CHECK(Built() == mrhi_errorInvalid, "a repeated variable location");
    Put32(Record(VARIABLES, 2, 8), 9);
    CHECK(Built() == mrhi_success, "two variable locations");
    Reset();
    Put16(Record(ENTRIES, 1, 48) + 32, 2);
    CHECK(Built() == mrhi_errorInvalid, "variables past their section");
    Reset();
    Put16(Record(ENTRIES, 2, 48) + 34, 1);
    CHECK(Built() == mrhi_errorInvalid, "variables on a compute entry");
}

// Resets, then sets entry index's u32 at to value.
static mrhiResult EntryWith(uint32_t index, size_t at, uint32_t value)
{
    Reset();
    Put32(Record(ENTRIES, index, 48) + at, value);
    return Built();
}

// Resets, then gives entry index builtins, without the WGSL the view
// index excludes.
static mrhiResult NativeEntryWith(uint32_t index, uint32_t builtins)
{
    Reset();
    Put32(Record(ENTRIES, index, 48) + 36, builtins);
    s_sections[WGSL].size = 0;
    return Built();
}

static void TestBuiltinsAndStorage(void)
{
    uint32_t fragment = mrhiShaderBuiltinsKnown & ~(uint32_t)mrhi_builtinViewIndex;
    CHECK(EntryWith(1, 36, fragment) == mrhi_success, "every fragment builtin");
    CHECK(NativeEntryWith(1, mrhiShaderBuiltinsKnown) == mrhi_success,
          "every builtin, without WGSL");
    CHECK(EntryWith(1, 36, mrhi_builtinViewIndex) == mrhi_errorInvalid, "the view index with WGSL");
    CHECK(NativeEntryWith(0, mrhi_builtinViewIndex) == mrhi_success, "a vertex entry's view index");
    CHECK(NativeEntryWith(0, mrhi_builtinViewIndex | mrhi_builtinFrontFacing) == mrhi_errorInvalid,
          "but no other vertex builtin");
    CHECK(NativeEntryWith(2, mrhi_builtinViewIndex) == mrhi_errorInvalid,
          "a compute entry's view index");
    CHECK(EntryWith(1, 36, mrhiShaderBuiltinsKnown + 1) == mrhi_errorInvalid, "an unknown builtin");
    CHECK(EntryWith(0, 36, mrhi_builtinFragDepth) == mrhi_errorInvalid, "a vertex builtin");
    CHECK(EntryWith(2, 36, mrhi_builtinSampleIndex) == mrhi_errorInvalid, "a compute builtin");
    CHECK(EntryWith(1, 36, 0x80000000u) == mrhi_errorInvalid, "the highest unknown builtin");
    CHECK(EntryWith(1, 40, 16) == mrhi_errorInvalid, "a fragment entry's workgroup storage");
    CHECK(EntryWith(1, 40, 0x10000u) == mrhi_errorInvalid,
          "a fragment entry's workgroup storage past 16 bits");
    CHECK(EntryWith(2, 40, 0) == mrhi_success, "a compute entry without workgroup storage");
    Reset();
    Assemble();
    mrhiContainer container;
    CHECK(mrhiParseContainer(s_container, s_size, &container) == mrhi_success &&
              container.builtins == mrhi_builtinFrontFacing,
          "the builtins noted");
    Put32(Record(ENTRIES, 1, 48) + 36, mrhi_builtinFragDepth);
    uint8_t* second = Record(ENTRIES, 3, 48);
    Put32(second, mrhi_stageFragment);
    Put32(second + 8, 1);
    Put32(second + 36, mrhi_builtinSampleMaskOut);
    Assemble();
    CHECK(mrhiParseContainer(s_container, s_size, &container) == mrhi_success &&
              container.builtins == (mrhi_builtinFragDepth | mrhi_builtinSampleMaskOut),
          "the builtins of every entry noted");
}

// Resets, then gives entry index heap uses, without the WGSL they
// exclude.
static void SetHeapUses(uint32_t index, mrhiShaderHeapUses uses)
{
    Reset();
    Put32(Record(ENTRIES, index, 48) + 44, uses);
    s_sections[WGSL].size = 0;
}

static mrhiResult HeapUses(uint32_t index, mrhiShaderHeapUses uses)
{
    SetHeapUses(index, uses);
    return Built();
}

static void TestHeapUses(void)
{
    CHECK(HeapUses(2, mrhiShaderHeapUsesKnown) == mrhi_success, "every heap use");
    CHECK(HeapUses(1, mrhiShaderHeapUsesKnown + 1) == mrhi_errorInvalid, "an unknown heap use");
    CHECK(HeapUses(1, 0x80000000u) == mrhi_errorInvalid, "the highest unknown heap use");
    CHECK(HeapUses(1, mrhi_heapUseWrites) == mrhi_errorInvalid, "writes alone");
    CHECK(HeapUses(1, mrhi_heapUseWrites | mrhi_heapUseSampledTextures | mrhi_heapUseSamplers) ==
              mrhi_errorInvalid,
          "writes without a storage kind");
    CHECK(HeapUses(1, mrhi_heapUseWrites | mrhi_heapUseStorageTextures) == mrhi_success &&
              HeapUses(2, mrhi_heapUseWrites | mrhi_heapUseStorageBuffers) == mrhi_success,
          "fragment and compute entries write");
    CHECK(HeapUses(0, mrhi_heapUseWrites | mrhi_heapUseStorageBuffers) == mrhi_errorInvalid,
          "a vertex entry does not");
    CHECK(HeapUses(0, mrhi_heapUseStorageBuffers) == mrhi_success, "but reads");
    Reset();
    Put32(Record(ENTRIES, 1, 48) + 44, mrhi_heapUseSampledTextures);
    CHECK(Built() == mrhi_errorInvalid, "WGSL beside a heap");
    Reset();
    Put32(Record(ENTRIES, 1, 48) + 44, 0x10000u);
    CHECK(Built() == mrhi_errorInvalid, "a heap use past 16 bits, beside WGSL");
    SetHeapUses(0, mrhi_heapUseSampledTextures);
    Put32(Record(ENTRIES, 2, 48) + 44, mrhi_heapUseStorageBuffers | mrhi_heapUseWrites);
    Assemble();
    mrhiContainer container;
    CHECK(mrhiParseContainer(s_container, s_size, &container) == mrhi_success &&
              container.heapUses ==
                  (mrhi_heapUseSampledTextures | mrhi_heapUseStorageBuffers | mrhi_heapUseWrites) &&
              container.wgsl == nullptr && container.wgslBytes == 0 &&
              mrhiContainerEntry(&container, 2).heapUses ==
                  (mrhi_heapUseStorageBuffers | mrhi_heapUseWrites),
          "the heap uses of every entry noted");
    DropSection(WGSL);
    CHECK(Built() == mrhi_success, "no WGSL section at all");
}

// Resets, then breaks binding index's byte at to value.
static mrhiResult BindingWith(uint32_t index, size_t at, uint8_t value)
{
    Reset();
    Record(BINDINGS, index, 24)[at] = value;
    return Built();
}

static void TestBindings(void)
{
    CHECK(BindingWith(0, 0, 4) == mrhi_errorInvalid, "a fifth table");
    CHECK(BindingWith(0, 0, 3) == mrhi_success, "the fourth table");
    CHECK(BindingWith(2, 1, mrhi_bindingNone) == mrhi_errorInvalid, "no kind");
    CHECK(BindingWith(2, 1, mrhi_bindingStorageTexture + 1) == mrhi_errorInvalid,
          "an unknown kind");
    CHECK(BindingWith(0, 4, 0) == mrhi_errorInvalid, "no stages");
    CHECK(BindingWith(0, 4, 8) == mrhi_errorInvalid, "an unknown stage");
    CHECK(BindingWith(1, 4, mrhi_stageVertex) == mrhi_errorInvalid,
          "a vertex stage writing a storage buffer");
    CHECK(BindingWith(5, 4, mrhi_stageVertex) == mrhi_errorInvalid,
          "a vertex stage writing a storage texture");
    Reset();
    uint8_t* storage = Record(BINDINGS, 5, 24);
    storage[4] = mrhi_stageVertex;
    storage[11] = mrhi_storageReadOnly;
    CHECK(Built() == mrhi_success, "a vertex stage reading a storage texture");
    CHECK(BindingWith(0, 2, 1) == mrhi_errorInvalid, "a repeated table and slot");
    CHECK(BindingWith(3, 8, mrhi_samplerNone) == mrhi_errorInvalid, "a sampler with no type");
    CHECK(BindingWith(3, 8, mrhi_samplerComparison + 1) == mrhi_errorInvalid,
          "an unknown sampler type");
    CHECK(BindingWith(3, 8, mrhi_samplerComparison) == mrhi_success, "a comparison sampler");
    CHECK(BindingWith(0, 8, mrhi_samplerFiltering) == mrhi_errorInvalid, "a buffer's sampler type");
    CHECK(BindingWith(4, 9, mrhi_sampleNone) == mrhi_errorInvalid, "no sample type");
    CHECK(BindingWith(4, 9, mrhi_sampleUint + 1) == mrhi_errorInvalid, "an unknown sample type");
    CHECK(BindingWith(3, 9, mrhi_sampleFloat) == mrhi_errorInvalid, "a sampler's sample type");
    CHECK(BindingWith(4, 10, mrhi_texture3d + 1) == mrhi_errorInvalid, "an unknown dimension");
    CHECK(BindingWith(0, 10, mrhi_texture2dArray) == mrhi_errorInvalid, "a buffer's dimension");
    CHECK(BindingWith(5, 10, mrhi_textureCube) == mrhi_errorInvalid, "a storage cube");
    CHECK(BindingWith(5, 10, mrhi_texture3d) == mrhi_success, "a 3D storage texture");
    CHECK(BindingWith(5, 11, mrhi_storageNone) == mrhi_errorInvalid, "no access");
    CHECK(BindingWith(5, 11, mrhi_storageReadWrite + 1) == mrhi_errorInvalid, "an unknown access");
    CHECK(BindingWith(4, 11, mrhi_storageReadOnly) == mrhi_errorInvalid, "a sampled access");
    CHECK(BindingWith(5, 12, 0) == mrhi_errorInvalid, "a storage texture with no format");
    CHECK(BindingWith(5, 12, 0xFF) == mrhi_errorInvalid, "an unknown format");
    CHECK(BindingWith(4, 12, mrhi_formatRgba8Unorm) == mrhi_errorInvalid, "a sampled format");
    CHECK(BindingWith(4, 14, 1) == mrhi_errorInvalid, "a filterable multisampled cube");
    Reset();
    uint8_t* sampled = Record(BINDINGS, 4, 24);
    sampled[9] = mrhi_sampleUnfilterableFloat;
    sampled[10] = mrhi_texture2d;
    sampled[14] = 1;
    CHECK(Built() == mrhi_success, "an unfilterable multisampled 2D texture");
    sampled[14] = 2;
    CHECK(Built() == mrhi_errorInvalid, "multisampled neither 0 nor 1");
    sampled[14] = 1;
    sampled[10] = mrhi_texture2dArray;
    CHECK(Built() == mrhi_errorInvalid, "a multisampled array");
    sampled[10] = mrhi_texture2d;
    sampled[9] = mrhi_sampleFloat;
    CHECK(Built() == mrhi_errorInvalid, "a filterable multisampled texture");
    CHECK(BindingWith(0, 14, 1) == mrhi_errorInvalid, "a multisampled buffer");
    CHECK(BindingWith(0, 15, 1) == mrhi_errorInvalid, "a binding zero that is not zero");
    CHECK(BindingWith(4, 16, 1) == mrhi_errorInvalid, "a texture's minimum size");
    CHECK(BindingWith(3, 23, 1) == mrhi_errorInvalid, "a sampler's minimum size");
    CHECK(BindingWith(1, 23, 1) == mrhi_success, "a storage buffer's minimum size");
}

static void TestConstantsAndCode(void)
{
    Reset();
    Record(CONSTANTS, 0, 16)[4] = mrhi_constantNone;
    CHECK(Built() == mrhi_errorInvalid, "a constant with no type");
    Record(CONSTANTS, 0, 16)[4] = mrhi_constantFloat32 + 1;
    CHECK(Built() == mrhi_errorInvalid, "an unknown constant type");
    Reset();
    uint8_t* flag = Record(CONSTANTS, 1, 16);
    flag[4] = mrhi_constantBool;
    Put32(flag, 8);
    Put32(flag + 8, 1);
    CHECK(Built() == mrhi_success, "a true boolean");
    Put32(flag + 8, 2);
    CHECK(Built() == mrhi_errorInvalid, "a boolean neither 0 nor 1");
    Put32(flag + 8, 0);
    flag[12] = 1;
    CHECK(Built() == mrhi_success, "a constant with no default");
    Put32(flag + 8, 1);
    CHECK(Built() == mrhi_errorInvalid, "a default's bits on one with none");
    Put32(flag + 8, 0);
    flag[12] = 2;
    CHECK(Built() == mrhi_errorInvalid, "a required flag neither 0 nor 1");
    flag[12] = 0;
    Put32(flag, 7);
    CHECK(Built() == mrhi_errorInvalid, "a repeated constant id");
    size_t zeros[] = {5, 6, 7, 13, 15};
    for (size_t i = 0; i < sizeof(zeros) / sizeof(zeros[0]); ++i)
    {
        Reset();
        Record(CONSTANTS, 0, 16)[zeros[i]] = 1;
        CHECK(Built() == mrhi_errorInvalid, "a constant zero that is not zero");
    }
    Reset();
    s_sections[SPIRV].size = 16;
    CHECK(Built() == mrhi_errorInvalid, "SPIR-V shorter than its header");
    Reset();
    s_sections[SPIRV].size = 22;
    CHECK(Built() == mrhi_errorInvalid, "SPIR-V not in words");
    Reset();
    s_sections[SPIRV].bytes[0] = 0x04;
    CHECK(Built() == mrhi_errorInvalid, "SPIR-V without its magic");
    Reset();
    s_sections[WGSL].size = 0;
    CHECK(Built() == mrhi_errorInvalid, "empty WGSL");
    Reset();
    s_sections[WGSL].bytes[3] = 0xC0;
    CHECK(Built() == mrhi_errorInvalid, "WGSL that is not UTF-8");
    Reset();
    s_sections[WGSL].bytes[7] = 0;
    CHECK(Built() == mrhi_errorInvalid, "WGSL with NUL");
}

// Resets and adds every Metal section, then sets byte at of the map.
static mrhiResult MapWith(size_t at, uint8_t value)
{
    Reset();
    AddMetal(true, true);
    s_sections[METAL_MAP].bytes[at] = value;
    return Built();
}

// The offset in the map of entry index's record, and of binding index.
#define MAP_ENTRY(index)   (8 + (index) * 16)
#define MAP_BINDING(index) (56 + (index))

static void TestMetalCode(void)
{
    Reset();
    AddMetal(true, true);
    Assemble();
    mrhiContainer container;
    CHECK(mrhiParseContainer(s_container, s_size, &container) == mrhi_success, "every Metal part");
    mrhiMetalEntry fragment = mrhiContainerMetalEntry(&container, 1);
    CHECK(container.metalMap != nullptr && container.mslBytes == 56 &&
              container.metallibBytes == 8 && mrhiContainerMetalRoot(&container) == 0 &&
              fragment.mslOffset == 18 && fragment.mslLength == 20 &&
              memcmp(container.msl + 18, "fragment void fs(){}", 20) == 0 &&
              fragment.sizesIndex == MRHI_METAL_NONE &&
              mrhiContainerMetalIndex(&container, 2) == 3 &&
              mrhiContainerMetalIndex(&container, 5) == 1,
          "the map read back");
    Reset();
    AddMetal(true, false);
    CHECK(Built() == mrhi_success, "MSL alone");
    Reset();
    AddMetal(false, true);
    CHECK(Built() == mrhi_success, "a metallib alone");
    Reset();
    AddMetal(false, false);
    CHECK(Built() == mrhi_errorInvalid, "a map without code");
    Reset();
    AddMetal(true, true);
    DropSection(METAL_MAP);
    CHECK(Built() == mrhi_errorInvalid, "code without a map");
    Reset();
    AddMetal(false, true);
    DropSection(METAL_MAP);
    CHECK(Built() == mrhi_errorInvalid, "a metallib without a map");
    Reset();
    AddMetal(true, true);
    s_sections[s_sectionCount++] = (Section){.type = 11};
    CHECK(Built() == mrhi_errorInvalid, "a repeated map");
    Reset();
    AddMetal(true, true);
    s_sections[METAL_MAP].size -= 1;
    CHECK(Built() == mrhi_errorInvalid, "a map short of a binding");
    s_sections[METAL_MAP].size += 2;
    CHECK(Built() == mrhi_errorInvalid, "a map a byte long");
    s_sections[METAL_MAP].size = 0;
    CHECK(Built() == mrhi_errorInvalid, "an empty map");
    Reset();
    AddMetal(false, true);
    s_sections[METAL_MAP + 1].bytes[3] = 'C';
    CHECK(Built() == mrhi_errorInvalid, "a metallib without its magic");
    s_sections[METAL_MAP + 1].bytes[3] = 'B';
    s_sections[METAL_MAP + 1].size = 3;
    Assemble();
    // The magic's last byte in the padding after it, which nothing checks.
    s_container[OffsetOf(METAL_MAP + 1) + 3] = 'B';
    Seal();
    CHECK(Parse() == mrhi_errorInvalid, "a metallib shorter than its magic");
    SetHeapUses(2, mrhi_heapUseStorageBuffers);
    AddMetal(true, true);
    CHECK(Built() == mrhi_errorInvalid, "Metal code beside a heap");
}

static void TestMetalMap(void)
{
    for (size_t at = 1; at < 8; ++at)
    {
        CHECK(MapWith(at, 1) == mrhi_errorInvalid, "a map zero that is not zero");
    }
    CHECK(MapWith(0, 30) == mrhi_success, "the root block at buffer 30");
    CHECK(MapWith(0, 31) == mrhi_errorInvalid, "the root block past the buffers");
    CHECK(MapWith(0, 255) == mrhi_errorInvalid, "no root block's index for one");
    CHECK(MapWith(0, 1) == mrhi_errorInvalid, "the root block on a binding's buffer");
    Reset();
    Put32(Record(META, 0, 16), 0);
    AddMetal(true, true);
    s_sections[METAL_MAP].bytes[0] = 255;
    CHECK(Built() == mrhi_success, "no root block, no index");
    s_sections[METAL_MAP].bytes[0] = 0;
    CHECK(Built() == mrhi_errorInvalid, "an index for an empty root block");
    CHECK(MapWith(MAP_BINDING(0), 30) == mrhi_success, "a buffer at 30");
    CHECK(MapWith(MAP_BINDING(0), 31) == mrhi_errorInvalid, "a buffer past 30");
    CHECK(MapWith(MAP_BINDING(1), 1) == mrhi_errorInvalid, "two buffers at one index");
    CHECK(MapWith(MAP_BINDING(0), 0) == mrhi_errorInvalid, "a buffer on the root block");
    CHECK(MapWith(MAP_BINDING(3), 15) == mrhi_success, "a sampler at 15");
    CHECK(MapWith(MAP_BINDING(3), 16) == mrhi_errorInvalid, "a sampler past 15");
    CHECK(MapWith(MAP_BINDING(4), 127) == mrhi_success, "a texture at 127");
    CHECK(MapWith(MAP_BINDING(4), 128) == mrhi_errorInvalid, "a texture past 127");
    CHECK(MapWith(MAP_BINDING(5), 0) == mrhi_errorInvalid, "two textures at one index");
    CHECK(MapWith(MAP_BINDING(3), 1) == mrhi_success, "a sampler on a texture's index");
    CHECK(MapWith(MAP_BINDING(4), 3) == mrhi_success, "a texture on a buffer's index");
    CHECK(MapWith(MAP_ENTRY(2) + 8, 30) == mrhi_success, "buffer sizes at a free index");
    CHECK(MapWith(MAP_ENTRY(2) + 8, 31) == mrhi_errorInvalid, "buffer sizes past 30");
    CHECK(MapWith(MAP_ENTRY(2) + 8, 0) == mrhi_errorInvalid, "buffer sizes on the root block");
    CHECK(MapWith(MAP_ENTRY(2) + 8, 2) == mrhi_errorInvalid, "buffer sizes on a binding");
    CHECK(MapWith(MAP_ENTRY(2) + 8, 4) == mrhi_success &&
              MapWith(MAP_ENTRY(1) + 8, 4) == mrhi_success,
          "buffer sizes on an index a texture or sampler uses");
    for (size_t at = 9; at < 16; ++at)
    {
        CHECK(MapWith(MAP_ENTRY(0) + at, 1) == mrhi_errorInvalid,
              "an entry's zero that is not zero");
    }
}

static void TestMetalSources(void)
{
    CHECK(MapWith(MAP_ENTRY(1) + 4, 0) == mrhi_errorInvalid, "an entry without MSL");
    CHECK(MapWith(MAP_ENTRY(2) + 4, 19) == mrhi_errorInvalid, "MSL a byte past the section");
    CHECK(MapWith(MAP_ENTRY(2) + 4, 17) == mrhi_success, "MSL short of the section's end");
    CHECK(MapWith(MAP_ENTRY(2) + 3, 0xFF) == mrhi_errorInvalid, "MSL far past the section");
    CHECK(MapWith(MAP_ENTRY(0) + 4, 38) == mrhi_success, "one MSL range over another");
    Reset();
    AddMetal(true, true);
    Put32(s_sections[METAL_MAP].bytes + MAP_ENTRY(2), 0xFFFFFFF0u);
    Put32(s_sections[METAL_MAP].bytes + MAP_ENTRY(2) + 4, 0x20u);
    CHECK(Built() == mrhi_errorInvalid, "an MSL range whose end wraps");
    Reset();
    AddMetal(true, true);
    s_sections[MSL].bytes[40] = 0;
    CHECK(Built() == mrhi_errorInvalid, "MSL with NUL");
    s_sections[MSL].bytes[40] = 0xC0;
    CHECK(Built() == mrhi_errorInvalid, "MSL that is not UTF-8");
    Reset();
    AddMetal(false, true);
    Put32(s_sections[METAL_MAP].bytes + MAP_ENTRY(0) + 4, 1);
    CHECK(Built() == mrhi_errorInvalid, "an MSL range without MSL");
    Put32(s_sections[METAL_MAP].bytes + MAP_ENTRY(0) + 4, 0);
    Put32(s_sections[METAL_MAP].bytes + MAP_ENTRY(0), 1);
    CHECK(Built() == mrhi_errorInvalid, "an MSL offset without MSL");
}

// A container whose input section is moved past the others and holds
// count records, each valid on its own.
static mrhiResult WithInputs(uint32_t count)
{
    Reset();
    Assemble();
    size_t offset = s_size;
    for (uint32_t i = 0; i < count; ++i)
    {
        uint8_t* input = s_container + offset + (size_t)i * 8;
        memset(input, 0, 8);
        input[4] = mrhi_scalarFloat32;
        input[5] = 1;
    }
    s_size = offset + (size_t)count * 8;
    Put64(s_container + 8, s_size);
    Move(INPUTS, offset, (uint64_t)count * 8);
    return Parse();
}

static void TestRecordLimit(void)
{
    CHECK(WithInputs(4096) == mrhi_success, "4096 records");
    CHECK(WithInputs(4097) == mrhi_errorInvalid, "4097 records");
}

// Every single-bit flip of the default container is refused: the digest
// covers every byte past it, and the header's fields before it are all
// checked.
static void TestEveryFlip(void)
{
    Reset();
    Assemble();
    bool refused = true;
    for (size_t at = 0; at < s_size; ++at)
    {
        for (int bit = 0; bit < 8; ++bit)
        {
            s_container[at] ^= (uint8_t)(1u << bit);
            refused = refused && Parse() != mrhi_success;
            s_container[at] ^= (uint8_t)(1u << bit);
        }
    }
    CHECK(refused, "every flipped bit refused");
    CHECK(Parse() == mrhi_success, "restored");
}

// Fails the device's allocations while s_failAllocations is set.
static bool s_failAllocations;

static void* Alloc(size_t size, size_t alignment, void* context)
{
    (void)alignment;
    (void)context;
    return s_failAllocations ? nullptr : malloc(size);
}

static void Free(void* memory, size_t size, size_t alignment, void* context)
{
    (void)size;
    (void)alignment;
    (void)context;
    free(memory);
}

static mrhiDevice* Open(uint32_t shaders, bool ready)
{
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.deviceLimits.shaders = shaders;
    def.allocator = (mrhiAllocator){.alloc = Alloc, .free = Free};
    return OpenWith(def, ready);
}

static mrhiShaderDef Def(void)
{
    mrhiShaderDef def = mrhiDefaultShaderDef();
    def.bytes = s_container;
    def.byteCount = s_size;
    return def;
}

static void TestCreate(void)
{
    Reset();
    Assemble();
    mrhiDevice* device = Open(2, true);
    mrhiShaderDef def = Def();
    def.label = "lit";
    def.labelLength = 3;
    mrhiShaderId a = {0};
    CHECK(mrhiCreateShader(device, &def, &a) == mrhi_success && a.index1 != 0, "a shader");
    memset(s_container, 0, s_size);
    mrhiShaderInfo info = {0};
    CHECK(mrhiGetShaderInfo(device, a, &info) == mrhi_success, "its info");
    Reset();
    Assemble();
    CHECK(memcmp(info.digest, s_container + 16, MRHI_DIGEST_BYTES) == 0 && info.entryCount == 3 &&
              info.bindingCount == 6 && info.rootBlockBytes == 16,
          "kept past the container's bytes");
    mrhiShaderId b = {0};
    CHECK(mrhiCreateShader(device, &def, &b) == mrhi_success, "a second from the same bytes");
    mrhiShaderId c = {0};
    CHECK(mrhiCreateShader(device, &def, &c) == mrhi_errorCapacity, "the limit");
    CHECK(mrhiDestroyShader(device, a) == mrhi_success, "destroyed");
    CHECK(mrhiDestroyShader(device, a) == mrhi_errorStale, "its id has ended");
    CHECK(mrhiGetShaderInfo(device, a, &info) == mrhi_errorStale, "no info for it");
    CHECK(mrhiCreateShader(device, &def, &c) == mrhi_success, "room again");
    CHECK(mrhiDestroyShader(nullptr, b) == mrhi_errorInvalid, "no device");
    CHECK(mrhiGetShaderInfo(nullptr, b, &info) == mrhi_errorInvalid, "no device for info");
    CHECK(mrhiGetDeviceMisuse(device) == 0, "no misuse");
    // b and c are still live: the device frees them.
    Close(device);
}

static void TestRefusals(void)
{
    Reset();
    Assemble();
    mrhiDevice* device = Open(4, true);
    mrhiShaderId shader;
    mrhiShaderDef def = Def();
    CHECK(mrhiCreateShader(nullptr, &def, &shader) == mrhi_errorInvalid, "no device");
    CHECK(mrhiCreateShader(device, nullptr, &shader) == mrhi_errorInvalid, "no def");
    CHECK(mrhiCreateShader(device, &def, nullptr) == mrhi_errorInvalid, "no out");
    CHECK(mrhiGetShaderInfo(device, shader, nullptr) == mrhi_errorInvalid, "no info out");
    def.cookie = 0;
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_errorInvalid, "no cookie");
    def = Def();
    def.label = "\xC0\x80";
    def.labelLength = 2;
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_errorInvalid, "a bad label");
    def = Def();
    def.bytes = nullptr;
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_errorInvalid, "no bytes");
    def = Def();
    memmove(s_container + 4, s_container, s_size);
    def.bytes = s_container + 4;
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_errorInvalid, "misaligned bytes");
    Assemble();
    s_container[s_size - 1] ^= 1;
    def = Def();
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_errorInvalid, "a damaged container");
    CHECK(mrhiGetDeviceMisuse(device) == 8, "each counted");
    Assemble();
    Put32(s_container + 4, 2);
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_errorVersion, "another version");
    Put32(s_sections[META].bytes, mrhiDefaultLimits().rootBlockBytes + 4);
    Assemble();
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_errorUnsupported,
          "a root block past the device's");
    Put32(s_sections[META].bytes, mrhiDefaultLimits().rootBlockBytes);
    Assemble();
    mrhiChain critical = {.next = nullptr, .type = 0x7000u};
    def.next = &critical;
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_errorUnsupported, "an extension");
    CHECK(mrhiGetDeviceMisuse(device) == 8, "other refusals are not misuse");
    def.next = nullptr;
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_success, "the device's root block");
    Close(device);
}

static void TestFeatures(void)
{
    Reset();
    Record(OUTPUTS, 0, 8)[4] = mrhi_scalarFloat16;
    Assemble();
    mrhiDevice* device = Open(2, true);
    mrhiShaderDef def = Def();
    mrhiShaderId shader;
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_errorUnsupported,
          "16-bit floats without the feature");
    Close(device);
    s_adapter.features.shaderF16 = true;
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    deviceDef.features.shaderF16 = true;
    device = OpenWith(deviceDef, true);
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_success, "16-bit floats with it");
    Reset();
    Put32(Record(ENTRIES, 1, 48) + 36, mrhi_builtinPrimitiveIndex);
    Assemble();
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_errorUnsupported, "the primitive index");
    CHECK(mrhiGetDeviceMisuse(device) == 0, "neither is misuse");
    Close(device);
    // The view index needs multiview (mrhi-0020).
    Reset();
    Put32(Record(ENTRIES, 0, 48) + 36, mrhi_builtinViewIndex);
    s_sections[WGSL].size = 0;
    Assemble();
    def = Def();
    device = Open(2, true);
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_errorUnsupported,
          "the view index without multiview");
    Close(device);
    s_adapter.features.multiview = true;
    deviceDef = mrhiDefaultDeviceDef();
    deviceDef.features.multiview = true;
    device = OpenWith(deviceDef, true);
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_success, "with it");
    Close(device);
}

// The SPIR-V instructions after the header, a word each.
static void Instructions(const uint32_t* words, size_t count)
{
    uint8_t* spirv = Record(SPIRV, 0, 20 + count * 4);
    for (size_t i = 0; i < count; ++i)
    {
        Put32(spirv + 20 + i * 4, words[i]);
    }
}

// OpCapability's first word: two words long, opcode 17.
#define CAPABILITY (2u << 16 | 17u)

// The container's features from the capabilities its SPIR-V declares
// first.
static mrhiContainer Declaring(const uint32_t* words, size_t count)
{
    Reset();
    Instructions(words, count);
    Assemble();
    mrhiContainer container = {0};
    CHECK(mrhiParseContainer(s_container, s_size, &container) == mrhi_success, "it parses");
    return container;
}

// The capabilities a container's SPIR-V declares, the features they
// need, and the shaders a device makes of them with and without them.
static void TestCapabilities(void)
{
    static const struct
    {
        uint32_t capability;
        bool float16;
        bool subgroups;
        bool int64;
    } cases[] = {
        {9, true, false, false},     {4433, true, false, false},  {4436, true, false, false},
        {4432, false, false, false}, {4437, false, false, false}, {11, false, false, true},
        {10, false, false, false},   {12, false, false, false},   {61, false, true, false},
        {68, false, true, false},    {60, false, false, false},   {69, false, false, false},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        const uint32_t words[] = {CAPABILITY, 1, CAPABILITY, cases[i].capability};
        mrhiContainer container = Declaring(words, 4);
        CHECK(container.float16 == cases[i].float16 && container.subgroups == cases[i].subgroups &&
                  container.int64 == cases[i].int64,
              "a capability and the feature it needs");
    }
    const uint32_t after[] = {CAPABILITY, 1, 3u << 16 | 14u, 0, 1, CAPABILITY, 9};
    CHECK(!Declaring(after, 7).float16, "the list ends at the first other instruction");
    const uint32_t empty[] = {17u, 9, CAPABILITY, 9};
    CHECK(!Declaring(empty, 4).float16, "a word count of 0 ends it");
    const uint32_t longer[] = {3u << 16 | 17u, 9, 0, CAPABILITY, 9};
    CHECK(!Declaring(longer, 5).float16, "an OpCapability of another length ends it");
    const uint32_t cut[] = {CAPABILITY};
    CHECK(!Declaring(cut, 1).float16, "a capability cut by the module's end is not read");
    static const uint32_t needs[3] = {9, 61, 11};
    for (uint32_t i = 0; i < 3; ++i)
    {
        const uint32_t words[] = {CAPABILITY, needs[i]};
        (void)Declaring(words, 2);
        mrhiShaderDef def = Def();
        mrhiShaderId shader;
        mrhiDevice* device = Open(2, true);
        CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_errorUnsupported,
              "code needing a feature the device lacks");
        Close(device);
        mrhiFeatures features = {.shaderF16 = i == 0, .subgroups = i == 1, .shaderInt64 = i == 2};
        s_adapter.features = features;
        mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
        deviceDef.features = features;
        device = OpenWith(deviceDef, true);
        CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_success, "with the feature");
        Close(device);
    }
}

// A device granting bindless sampling, and heterogeneous heaps when
// asked.
static mrhiDevice* OpenBindless(bool heterogeneous)
{
    s_adapter.features.bindlessSampling = true;
    s_adapter.features.bindlessHeterogeneous = true;
    s_adapter.limits.heapSize = 4096;
    s_adapter.limits.samplerHeapSize = 64;
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.features.bindlessSampling = true;
    def.features.bindlessHeterogeneous = heterogeneous;
    def.limits.heapSize = 1024;
    def.limits.samplerHeapSize = 16;
    return OpenWith(def, true);
}

// Makes a shader of container whose entries use heaps on devices with
// and without the features they need.
static void TestHeapFeatures(void)
{
    SetHeapUses(1, mrhi_heapUseSampledTextures | mrhi_heapUseSamplers);
    Assemble();
    mrhiDevice* device = Open(2, true);
    mrhiShaderDef def = Def();
    mrhiShaderId shader;
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_errorUnsupported,
          "heaps without bindless sampling");
    Close(device);
    device = OpenBindless(false);
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_success, "with it");
    mrhiShaderInfo info;
    CHECK(mrhiGetShaderInfo(device, shader, &info) == mrhi_success &&
              info.heapUses == (mrhi_heapUseSampledTextures | mrhi_heapUseSamplers),
          "its heap uses told");
    SetHeapUses(2, mrhi_heapUseStorageTextures);
    Assemble();
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_errorUnsupported,
          "storage textures without heterogeneous heaps");
    SetHeapUses(2, mrhi_heapUseStorageBuffers);
    Assemble();
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_errorUnsupported,
          "storage buffers without them");
    CHECK(mrhiGetDeviceMisuse(device) == 0, "neither is misuse");
    Close(device);
    device = OpenBindless(true);
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_success, "storage buffers with them");
    SetHeapUses(2, mrhi_heapUseStorageTextures | mrhi_heapUseWrites);
    Assemble();
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_success, "storage textures with them");
    Close(device);
    ResetAdapter();
}

// Appends count bindings of a kind used by stages, in table 2 from slot
// first, with the details the kind needs.
static void AddBindings(mrhiBindingKind kind, uint32_t count, mrhiShaderStages stages,
                        uint32_t first)
{
    uint32_t at = (uint32_t)(s_sections[BINDINGS].size / 24);
    for (uint32_t i = 0; i < count; ++i)
    {
        uint8_t* binding = Binding(at + i, 2, (uint16_t)(first + i), kind, stages);
        binding[8] = kind == mrhi_bindingSampler ? mrhi_samplerFiltering : 0;
        binding[9] = kind == mrhi_bindingSampledTexture ? mrhi_sampleFloat : 0;
        binding[11] = kind == mrhi_bindingStorageTexture ? mrhi_storageWriteOnly : 0;
        Put16(binding + 12, kind == mrhi_bindingStorageTexture ? mrhi_formatRgba8Unorm : 0);
    }
}

// Makes a shader of the sections on a device, destroying it again.
static mrhiResult Made(mrhiDevice* device)
{
    Assemble();
    mrhiShaderDef def = Def();
    mrhiShaderId shader;
    mrhiResult status = mrhiCreateShader(device, &def, &shader);
    if (status == mrhi_success)
    {
        CHECK(mrhiDestroyShader(device, shader) == mrhi_success, "destroyed");
    }
    return status;
}

// Makes a shader with count more bindings of a kind than the default
// container's; the device's limit for them is allowed.
static mrhiResult WithBindings(mrhiDevice* device, mrhiBindingKind kind, uint32_t count,
                               mrhiShaderStages stages)
{
    Reset();
    AddBindings(kind, count, stages, 0);
    return Made(device);
}

// A table of more bindings than a chunk of commands holds with its
// command, on a device whose stage limits allow them.
static void TestTableSize(void)
{
    s_adapter.limits.samplersPerStage = 256;
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.limits.samplersPerStage = 256;
    mrhiDevice* device = OpenWith(def, true);
    CHECK(WithBindings(device, mrhi_bindingSampler, MRHI_TABLE_BINDINGS, mrhi_stageFragment) ==
              mrhi_success,
          "a table of MRHI_TABLE_BINDINGS");
    CHECK(WithBindings(device, mrhi_bindingSampler, MRHI_TABLE_BINDINGS + 1, mrhi_stageFragment) ==
              mrhi_errorUnsupported,
          "a table of more");
    Close(device);
}

static void TestLimits(void)
{
    mrhiDevice* device = Open(4, true);
    mrhiLimits limits = mrhiDefaultLimits();
    // The default container binds, in the fragment stage, a uniform buffer,
    // a sampler and a sampled texture; in the compute stage a storage buffer
    // and a storage texture.
    CHECK(WithBindings(device, mrhi_bindingSampledTexture, limits.sampledTexturesPerStage - 1,
                       mrhi_stageFragment) == mrhi_success,
          "as many sampled textures as the limit");
    CHECK(WithBindings(device, mrhi_bindingSampledTexture, limits.sampledTexturesPerStage,
                       mrhi_stageFragment) == mrhi_errorUnsupported,
          "a sampled texture past the limit");
    CHECK(WithBindings(device, mrhi_bindingSampledTexture, limits.sampledTexturesPerStage,
                       mrhi_stageVertex) == mrhi_success,
          "the limit counted per stage");
    CHECK(WithBindings(device, mrhi_bindingSampler, limits.samplersPerStage - 1,
                       mrhi_stageFragment) == mrhi_success,
          "as many samplers as the limit");
    CHECK(WithBindings(device, mrhi_bindingSampler, limits.samplersPerStage, mrhi_stageFragment) ==
              mrhi_errorUnsupported,
          "a sampler past the limit");
    CHECK(WithBindings(device, mrhi_bindingUniformBuffer, limits.uniformBuffersPerStage - 1,
                       mrhi_stageFragment) == mrhi_success,
          "as many uniform buffers as the limit");
    CHECK(WithBindings(device, mrhi_bindingUniformBuffer, limits.uniformBuffersPerStage,
                       mrhi_stageFragment) == mrhi_errorUnsupported,
          "a uniform buffer past the limit");
    CHECK(WithBindings(device, mrhi_bindingReadOnlyStorageBuffer, limits.storageBuffersPerStage - 1,
                       mrhi_stageCompute) == mrhi_success,
          "as many storage buffers as the limit");
    CHECK(WithBindings(device, mrhi_bindingReadOnlyStorageBuffer, limits.storageBuffersPerStage,
                       mrhi_stageCompute) == mrhi_errorUnsupported,
          "a read-only storage buffer past the limit, counted with the others");
    CHECK(WithBindings(device, mrhi_bindingStorageTexture, limits.storageTexturesPerStage - 1,
                       mrhi_stageCompute) == mrhi_success,
          "as many storage textures as the limit");
    CHECK(WithBindings(device, mrhi_bindingStorageTexture, limits.storageTexturesPerStage,
                       mrhi_stageCompute) == mrhi_errorUnsupported,
          "a storage texture past the limit");
    Reset();
    AddBindings(mrhi_bindingSampler, 1, mrhi_stageCompute, limits.bindingsPerTable - 1);
    CHECK(Made(device) == mrhi_success, "the last slot");
    Reset();
    AddBindings(mrhi_bindingSampler, 1, mrhi_stageCompute, limits.bindingsPerTable);
    CHECK(Made(device) == mrhi_errorUnsupported, "a slot past the limit");
    Reset();
    Put64(Record(BINDINGS, 0, 24) + 16, limits.uniformBindingBytes);
    CHECK(Made(device) == mrhi_success, "a uniform buffer as large as the limit");
    Put64(Record(BINDINGS, 0, 24) + 16, limits.uniformBindingBytes + 1);
    CHECK(Made(device) == mrhi_errorUnsupported, "a uniform buffer past the limit");
    Reset();
    Put64(Record(BINDINGS, 1, 24) + 16, limits.storageBindingBytes);
    CHECK(Made(device) == mrhi_success, "a storage buffer as large as the limit");
    Put64(Record(BINDINGS, 1, 24) + 16, limits.storageBindingBytes + 1);
    CHECK(Made(device) == mrhi_errorUnsupported, "a storage buffer past the limit");
    CHECK(mrhiGetDeviceMisuse(device) == 0, "limits are not misuse");
    Close(device);
}

// Makes a shader whose compute entry has a workgroup and its storage.
static mrhiResult WithWorkgroup(mrhiDevice* device, uint32_t x, uint32_t y, uint32_t z,
                                uint32_t storage)
{
    Reset();
    uint8_t* compute = Record(ENTRIES, 2, 48);
    Put32(compute + 12, x);
    Put32(compute + 16, y);
    Put32(compute + 20, z);
    Put32(compute + 40, storage);
    return Made(device);
}

static void TestEntryLimits(void)
{
    mrhiDevice* device = Open(4, true);
    mrhiLimits limits = mrhiDefaultLimits();
    CHECK(WithWorkgroup(device, limits.workgroupSizeX, 1, 1, limits.workgroupStorageBytes) ==
              mrhi_success,
          "a workgroup at the limits");
    CHECK(WithWorkgroup(device, limits.workgroupSizeX + 1, 1, 1, 0) == mrhi_errorUnsupported,
          "a workgroup past x");
    CHECK(WithWorkgroup(device, 1, limits.workgroupSizeY, 1, 0) == mrhi_success &&
              WithWorkgroup(device, 1, 1, limits.workgroupSizeZ, 0) == mrhi_success,
          "a workgroup at the y and z limits");
    CHECK(WithWorkgroup(device, 1, limits.workgroupSizeY + 1, 1, 0) == mrhi_errorUnsupported,
          "a workgroup past y");
    CHECK(WithWorkgroup(device, 1, 1, limits.workgroupSizeZ + 1, 0) == mrhi_errorUnsupported,
          "a workgroup past z");
    CHECK(WithWorkgroup(device, 16, 16, 1, 0) == mrhi_success, "256 invocations");
    CHECK(WithWorkgroup(device, 16, 4, 8, 0) == mrhi_errorUnsupported, "512 invocations");
    CHECK(WithWorkgroup(device, 16, 16, 2, 0) == mrhi_errorUnsupported,
          "512 invocations, z counted");
    CHECK(WithWorkgroup(device, 256, 256, 64, 0) == mrhi_errorUnsupported,
          "every size at its limit");
    CHECK(WithWorkgroup(device, 1, 1, 1, limits.workgroupStorageBytes + 1) == mrhi_errorUnsupported,
          "workgroup storage past the limit");

    Reset();
    for (uint32_t i = 0; i < limits.vertexAttributes; ++i)
    {
        Variable(INPUTS, i, i, mrhi_scalarFloat32, 4, 0, 0);
    }
    Put16(Record(ENTRIES, 0, 48) + 26, (uint16_t)limits.vertexAttributes);
    CHECK(Made(device) == mrhi_success, "as many vertex inputs as the limit");
    Put32(Record(INPUTS, 0, 8), limits.vertexAttributes);
    CHECK(Made(device) == mrhi_errorUnsupported, "a vertex input location past the limit");
    Put32(Record(INPUTS, 0, 8), limits.vertexAttributes + 1);
    Variable(INPUTS, limits.vertexAttributes, 0, mrhi_scalarFloat32, 4, 0, 0);
    Put16(Record(ENTRIES, 0, 48) + 26, (uint16_t)(limits.vertexAttributes + 1));
    CHECK(Made(device) == mrhi_errorUnsupported, "a vertex input past the limit");

    Reset();
    uint32_t count = limits.interStageVariables;
    for (uint32_t i = 0; i < count; ++i)
    {
        Variable(VARIABLES, i, i, mrhi_scalarFloat32, 4, mrhi_interpolationPerspective,
                 mrhi_samplingCenter);
    }
    // Both entries read and write every variable; the fragment entry's
    // front_facing makes one too many.
    uint8_t* vertex = Record(ENTRIES, 0, 48);
    uint8_t* fragment = Record(ENTRIES, 1, 48);
    Put16(vertex + 34, (uint16_t)count);
    Put16(fragment + 32, 0);
    Put16(fragment + 34, (uint16_t)count);
    CHECK(Made(device) == mrhi_errorUnsupported, "a fragment builtin past the limit");
    Put32(fragment + 36, 0);
    CHECK(Made(device) == mrhi_success, "as many variables as the limit");
    Put16(fragment + 34, (uint16_t)(count - 1));
    Put32(fragment + 36,
          mrhi_builtinFrontFacing | mrhi_builtinFragDepth | mrhi_builtinSampleMaskOut);
    CHECK(Made(device) == mrhi_success, "outputs are not counted");
    Put32(fragment + 36, mrhi_builtinFrontFacing | mrhi_builtinSampleIndex);
    CHECK(Made(device) == mrhi_errorUnsupported, "two builtins counted");
    Put32(fragment + 36, mrhi_builtinSampleMaskIn);
    Put16(fragment + 34, (uint16_t)count);
    CHECK(Made(device) == mrhi_errorUnsupported, "the sample mask counted");
    Put32(fragment + 36, 0);
    Put32(Record(VARIABLES, 0, 8), count);
    CHECK(Made(device) == mrhi_errorUnsupported, "a variable location past the limit");
    Put16(fragment + 34, 0);
    CHECK(Made(device) == mrhi_errorUnsupported, "a vertex output location past the limit");
    Variable(VARIABLES, count, 0, mrhi_scalarFloat32, 4, mrhi_interpolationPerspective,
             mrhi_samplingCenter);
    Put16(vertex + 34, (uint16_t)(count + 1));
    CHECK(Made(device) == mrhi_errorUnsupported, "vertex outputs past the limit");
    CHECK(mrhiGetDeviceMisuse(device) == 0, "limits are not misuse");
    Close(device);
    // With more invocations than any one size, each size is checked.
    s_adapter.limits.workgroupInvocations = 1024;
    mrhiDeviceDef def = mrhiDefaultDeviceDef();
    def.limits.workgroupInvocations = 1024;
    device = OpenWith(def, true);
    CHECK(WithWorkgroup(device, 256, 4, 1, 0) == mrhi_success, "1024 invocations granted");
    CHECK(WithWorkgroup(device, limits.workgroupSizeX + 1, 1, 1, 0) == mrhi_errorUnsupported,
          "x past its own limit");
    CHECK(WithWorkgroup(device, 1, limits.workgroupSizeY + 1, 1, 0) == mrhi_errorUnsupported,
          "y past its own limit");
    Close(device);
}

static void TestStateAndFailure(void)
{
    Reset();
    Assemble();
    mrhiDevice* device = Open(2, false);
    mrhiShaderDef def = Def();
    mrhiShaderId a;
    mrhiShaderId b;
    CHECK(mrhiCreateShader(device, &def, &a) == mrhi_errorState, "not ready yet");
    mrhiInstanceNotification record;
    CHECK(mrhiNextInstanceNotification(s_instance, &record) == mrhi_success, "ready");
    s_failAllocations = true;
    CHECK(mrhiCreateShader(device, &def, &a) == mrhi_errorCapacity, "the allocator fails");
    s_failAllocations = false;
    CHECK(mrhiCreateShader(device, &def, &a) == mrhi_success, "one");
    CHECK(mrhiCreateShader(device, &def, &b) == mrhi_success, "no slot was lost");
    Close(device);
    s_adapter.objectsBeforeFailure = 1;
    device = Open(2, true);
    CHECK(mrhiCreateShader(device, &def, &a) == mrhi_success, "the one the driver makes");
    CHECK(mrhiCreateShader(device, &def, &b) == mrhi_errorPlatform, "the driver fails");
    CHECK(mrhiCreateShader(device, &def, &b) == mrhi_errorPlatform, "again, no slot lost");
    CHECK(mrhiDestroyShader(device, a) == mrhi_success, "the made one destroyed");
    Close(device);
}

// Reads a written container and checks what it holds: its digest in
// hex, its entry and binding counts, its root block, and its heap uses
// (a device granting both bindless features reads those).
static int CheckFile(char** args)
{
    FILE* file = fopen(args[0], "rb");
    CHECK(file != nullptr, "the file opens");
    if (file == nullptr)
    {
        return 1;
    }
    s_size = fread(s_container, 1, sizeof(s_container), file);
    fclose(file);
    uint8_t digest[MRHI_DIGEST_BYTES] = {0};
    const char* hex = args[1];
    CHECK(strlen(hex) == 2 * MRHI_DIGEST_BYTES, "a digest in hex");
    for (size_t i = 0; i < 2 * MRHI_DIGEST_BYTES && hex[i] != '\0'; ++i)
    {
        const char* digit = strchr("0123456789abcdef", hex[i]);
        CHECK(digit != nullptr, "a hex digit");
        uint8_t value = digit == nullptr ? 0 : (uint8_t)(digit - "0123456789abcdef");
        digest[i / 2] = (uint8_t)(digest[i / 2] << 4 | value);
    }
    uint32_t heapUses = (uint32_t)atoi(args[5]);
    mrhiDevice* device = heapUses == 0 ? Open(1, true) : OpenBindless(true);
    mrhiShaderDef def = Def();
    mrhiShaderId shader;
    mrhiShaderInfo info = {0};
    CHECK(mrhiCreateShader(device, &def, &shader) == mrhi_success, "the library reads it");
    CHECK(mrhiGetShaderInfo(device, shader, &info) == mrhi_success, "its info");
    CHECK(memcmp(info.digest, digest, MRHI_DIGEST_BYTES) == 0, "the same digest");
    CHECK(info.entryCount == (uint32_t)atoi(args[2]) &&
              info.bindingCount == (uint32_t)atoi(args[3]) &&
              info.rootBlockBytes == (uint32_t)atoi(args[4]) && info.heapUses == heapUses,
          "the same reflection");
    Close(device);
    return s_failures == 0 ? 0 : 1;
}

int main(int argc, char** argv)
{
    ResetAdapter();
    if (argc == 7)
    {
        return CheckFile(argv + 1);
    }
    TestDefault();
    TestHeader();
    TestSections();
    TestMeta();
    TestEntries();
    TestInterfaces();
    TestVariables();
    TestBuiltinsAndStorage();
    TestHeapUses();
    TestBindings();
    TestConstantsAndCode();
    TestMetalCode();
    TestMetalMap();
    TestMetalSources();
    TestRecordLimit();
    TestEveryFlip();
    TestCreate();
    TestRefusals();
    TestFeatures();
    TestCapabilities();
    TestHeapFeatures();
    TestLimits();
    TestTableSize();
    TestEntryLimits();
    TestStateAndFailure();
    return s_failures == 0 ? 0 : 1;
}
