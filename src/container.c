// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The container's parse: the header and digest, the section table's
// bounds and overlaps, then every record against the rules of
// docs/contract/container.md. Every read is bounded by the sizes checked
// before it, and bytes are read one at a time, so nothing depends on the
// caller's alignment.

#include "container.h"

#include "capabilities_core.h"
#include "label.h"
#include "sha256.h"

#include <stdckdint.h>
#include <string.h>

#define HEADER_BYTES   64
#define SECTION_BYTES  24
#define ENTRY_BYTES    32
#define BINDING_BYTES  24
#define INPUT_BYTES    8
#define OUTPUT_BYTES   8
#define CONSTANT_BYTES 16
#define SECTION_TYPES  9
#define MAX_SECTIONS   64
#define MAX_ROOT_BLOCK 256
#define MAX_NAME       256
// The records an array section holds at most, which also bounds the
// uniqueness checks' work.
#define MAX_RECORDS 4096
#define SPIRV_MAGIC 0x07230203u

enum
{
    SECTION_META = 1,
    SECTION_STRINGS,
    SECTION_ENTRIES,
    SECTION_BINDINGS,
    SECTION_INPUTS,
    SECTION_OUTPUTS,
    SECTION_CONSTANTS,
    SECTION_SPIRV,
    SECTION_WGSL,
};

static uint16_t Read16(const uint8_t* at)
{
    return (uint16_t)(at[0] | at[1] << 8);
}

static uint32_t Read32(const uint8_t* at)
{
    return (uint32_t)at[0] | (uint32_t)at[1] << 8 | (uint32_t)at[2] << 16 | (uint32_t)at[3] << 24;
}

static uint64_t Read64(const uint8_t* at)
{
    return (uint64_t)Read32(at) | (uint64_t)Read32(at + 4) << 32;
}

static bool IsZero(const uint8_t* at, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (at[i] != 0)
        {
            return false;
        }
    }
    return true;
}

// A section as the table gives it.
typedef struct Section
{
    uint32_t type;
    uint64_t offset;
    uint64_t size;
} Section;

// Checks the header: success with the section count, version or invalid.
static mrhiResult CheckHeader(const uint8_t* bytes, size_t size, uint32_t* countOut)
{
    if (size < HEADER_BYTES || bytes[0] != 'M' || bytes[1] != 'R' || bytes[2] != 'S' ||
        bytes[3] != 'C')
    {
        return mrhi_errorInvalid;
    }
    if (Read32(bytes + 4) != 1)
    {
        return mrhi_errorVersion;
    }
    uint32_t count = Read32(bytes + 48);
    if (Read64(bytes + 8) != size || !IsZero(bytes + 52, 12) || count > MAX_SECTIONS ||
        count * (uint64_t)SECTION_BYTES > size - HEADER_BYTES)
    {
        return mrhi_errorInvalid;
    }
    uint8_t digest[MRHI_DIGEST_BYTES];
    mrhiSha256(bytes + 48, size - 48, digest);
    for (int i = 0; i < MRHI_DIGEST_BYTES; ++i)
    {
        if (digest[i] != bytes[16 + i])
        {
            return mrhi_errorInvalid;
        }
    }
    *countOut = count;
    return mrhi_success;
}

// Reads and checks the section table: every section after the table,
// inside the container, 8-byte aligned, apart from the others, and each
// known type at most once.
static bool ReadSections(const uint8_t* bytes, size_t size, uint32_t count, Section* sections)
{
    uint64_t tableEnd = HEADER_BYTES + (uint64_t)count * SECTION_BYTES;
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t* record = bytes + HEADER_BYTES + (size_t)i * SECTION_BYTES;
        Section section = {Read32(record), Read64(record + 8), Read64(record + 16)};
        uint64_t end = 0;
        if (Read32(record + 4) != 0 || section.offset % 8 != 0 || section.offset < tableEnd ||
            ckd_add(&end, section.offset, section.size) || end > size)
        {
            return false;
        }
        for (uint32_t j = 0; j < i; ++j)
        {
            bool apart = sections[j].offset + sections[j].size <= section.offset ||
                         end <= sections[j].offset;
            bool repeated = sections[j].type == section.type && section.type <= SECTION_TYPES;
            if (!apart || repeated)
            {
                return false;
            }
        }
        sections[i] = section;
    }
    return true;
}

