// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A Vulkan device's floor, features and limits from its facts
// (mrhi-0003), for devices no machine of the tests has: from a device
// that has everything, each feature the floor or a contract feature
// needs is taken away alone, and only what needs it goes; a queue
// family with no timestamp bits has no timestamps; the heaps' sizes are
// the smallest of their update after bind limits less the tables'
// reserve, 0 without bindless sampling and the storage limits counted
// only with heterogeneous heaps; multiview's views are at most 32.

#include "capabilities_core.h"
#include "test_harness.h"
#include "vulkan_facts.h"

#include <stddef.h>

// A device that has every feature and generous limits.
static mrhiVulkanFacts Full(void)
{
    mrhiVulkanFacts facts = {.family = 0, .familyTimestampBits = 64};
    VkPhysicalDeviceFeatures* core = &facts.features.features;
    VkBool32* bools = (VkBool32*)core;
    for (size_t i = 0; i < sizeof(*core) / sizeof(VkBool32); i++)
    {
        bools[i] = VK_TRUE;
    }
    VkPhysicalDeviceVulkan11Features* f11 = &facts.features11;
    f11->storageBuffer16BitAccess = f11->uniformAndStorageBuffer16BitAccess = VK_TRUE;
    f11->multiview = VK_TRUE;
    VkPhysicalDeviceVulkan12Features* f12 = &facts.features12;
    f12->timelineSemaphore = f12->bufferDeviceAddress = f12->descriptorIndexing = VK_TRUE;
    f12->shaderFloat16 = f12->drawIndirectCount = VK_TRUE;
    f12->runtimeDescriptorArray = f12->descriptorBindingPartiallyBound = VK_TRUE;
    f12->descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
    f12->descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    f12->shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    f12->descriptorBindingStorageImageUpdateAfterBind = VK_TRUE;
    f12->descriptorBindingStorageBufferUpdateAfterBind = VK_TRUE;
    f12->shaderStorageImageArrayNonUniformIndexing = VK_TRUE;
    f12->shaderStorageBufferArrayNonUniformIndexing = VK_TRUE;
    facts.features13.dynamicRendering = facts.features13.synchronization2 = VK_TRUE;
    facts.mutableType.mutableDescriptorType = VK_TRUE;
    VkPhysicalDeviceLimits* limits = &facts.properties.properties.limits;
    limits->timestampComputeAndGraphics = VK_TRUE;
    limits->maxBoundDescriptorSets = 8;
    facts.properties11.subgroupSupportedStages = VK_SHADER_STAGE_ALL;
    facts.properties11.subgroupSupportedOperations = 0xFFu;
    facts.properties11.maxMultiviewViewCount = 64;
    VkPhysicalDeviceVulkan12Properties* bind = &facts.properties12;
    bind->maxPerStageDescriptorUpdateAfterBindSampledImages = 100000;
    bind->maxDescriptorSetUpdateAfterBindSampledImages = 100000;
    bind->maxPerStageUpdateAfterBindResources = 100000;
    bind->maxUpdateAfterBindDescriptorsInAllPools = 100000;
    bind->maxPerStageDescriptorUpdateAfterBindStorageImages = 100000;
    bind->maxDescriptorSetUpdateAfterBindStorageImages = 100000;
    bind->maxPerStageDescriptorUpdateAfterBindStorageBuffers = 100000;
    bind->maxDescriptorSetUpdateAfterBindStorageBuffers = 100000;
    bind->maxPerStageDescriptorUpdateAfterBindSamplers = 100000;
    bind->maxDescriptorSetUpdateAfterBindSamplers = 100000;
    return facts;
}

static mrhiFeatures FeaturesOf(const mrhiVulkanFacts* facts)
{
    return mrhiVulkanFeaturesOf(facts, true, true);
}

static bool Floor(const mrhiVulkanFacts* facts)
{
    return mrhiVulkanMeetsFloor(facts);
}

static bool F16(const mrhiVulkanFacts* facts)
{
    return FeaturesOf(facts).shaderF16;
}

static bool CountedDraws(const mrhiVulkanFacts* facts)
{
    return FeaturesOf(facts).multiDrawIndirectCount;
}

