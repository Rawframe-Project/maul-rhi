// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Shader containers on a device: the container checked as hostile
// input and against the device, its reflection kept for the shader
// and its pipelines, and its code handed to the driver.

#include "device_core.h"
#include "label.h"

#include <string.h>

#define SHADER_DEF_COOKIE 0x6D727368u

mrhiShaderDef mrhiDefaultShaderDef(void)
{
    mrhiShaderDef def = {0};
    def.cookie = SHADER_DEF_COOKIE;
    return def;
}

// Whether a container's bindings fit limits: slots, binding
// sizes, and each stage's count of each kind. Tables and color outputs
// need no check: the floor's four tables and eight color attachments are
// the container's own bounds.
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

// Whether an entry fits limits: a compute entry's
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

// Whether a device has the features a container's code needs.
static bool HasFeatures(const mrhiDevice* device, const mrhiContainer* container)
{
    const mrhiFeatures* features = &device->features;
    mrhiShaderHeapUses heterogeneous = mrhi_heapUseStorageTextures | mrhi_heapUseStorageBuffers;
    return (!container->float16 || features->shaderF16) &&
           (!container->subgroups || features->subgroups) &&
           (!container->int64 || features->shaderInt64) &&
           (container->builtins & mrhi_builtinPrimitiveIndex) == 0 &&
           (container->heapUses == 0 || features->bindlessSampling) &&
           ((container->builtins & mrhi_builtinViewIndex) == 0 || features->multiview) &&
           ((container->heapUses & heterogeneous) == 0 || features->bindlessHeterogeneous);
}

// Whether a container fits limits: its root block, bindings and entries.
static bool FitsLimits(const mrhiLimits* limits, const mrhiContainer* container)
{
    if (container->rootBlockBytes > limits->rootBlockBytes || !AreBindingsWithin(limits, container))
    {
        return false;
    }
    for (uint32_t i = 0; i < container->entryCount; ++i)
    {
        if (!IsEntryWithin(limits, container, mrhiContainerEntry(container, i)))
        {
            return false;
        }
    }
    return true;
}

// Whether each of a container's four tables holds at most
// MRHI_MAX_TABLE_BINDINGS bindings, so that a table set while recording fits
// one chunk.
static bool AreTablesWithin(const mrhiContainer* container)
{
    uint32_t tables[4] = {0};
    for (uint32_t i = 0; i < container->bindingCount; ++i)
    {
        if (++tables[mrhiContainerBinding(container, i).table] > MRHI_MAX_TABLE_BINDINGS)
        {
            return false;
        }
    }
    return true;
}

// Checks a def and its container: success with the container, or the
// refusal, invalid input counted as misuse and a bad container not.
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
        return mrhiDeviceMisuse(device, mrhi_diagnosticShaderBytes);
    }
    status = mrhiParseContainer(def->bytes, def->byteCount, containerOut);
    if (status == mrhi_errorFormat || (status == mrhi_success && !AreTablesWithin(containerOut)))
    {
        return mrhiDeviceBadData(device, mrhi_diagnosticShaderContainer);
    }
    if (status != mrhi_success)
    {
        return status;
    }
    if (!HasFeatures(device, containerOut))
    {
        return mrhi_errorUnsupported;
    }
    return FitsLimits(&device->limits, containerOut)
               ? mrhi_success
               : mrhiPastLimits(FitsLimits(&device->adapterLimits, containerOut));
}

mrhiResult mrhiCreateShader(mrhiDevice* device, const mrhiShaderDef* def, mrhiShaderId* shaderOut)
{
    if (shaderOut != nullptr)
    {
        *shaderOut = (mrhiShaderId){0};
    }
    if (device == nullptr)
    {
        return mrhi_errorInvalid;
    }
    if (def == nullptr || shaderOut == nullptr)
    {
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
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
    slot->reflection = mrhiKeepReflection(&device->allocator, &container);
    if (slot->reflection == nullptr)
    {
        mrhiPoolRelease(&device->shaders, index1);
        return mrhi_errorCapacity;
    }
    mrhiShaderDef driverDef = *def;
    mrhiDropLabel(&driverDef.label, &driverDef.labelLength);
    status =
        mrhiDriverStatus(device, device->driver.vtable->createShader(
                                     device->driver.self, &driverDef, &container, &slot->handle));
    if (status != mrhi_success)
    {
        mrhiReleaseReflection(&device->allocator, slot->reflection);
        *slot = (mrhiShaderSlot){0};
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
    mrhiReleaseReflection(&device->allocator, slot->reflection);
    *slot = (mrhiShaderSlot){0};
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
        return mrhiDeviceMisuse(device, mrhi_diagnosticNullArgument);
    }
    if (!mrhiPoolIsLive(&device->shaders, shader.index1, shader.generation))
    {
        return mrhi_errorStale;
    }
    const mrhiReflection* reflection = device->shaderSlots[shader.index1 - 1].reflection;
    *infoOut = (mrhiShaderInfo){
        .entryCount = reflection->entryCount,
        .bindingCount = reflection->bindingCount,
        .rootBlockBytes = reflection->rootBlockBytes,
        .heapUses = reflection->heapUses,
    };
    memcpy(infoOut->digest, reflection->digest, MRHI_DIGEST_BYTES);
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
            mrhiReleaseReflection(&device->allocator, slot->reflection);
            *slot = (mrhiShaderSlot){0};
        }
    }
}