// Finds a section by type: its bytes and size, or NULL when absent.
static const uint8_t* FindSection(const uint8_t* bytes, const Section* sections, uint32_t count,
                                  uint32_t type, uint64_t* sizeOut)
{
    for (uint32_t i = 0; i < count; ++i)
    {
        if (sections[i].type == type)
        {
            *sizeOut = sections[i].size;
            return bytes + sections[i].offset;
        }
    }
    *sizeOut = 0;
    return nullptr;
}

// Takes an array section: false when its size is not a whole number of
// records, or holds more than MAX_RECORDS.
static bool TakeArray(const uint8_t* bytes, const Section* sections, uint32_t count, uint32_t type,
                      uint32_t recordBytes, const uint8_t** arrayOut, uint32_t* countOut)
{
    uint64_t size = 0;
    *arrayOut = FindSection(bytes, sections, count, type, &size);
    *countOut = (uint32_t)(size / recordBytes);
    return size % recordBytes == 0 && size / recordBytes <= MAX_RECORDS;
}

mrhiShaderEntry mrhiContainerEntry(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* at = container->entries + (size_t)index * ENTRY_BYTES;
    return (mrhiShaderEntry){
        .stage = Read32(at),
        .nameOffset = Read32(at + 4),
        .nameLength = Read32(at + 8),
        .workgroup = {Read32(at + 12), Read32(at + 16), Read32(at + 20)},
        .firstInput = Read16(at + 24),
        .inputCount = Read16(at + 26),
        .firstOutput = Read16(at + 28),
        .outputCount = Read16(at + 30),
    };
}

mrhiShaderBinding mrhiContainerBinding(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* at = container->bindings + (size_t)index * BINDING_BYTES;
    return (mrhiShaderBinding){
        .table = at[0],
        .kind = at[1],
        .slot = Read16(at + 2),
        .stages = Read32(at + 4),
        .sampler = at[8],
        .sampleType = at[9],
        .viewDimension = at[10],
        .access = at[11],
        .format = Read16(at + 12),
        .multisampled = at[14] != 0,
        .minSize = Read64(at + 16),
    };
}

mrhiShaderInput mrhiContainerInput(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* at = container->inputs + (size_t)index * INPUT_BYTES;
    return (mrhiShaderInput){.location = Read32(at), .format = at[4]};
}

mrhiShaderOutput mrhiContainerOutput(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* at = container->outputs + (size_t)index * OUTPUT_BYTES;
    return (mrhiShaderOutput){.location = Read32(at), .kind = at[4]};
}

mrhiShaderConstant mrhiContainerConstant(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* at = container->constants + (size_t)index * CONSTANT_BYTES;
    return (mrhiShaderConstant){.id = Read32(at), .type = at[4], .bits = Read32(at + 8)};
}

// Whether every vertex input and color output is well formed on its
// own, whether or not an entry names it.
static bool AreInterfaceRecordsValid(const mrhiContainer* container)
{
    for (uint32_t i = 0; i < container->inputCount; ++i)
    {
        mrhiShaderInput input = mrhiContainerInput(container, i);
        if (input.format == mrhi_vertexNone || input.format > mrhi_vertexUnorm1010102 ||
            !IsZero(container->inputs + (size_t)i * INPUT_BYTES + 5, 3))
        {
            return false;
        }
    }
    for (uint32_t i = 0; i < container->outputCount; ++i)
    {
        mrhiShaderOutput output = mrhiContainerOutput(container, i);
        if (output.kind == mrhi_outputNone || output.kind > mrhi_outputUint ||
            output.location >= MRHI_COLOR_TARGETS ||
            !IsZero(container->outputs + (size_t)i * OUTPUT_BYTES + 5, 3))
        {
            return false;
        }
    }
    return true;
}

// Whether a range of records lies in an array of count.
static bool InArray(uint32_t first, uint32_t length, uint32_t count)
{
    return first + length <= count;
}

