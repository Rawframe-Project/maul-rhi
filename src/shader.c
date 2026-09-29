// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Shader containers on a device (mrhi-0009): the container checked as
// hostile input, its reflection kept in one block from the device's
// allocator, and its code handed to the driver.

#include "allocator.h"
#include "device_core.h"

#include <stdalign.h>
#include <stddef.h>
#include <string.h>

#define SHADER_DEF_COOKIE 0x6D727368u

mrhiShaderDef mrhiDefaultShaderDef(void)
{
    mrhiShaderDef def = {0};
    def.cookie = SHADER_DEF_COOKIE;
    return def;
}

// Where each part of a shader's reflection block starts.
typedef struct ReflectionParts
{
    size_t entries;
    size_t bindings;
    size_t inputs;
    size_t outputs;
    size_t variables;
    size_t constants;
    size_t names;
} ReflectionParts;

// The bytes of a container's entries' names.
static size_t NameBytes(const mrhiContainer* container)
{
    size_t bytes = 0;
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        bytes += mrhiContainerEntry(container, i).nameLength;
    }
    return bytes;
}

// Copies a container's reflection into a new block for the slot: true,
// or false when the allocator fails.
static bool KeepReflection(const mrhiAllocator* allocator, const mrhiContainer* container,
                           mrhiShaderSlot* slot)
{
    mrhiLayout layout = {0};
    ReflectionParts parts = {
        .entries = mrhiLayoutAdd(&layout, container->entryCount, sizeof(mrhiShaderEntry),
                                 alignof(mrhiShaderEntry)),
        .bindings = mrhiLayoutAdd(&layout, container->bindingCount, sizeof(mrhiShaderBinding),
                                  alignof(mrhiShaderBinding)),
        .inputs = mrhiLayoutAdd(&layout, container->inputCount, sizeof(mrhiShaderVariable),
                                alignof(mrhiShaderVariable)),
        .outputs = mrhiLayoutAdd(&layout, container->outputCount, sizeof(mrhiShaderVariable),
                                 alignof(mrhiShaderVariable)),
        .variables = mrhiLayoutAdd(&layout, container->variableCount, sizeof(mrhiShaderVariable),
                                   alignof(mrhiShaderVariable)),
        .constants = mrhiLayoutAdd(&layout, container->constantCount, sizeof(mrhiShaderConstant),
                                   alignof(mrhiShaderConstant)),
        .names = mrhiLayoutAdd(&layout, NameBytes(container), 1, 1),
    };
    unsigned char* block =
        layout.overflow ? nullptr : mrhiAllocate(allocator, layout.size, alignof(max_align_t));
    if (block == nullptr)
    {
        return false;
    }
    slot->reflection = block;
    slot->reflectionBytes = layout.size;
    slot->entries = (mrhiShaderEntry*)(block + parts.entries);
    slot->bindings = (mrhiShaderBinding*)(block + parts.bindings);
    slot->inputs = (mrhiShaderVariable*)(block + parts.inputs);
    slot->outputs = (mrhiShaderVariable*)(block + parts.outputs);
    slot->variables = (mrhiShaderVariable*)(block + parts.variables);
    slot->constants = (mrhiShaderConstant*)(block + parts.constants);
    char* names = (char*)(block + parts.names);
    slot->names = names;
    uint32_t packed = 0;
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        mrhiShaderEntry entry = mrhiContainerEntry(container, i);
        memcpy(names + packed, container->strings + entry.nameOffset, entry.nameLength);
        entry.nameOffset = packed;
        packed += entry.nameLength;
        slot->entries[i] = entry;
    }
    for (uint32_t i = 0; i < container->bindingCount; ++i)
    {
        slot->bindings[i] = mrhiContainerBinding(container, i);
    }
    for (uint32_t i = 0; i < container->inputCount; ++i)
    {
        slot->inputs[i] = mrhiContainerInput(container, i);
    }
    for (uint32_t i = 0; i < container->outputCount; ++i)
    {
        slot->outputs[i] = mrhiContainerOutput(container, i);
    }
    for (uint32_t i = 0; i < container->variableCount; ++i)
    {
        slot->variables[i] = mrhiContainerVariable(container, i);
    }
    for (uint32_t i = 0; i < container->constantCount; ++i)
    {
        slot->constants[i] = mrhiContainerConstant(container, i);
    }
    memcpy(slot->digest, container->digest, MRHI_DIGEST_BYTES);
    slot->rootBlockBytes = container->rootBlockBytes;
    slot->entryCount = container->entryCount;
    slot->bindingCount = container->bindingCount;
    slot->inputCount = container->inputCount;
    slot->outputCount = container->outputCount;
    slot->variableCount = container->variableCount;
    slot->constantCount = container->constantCount;
    return true;
}

