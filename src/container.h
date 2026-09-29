// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Shader containers (mrhi-0009, docs/contract/container.md): the parse
// that checks every byte as hostile input, and the decoded reflection.

#ifndef MAUL_RHI_SRC_CONTAINER_H
#define MAUL_RHI_SRC_CONTAINER_H

#include "maul-rhi/shader.h"

// An entry point: its stage, its name in the strings, its workgroup
// size and storage, the builtins and heaps it uses, and its ranges of
// vertex inputs, color outputs and inter-stage variables.
typedef struct mrhiShaderEntry
{
    mrhiShaderStages stage;
    uint32_t nameOffset;
    uint32_t nameLength;
    uint32_t workgroup[3];
    uint16_t firstInput;
    uint16_t inputCount;
    uint16_t firstOutput;
    uint16_t outputCount;
    uint16_t firstVariable;
    uint16_t variableCount;
    mrhiShaderBuiltins builtins;
    uint32_t workgroupStorageBytes;
    mrhiShaderHeapUses heapUses;
} mrhiShaderEntry;

typedef struct mrhiShaderBinding
{
    uint8_t table;
    mrhiBindingKind kind;
    uint16_t slot;
    mrhiShaderStages stages;
    mrhiSamplerBinding sampler;
    mrhiSampleType sampleType;
    mrhiTextureKind viewDimension;
    mrhiStorageAccess access;
    mrhiFormat format;
    bool multisampled;
    uint64_t minSize;
} mrhiShaderBinding;

// A vertex input, a color output or an inter-stage variable.
typedef struct mrhiShaderVariable
{
    uint32_t location;
    mrhiScalarType type;
    uint8_t components;
    mrhiInterpolation interpolation;
    mrhiSampling sampling;
} mrhiShaderVariable;

typedef struct mrhiShaderConstant
{
    uint32_t id;
    mrhiConstantType type;
    uint32_t bits;
    // Whether it has no default, so every pipeline sets it.
    bool required;
} mrhiShaderConstant;

// An entry's Metal code: its MSL's range in the MSL section (0 and 0
// without one) and its buffer sizes' index, or MRHI_METAL_NONE.
typedef struct mrhiMetalEntry
{
    uint32_t mslOffset;
    uint32_t mslLength;
    uint8_t sizesIndex;
} mrhiMetalEntry;

// A Metal index the map leaves unused.
#define MRHI_METAL_NONE 255

// A checked container: its digest and root block, its sections in the
// caller's bytes, whether it uses 16-bit floats, and the builtins and
// heap uses of its entries together; the WGSL is absent (NULL, 0 bytes)
// exactly when an entry uses a heap. The Metal map is NULL without Metal
// code, and the MSL and metallib each NULL and 0 bytes when absent.
typedef struct mrhiContainer
{
    uint8_t digest[MRHI_DIGEST_BYTES];
    uint32_t rootBlockBytes;
    bool float16;
    mrhiShaderBuiltins builtins;
    mrhiShaderHeapUses heapUses;
    const uint8_t* strings;
    uint32_t stringBytes;
    const uint8_t* entries;
    uint32_t entryCount;
    const uint8_t* bindings;
    uint32_t bindingCount;
    const uint8_t* inputs;
    uint32_t inputCount;
    const uint8_t* outputs;
    uint32_t outputCount;
    const uint8_t* variables;
    uint32_t variableCount;
    const uint8_t* constants;
    uint32_t constantCount;
    const uint8_t* spirv;
    size_t spirvBytes;
    const uint8_t* wgsl;
    size_t wgslBytes;
    const uint8_t* metalMap;
    size_t metalMapBytes;
    const uint8_t* msl;
    size_t mslBytes;
    const uint8_t* metallib;
    size_t metallibBytes;
} mrhiContainer;

// Checks a container: success with its sections, mrhi_errorVersion for
// another version, or mrhi_errorInvalid.
mrhiResult mrhiParseContainer(const void* bytes, size_t size, mrhiContainer* containerOut);

// The records of a checked container, by index.
mrhiShaderEntry mrhiContainerEntry(const mrhiContainer* container, uint32_t index);
mrhiShaderBinding mrhiContainerBinding(const mrhiContainer* container, uint32_t index);
mrhiShaderVariable mrhiContainerInput(const mrhiContainer* container, uint32_t index);
mrhiShaderVariable mrhiContainerOutput(const mrhiContainer* container, uint32_t index);
mrhiShaderVariable mrhiContainerVariable(const mrhiContainer* container, uint32_t index);
mrhiShaderConstant mrhiContainerConstant(const mrhiContainer* container, uint32_t index);

// The Metal map of a checked container with Metal code: the root block's
// buffer index (MRHI_METAL_NONE when it is empty), an entry's code, and a
// binding's index in its class.
uint8_t mrhiContainerMetalRoot(const mrhiContainer* container);
mrhiMetalEntry mrhiContainerMetalEntry(const mrhiContainer* container, uint32_t index);
uint8_t mrhiContainerMetalIndex(const mrhiContainer* container, uint32_t binding);

#endif // MAUL_RHI_SRC_CONTAINER_H