// Whether an entry is well formed, its name unique among those before.
static bool IsEntryValid(const mrhiContainer* container, uint32_t index)
{
    mrhiShaderEntry entry = mrhiContainerEntry(container, index);
    bool stage = entry.stage == mrhi_stageVertex || entry.stage == mrhi_stageFragment ||
                 entry.stage == mrhi_stageCompute;
    bool compute = entry.stage == mrhi_stageCompute;
    bool workgroup =
        compute ? entry.workgroup[0] > 0 && entry.workgroup[1] > 0 && entry.workgroup[2] > 0
                : entry.workgroup[0] == 0 && entry.workgroup[1] == 0 && entry.workgroup[2] == 0;
    bool name =
        entry.nameLength > 0 && entry.nameLength <= MAX_NAME &&
        (uint64_t)entry.nameOffset + entry.nameLength <= container->stringBytes &&
        mrhiIsTextValid((const char*)container->strings + entry.nameOffset, entry.nameLength);
    bool inputs = InArray(entry.firstInput, entry.inputCount, container->inputCount) &&
                  (entry.inputCount == 0 || entry.stage == mrhi_stageVertex);
    bool outputs = InArray(entry.firstOutput, entry.outputCount, container->outputCount) &&
                   (entry.outputCount == 0 || entry.stage == mrhi_stageFragment);
    if (!stage || !workgroup || !name || !inputs || !outputs)
    {
        return false;
    }
    for (uint32_t i = 0; i < index; ++i)
    {
        mrhiShaderEntry other = mrhiContainerEntry(container, i);
        if (other.nameLength == entry.nameLength &&
            memcmp(container->strings + other.nameOffset, container->strings + entry.nameOffset,
                   entry.nameLength) == 0)
        {
            return false;
        }
    }
    return true;
}

// Whether an entry's inputs, and its outputs, have unique locations.
static bool AreLocationsUnique(const mrhiContainer* container, mrhiShaderEntry entry)
{
    for (uint32_t i = 0; i < entry.inputCount; ++i)
    {
        uint32_t location = mrhiContainerInput(container, entry.firstInput + i).location;
        for (uint32_t j = 0; j < i; ++j)
        {
            if (mrhiContainerInput(container, entry.firstInput + j).location == location)
            {
                return false;
            }
        }
    }
    for (uint32_t i = 0; i < entry.outputCount; ++i)
    {
        uint32_t location = mrhiContainerOutput(container, entry.firstOutput + i).location;
        for (uint32_t j = 0; j < i; ++j)
        {
            if (mrhiContainerOutput(container, entry.firstOutput + j).location == location)
            {
                return false;
            }
        }
    }
    return true;
}

// Whether a binding's details suit its kind.
static bool AreDetailsValid(const mrhiShaderBinding* binding)
{
    bool buffer = binding->kind == mrhi_bindingUniformBuffer ||
                  binding->kind == mrhi_bindingStorageBuffer ||
                  binding->kind == mrhi_bindingReadOnlyStorageBuffer;
    bool sampler = binding->kind == mrhi_bindingSampler;
    bool sampled = binding->kind == mrhi_bindingSampledTexture;
    bool storage = binding->kind == mrhi_bindingStorageTexture;
    bool samplerOk = sampler ? binding->sampler >= mrhi_samplerFiltering &&
                                   binding->sampler <= mrhi_samplerComparison
                             : binding->sampler == mrhi_samplerNone;
    bool sampleOk =
        sampled ? binding->sampleType >= mrhi_sampleFloat && binding->sampleType <= mrhi_sampleUint
                : binding->sampleType == mrhi_sampleNone;
    bool accessOk = storage ? binding->access >= mrhi_storageReadOnly &&
                                  binding->access <= mrhi_storageReadWrite &&
                                  mrhiIsFormatKnown(binding->format)
                            : binding->access == mrhi_storageNone && binding->format == 0;
    bool texture = sampled || storage;
    bool dimension = !texture  ? binding->viewDimension == 0
                     : storage ? binding->viewDimension == mrhi_texture2d ||
                                     binding->viewDimension == mrhi_texture2dArray ||
                                     binding->viewDimension == mrhi_texture3d
                               : binding->viewDimension <= mrhi_texture3d;
    bool multisampled =
        !binding->multisampled || (sampled && binding->viewDimension == mrhi_texture2d &&
                                   binding->sampleType != mrhi_sampleFloat);
    return samplerOk && sampleOk && accessOk && dimension && multisampled &&
           (buffer || binding->minSize == 0);
}

// Whether a binding is well formed, its table and slot unique among those
// before.
static bool IsBindingValid(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* raw = container->bindings + (size_t)index * BINDING_BYTES;
    mrhiShaderBinding binding = mrhiContainerBinding(container, index);
    bool writable =
        binding.kind == mrhi_bindingStorageBuffer ||
        (binding.kind == mrhi_bindingStorageTexture && binding.access != mrhi_storageReadOnly);
    bool stages = binding.stages != 0 && (binding.stages & ~mrhiShaderStagesKnown) == 0 &&
                  !(writable && (binding.stages & mrhi_stageVertex) != 0);
    if (raw[14] > 1 || raw[15] != 0 || binding.table >= 4 || binding.kind == mrhi_bindingNone ||
        binding.kind > mrhi_bindingStorageTexture || !stages || !AreDetailsValid(&binding))
    {
        return false;
    }
    for (uint32_t i = 0; i < index; ++i)
    {
        mrhiShaderBinding other = mrhiContainerBinding(container, i);
        if (other.table == binding.table && other.slot == binding.slot)
        {
            return false;
        }
    }
    return true;
}