// Frees a slot's reflection and marks the slot free.
static void ReleaseReflection(const mrhiAllocator* allocator, mrhiShaderSlot* slot)
{
    mrhiRelease(allocator, slot->reflection, slot->reflectionBytes, alignof(max_align_t));
    *slot = (mrhiShaderSlot){0};
}

// Whether a container's bindings fit the device's limits: slots, binding
// sizes, and each stage's count of each kind. Tables and color outputs
// need no check: the floor's four tables and eight color attachments
// are the container's own bounds.
static bool AreBindingsWithin(const mrhiLimits* limits, const mrhiContainer* container)
{
    for (uint32_t stage = mrhi_stageVertex; stage <= mrhi_stageCompute; stage <<= 1)
    {
        uint32_t counts[mrhi_bindingStorageTexture + 1] = {0};
        for (uint32_t i = 0; i < container->bindingCount; ++i)
        {
            mrhiShaderBinding binding = mrhiContainerBinding(container, i);
            counts[binding.kind] += (binding.stages & stage) != 0 ? 1 : 0;
        }
        if (counts[mrhi_bindingUniformBuffer] > limits->uniformBuffersPerStage ||
            counts[mrhi_bindingStorageBuffer] + counts[mrhi_bindingReadOnlyStorageBuffer] >
                limits->storageBuffersPerStage ||
            counts[mrhi_bindingSampler] > limits->samplersPerStage ||
            counts[mrhi_bindingSampledTexture] > limits->sampledTexturesPerStage ||
            counts[mrhi_bindingStorageTexture] > limits->storageTexturesPerStage)
        {
            return false;
        }
    }
    for (uint32_t i = 0; i < container->bindingCount; ++i)
    {
        mrhiShaderBinding binding = mrhiContainerBinding(container, i);
        uint64_t bytes = binding.kind == mrhi_bindingUniformBuffer ? limits->uniformBindingBytes
                                                                   : limits->storageBindingBytes;
        if (binding.slot >= limits->bindingsPerTable || binding.minSize > bytes)
        {
            return false;
        }
    }
    return true;
}

// The fragment builtins an entry reads that count as inter-stage
// variables.
static uint32_t StageBuiltins(mrhiShaderBuiltins builtins)
{
    const mrhiShaderBuiltins counted[] = {mrhi_builtinFrontFacing, mrhi_builtinSampleIndex,
                                          mrhi_builtinSampleMaskIn, mrhi_builtinPrimitiveIndex};
    uint32_t count = 0;
    for (size_t i = 0; i < sizeof(counted) / sizeof(counted[0]); ++i)
    {
        count += (builtins & counted[i]) != 0 ? 1 : 0;
    }
    return count;
}

// Whether an entry fits the device's limits: a compute entry's
// workgroup, and the vertex inputs and inter-stage variables of the
// others, with the fragment builtins that count as variables.
static bool IsEntryWithin(const mrhiLimits* limits, const mrhiContainer* container,
                          mrhiShaderEntry entry)
{
    if (entry.stage == mrhi_stageCompute)
    {
        uint64_t invocations = (uint64_t)entry.workgroup[0] * entry.workgroup[1];
        return entry.workgroup[0] <= limits->workgroupSizeX &&
               entry.workgroup[1] <= limits->workgroupSizeY &&
               entry.workgroup[2] <= limits->workgroupSizeZ &&
               invocations * entry.workgroup[2] <= limits->workgroupInvocations &&
               entry.workgroupStorageBytes <= limits->workgroupStorageBytes;
    }
    // Unique input locations below the limit keep their count within it.
    if (entry.variableCount + StageBuiltins(entry.builtins) > limits->interStageVariables)
    {
        return false;
    }
    for (uint32_t i = 0; i < entry.inputCount; ++i)
    {
        if (mrhiContainerInput(container, entry.firstInput + i).location >=
            limits->vertexAttributes)
        {
            return false;
        }
    }
    for (uint32_t i = 0; i < entry.variableCount; ++i)
    {
        if (mrhiContainerVariable(container, entry.firstVariable + i).location >=
            limits->interStageVariables)
        {
            return false;
        }
    }
    return true;
}