static bool Sampling(const mrhiVulkanFacts* facts)
{
    return FeaturesOf(facts).bindlessSampling;
}

static bool Heterogeneous(const mrhiVulkanFacts* facts)
{
    return FeaturesOf(facts).bindlessHeterogeneous;
}

static bool Subgroups(const mrhiVulkanFacts* facts)
{
    return FeaturesOf(facts).subgroups;
}

// A feature of the facts taken away, and what must go with it.
typedef struct Taken
{
    size_t offset;
    bool (*holds)(const mrhiVulkanFacts* facts);
    const char* name;
} Taken;

#define CORE(field)      offsetof(mrhiVulkanFacts, features.features.field)
#define F11(field)       offsetof(mrhiVulkanFacts, features11.field)
#define F12(field)       offsetof(mrhiVulkanFacts, features12.field)
#define F13(field)       offsetof(mrhiVulkanFacts, features13.field)
#define TAKEN(at, holds) {at, holds, #at}

static const Taken s_taken[] = {
    TAKEN(CORE(fullDrawIndexUint32), Floor),
    TAKEN(CORE(imageCubeArray), Floor),
    TAKEN(CORE(independentBlend), Floor),
    TAKEN(CORE(sampleRateShading), Floor),
    TAKEN(CORE(depthBiasClamp), Floor),
    TAKEN(CORE(fragmentStoresAndAtomics), Floor),
    TAKEN(CORE(samplerAnisotropy), Floor),
    TAKEN(CORE(shaderStorageImageExtendedFormats), Floor),
    TAKEN(F13(dynamicRendering), Floor),
    TAKEN(F13(synchronization2), Floor),
    TAKEN(F12(timelineSemaphore), Floor),
    TAKEN(F12(bufferDeviceAddress), Floor),
    TAKEN(F12(descriptorIndexing), Floor),
    TAKEN(F12(shaderFloat16), F16),
    TAKEN(F11(storageBuffer16BitAccess), F16),
    TAKEN(F11(uniformAndStorageBuffer16BitAccess), F16),
    TAKEN(CORE(multiDrawIndirect), CountedDraws),
    TAKEN(F12(drawIndirectCount), CountedDraws),
    TAKEN(F12(runtimeDescriptorArray), Sampling),
    TAKEN(F12(descriptorBindingPartiallyBound), Sampling),
    TAKEN(F12(descriptorBindingUpdateUnusedWhilePending), Sampling),
    TAKEN(F12(descriptorBindingSampledImageUpdateAfterBind), Sampling),
    TAKEN(F12(shaderSampledImageArrayNonUniformIndexing), Sampling),
    TAKEN(F12(descriptorBindingStorageImageUpdateAfterBind), Heterogeneous),
    TAKEN(F12(descriptorBindingStorageBufferUpdateAfterBind), Heterogeneous),
    TAKEN(F12(shaderStorageImageArrayNonUniformIndexing), Heterogeneous),
    TAKEN(F12(shaderStorageBufferArrayNonUniformIndexing), Heterogeneous),
    TAKEN(CORE(shaderStorageImageReadWithoutFormat), Heterogeneous),
    TAKEN(CORE(shaderStorageImageWriteWithoutFormat), Heterogeneous),
    TAKEN(offsetof(mrhiVulkanFacts, mutableType.mutableDescriptorType), Heterogeneous),
};

static void TestTakenAlone(void)
{
    const mrhiVulkanFacts full = Full();
    for (size_t i = 0; i < sizeof(s_taken) / sizeof(s_taken[0]); i++)
    {
        CHECK(s_taken[i].holds(&full), s_taken[i].name);
        mrhiVulkanFacts facts = full;
        *(VkBool32*)((char*)&facts + s_taken[i].offset) = VK_FALSE;
        CHECK(!s_taken[i].holds(&facts), s_taken[i].name);
        // Only what needs the feature goes: the floor stays for a
        // contract feature's, and the sampling heaps for the
        // heterogeneous heaps'.
        CHECK(s_taken[i].holds == Floor || mrhiVulkanMeetsFloor(&facts), s_taken[i].name);
        CHECK(s_taken[i].holds != Heterogeneous || Sampling(&facts), s_taken[i].name);
    }
}