// Whether a constant is well formed, its id unique among those before.
static bool IsConstantValid(const mrhiContainer* container, uint32_t index)
{
    const uint8_t* raw = container->constants + (size_t)index * CONSTANT_BYTES;
    mrhiShaderConstant constant = mrhiContainerConstant(container, index);
    if (!IsZero(raw + 5, 3) || !IsZero(raw + 12, 4) || constant.type == mrhi_constantNone ||
        constant.type > mrhi_constantFloat32 ||
        (constant.type == mrhi_constantBool && constant.bits > 1))
    {
        return false;
    }
    for (uint32_t i = 0; i < index; ++i)
    {
        if (mrhiContainerConstant(container, i).id == constant.id)
        {
            return false;
        }
    }
    return true;
}

// Whether the records agree with the rules and with each other.
static bool AreRecordsValid(const mrhiContainer* container)
{
    if (container->entryCount == 0 || !AreInterfaceRecordsValid(container))
    {
        return false;
    }
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        if (!IsEntryValid(container, i) ||
            !AreLocationsUnique(container, mrhiContainerEntry(container, i)))
        {
            return false;
        }
    }
    for (uint32_t i = 0; i < container->bindingCount; ++i)
    {
        if (!IsBindingValid(container, i))
        {
            return false;
        }
    }
    for (uint32_t i = 0; i < container->constantCount; ++i)
    {
        if (!IsConstantValid(container, i))
        {
            return false;
        }
    }
    return true;
}

// Takes the meta, strings and code sections. An absent section has size
// 0, so the size checks require the meta and code; the strings are
// required by the entries' names.
static bool TakeParts(const uint8_t* bytes, const Section* sections, uint32_t count,
                      mrhiContainer* container)
{
    uint64_t size = 0;
    const uint8_t* meta = FindSection(bytes, sections, count, SECTION_META, &size);
    if (size != 16 || !IsZero(meta + 4, 12))
    {
        return false;
    }
    container->rootBlockBytes = Read32(meta);
    container->strings = FindSection(bytes, sections, count, SECTION_STRINGS, &size);
    container->stringBytes = (uint32_t)size;
    bool strings = size <= UINT32_MAX;
    container->spirv = FindSection(bytes, sections, count, SECTION_SPIRV, &size);
    container->spirvBytes = size;
    bool spirv = size >= 20 && size % 4 == 0 && Read32(container->spirv) == SPIRV_MAGIC;
    container->wgsl = FindSection(bytes, sections, count, SECTION_WGSL, &size);
    container->wgslBytes = size;
    bool wgsl = size > 0 && mrhiIsTextValid((const char*)container->wgsl, size);
    return container->rootBlockBytes % 4 == 0 && container->rootBlockBytes <= MAX_ROOT_BLOCK &&
           strings && spirv && wgsl;
}

mrhiResult mrhiParseContainer(const void* bytes, size_t size, mrhiContainer* containerOut)
{
    const uint8_t* data = bytes;
    uint32_t count = 0;
    mrhiResult status = CheckHeader(data, size, &count);
    if (status != mrhi_success)
    {
        return status;
    }
    Section sections[MAX_SECTIONS];
    mrhiContainer container = {0};
    for (int i = 0; i < MRHI_DIGEST_BYTES; ++i)
    {
        container.digest[i] = data[16 + i];
    }
    bool valid = ReadSections(data, size, count, sections) &&
                 TakeParts(data, sections, count, &container) &&
                 TakeArray(data, sections, count, SECTION_ENTRIES, ENTRY_BYTES, &container.entries,
                           &container.entryCount) &&
                 TakeArray(data, sections, count, SECTION_BINDINGS, BINDING_BYTES,
                           &container.bindings, &container.bindingCount) &&
                 TakeArray(data, sections, count, SECTION_INPUTS, INPUT_BYTES, &container.inputs,
                           &container.inputCount) &&
                 TakeArray(data, sections, count, SECTION_OUTPUTS, OUTPUT_BYTES, &container.outputs,
                           &container.outputCount) &&
                 TakeArray(data, sections, count, SECTION_CONSTANTS, CONSTANT_BYTES,
                           &container.constants, &container.constantCount) &&
                 AreRecordsValid(&container);
    if (!valid)
    {
        return mrhi_errorInvalid;
    }
    *containerOut = container;
    return mrhi_success;
}