// Whether a container fits the device: its root block, bindings and
// entries within the limits, and the features its code needs.
static bool IsWithin(const mrhiDevice* device, const mrhiContainer* container)
{
    if (container->rootBlockBytes > device->limits.rootBlockBytes ||
        (container->float16 && !device->features.shaderF16) ||
        (container->builtins & mrhi_builtinPrimitiveIndex) != 0 ||
        !AreBindingsWithin(&device->limits, container))
    {
        return false;
    }
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        if (!IsEntryWithin(&device->limits, container, mrhiContainerEntry(container, i)))
        {
            return false;
        }
    }
    return true;
}

// Checks a def and its container: success with the container, or the
// refusal, invalid input counted as misuse.
static mrhiResult CheckShaderDef(mrhiDevice* device, const mrhiShaderDef* def,
                                 mrhiContainer* containerOut)
{
    mrhiResult status = mrhiCheckObjectDef(device, MRHI_DEF_HEAD(def), SHADER_DEF_COOKIE);
    if (status != mrhi_success)
    {
        return status;
    }
    if (def->bytes == nullptr || (uintptr_t)def->bytes % 8 != 0)
    {
        return mrhiDeviceMisuse(device);
    }
    status = mrhiParseContainer(def->bytes, def->byteCount, containerOut);
    if (status == mrhi_errorInvalid)
    {
        return mrhiDeviceMisuse(device);
    }
    return status == mrhi_success && !IsWithin(device, containerOut) ? mrhi_errorUnsupported
                                                                     : status;
}

mrhiResult mrhiCreateShader(mrhiDevice* device, const mrhiShaderDef* def, mrhiShaderId* shaderOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || shaderOut == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    mrhiContainer container = {0};
    mrhiResult status = CheckShaderDef(device, def, &container);
    if (status != mrhi_success)
    {
        return status;
    }
    status = mrhiDeviceUsable(device);
    if (status != mrhi_success)
    {
        return status;
    }
    uint32_t index1 = 0;
    uint32_t generation = 0;
    if (!mrhiPoolAcquire(&device->shaders, &index1, &generation))
    {
        return mrhi_errorCapacity;
    }
    mrhiShaderSlot* slot = &device->shaderSlots[index1 - 1];
    if (!KeepReflection(&device->allocator, &container, slot))
    {
        mrhiPoolRelease(&device->shaders, index1);
        return mrhi_errorCapacity;
    }
    status =
        device->driver.vtable->createShader(device->driver.self, def, &container, &slot->handle);
    if (status != mrhi_success)
    {
        ReleaseReflection(&device->allocator, slot);
        mrhiPoolRelease(&device->shaders, index1);
        return status;
    }
    *shaderOut = (mrhiShaderId){index1, generation};
    return mrhi_success;
}

mrhiResult mrhiDestroyShader(mrhiDevice* device, mrhiShaderId shader)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (!mrhiPoolIsLive(&device->shaders, shader.index1, shader.generation))
    {
        return mrhi_errorStale;
    }
    mrhiShaderSlot* slot = &device->shaderSlots[shader.index1 - 1];
    device->driver.vtable->destroyShader(device->driver.self, slot->handle);
    ReleaseReflection(&device->allocator, slot);
    mrhiPoolRelease(&device->shaders, shader.index1);
    return mrhi_success;
}

mrhiResult mrhiGetShaderInfo(mrhiDevice* device, mrhiShaderId shader, mrhiShaderInfo* infoOut)
{
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (infoOut == nullptr)
    {
        return mrhiDeviceMisuse(device);
    }
    if (!mrhiPoolIsLive(&device->shaders, shader.index1, shader.generation))
    {
        return mrhi_errorStale;
    }
    const mrhiShaderSlot* slot = &device->shaderSlots[shader.index1 - 1];
    *infoOut = (mrhiShaderInfo){
        .entryCount = slot->entryCount,
        .bindingCount = slot->bindingCount,
        .rootBlockBytes = slot->rootBlockBytes,
    };
    memcpy(infoOut->digest, slot->digest, MRHI_DIGEST_BYTES);
    return mrhi_success;
}

void mrhiDestroyShaders(mrhiDevice* device)
{
    for (uint32_t i = 0; i < device->deviceLimits.shaders; ++i)
    {
        mrhiShaderSlot* slot = &device->shaderSlots[i];
        if (slot->reflection != nullptr)
        {
            device->driver.vtable->destroyShader(device->driver.self, slot->handle);
            ReleaseReflection(&device->allocator, slot);
        }
    }
}