static void TestFloorAndFeatures(void)
{
    mrhiVulkanFacts facts = Full();
    mrhiFeatures all = FeaturesOf(&facts);
    CHECK(mrhiVulkanMeetsFloor(&facts) && all.timestampQuery && all.pipelineStatisticsQuery &&
              all.textureCompressionBc && all.textureCompressionEtc2 &&
              all.textureCompressionAstc && all.float32Filterable && all.rg11b10Renderable &&
              all.dualSourceBlending && all.unclippedDepth && all.shaderF16 && all.subgroups &&
              all.shaderInt64 && all.indirectFirstInstance && all.multiDrawIndirectCount &&
              all.multiview && all.bindlessSampling && all.bindlessHeterogeneous,
          "a device with everything has every feature");
    mrhiFeatures formats = mrhiVulkanFeaturesOf(&facts, false, false);
    CHECK(!formats.float32Filterable && !formats.rg11b10Renderable,
          "the formats' features as their queries found them");
    facts.family = UINT32_MAX;
    CHECK(!mrhiVulkanMeetsFloor(&facts), "no queue family with graphics and compute");
    facts = Full();
    facts.familyTimestampBits = 0;
    CHECK(!FeaturesOf(&facts).timestampQuery, "a queue family without timestamp bits");
    facts = Full();
    facts.properties.properties.limits.timestampComputeAndGraphics = VK_FALSE;
    CHECK(!FeaturesOf(&facts).timestampQuery, "timestamps not in every queue");
    facts = Full();
    facts.properties.properties.limits.maxBoundDescriptorSets = 4;
    CHECK(!Sampling(&facts), "no set left after the four tables, no bindless sampling");
    facts.properties.properties.limits.maxBoundDescriptorSets = 5;
    CHECK(Sampling(&facts), "a fifth set is enough");
    facts = Full();
    facts.properties11.subgroupSupportedStages = VK_SHADER_STAGE_COMPUTE_BIT;
    CHECK(!Subgroups(&facts), "subgroups only in compute shaders");
    facts = Full();
    facts.properties11.subgroupSupportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT;
    CHECK(!Subgroups(&facts), "only the basic subgroup operations");
}

static void TestLimits(void)
{
    mrhiVulkanFacts facts = Full();
    VkPhysicalDeviceVulkan12Properties* bind = &facts.properties12;
    bind->maxPerStageDescriptorUpdateAfterBindStorageImages = 5000;
    bind->maxPerStageDescriptorUpdateAfterBindSamplers = 3000;
    const uint32_t tables = 4u * MRHI_TABLE_BINDINGS;
    mrhiLimits limits = mrhiVulkanLimitsOf(&facts);
    CHECK(limits.heapSize == 5000 - tables - MRHI_COLOR_TARGETS &&
              limits.samplerHeapSize == 3000 - tables && limits.multiviewViews == 32,
          "the heaps by their smallest limits less the reserve, 32 views at most");
    facts.mutableType.mutableDescriptorType = VK_FALSE;
    CHECK(mrhiVulkanLimitsOf(&facts).heapSize == 100000 - tables - MRHI_COLOR_TARGETS,
          "the storage limits counted only for heterogeneous heaps");
    facts.features12.runtimeDescriptorArray = VK_FALSE;
    limits = mrhiVulkanLimitsOf(&facts);
    CHECK(limits.heapSize == 0 && limits.samplerHeapSize == 0, "no heaps without bindless");
    facts = Full();
    facts.properties12.maxPerStageDescriptorUpdateAfterBindSamplers = 10;
    facts.properties11.maxMultiviewViewCount = 6;
    limits = mrhiVulkanLimitsOf(&facts);
    CHECK(limits.samplerHeapSize == 0 && limits.multiviewViews == 6,
          "a heap under the reserve is 0, and fewer views kept");
    facts.features11.multiview = VK_FALSE;
    CHECK(mrhiVulkanLimitsOf(&facts).multiviewViews == 1, "one view without multiview");
}

int main(void)
{
    TestTakenAlone();
    TestFloorAndFeatures();
    TestLimits();
    return s_failures == 0 ? 0 : 1;
}
