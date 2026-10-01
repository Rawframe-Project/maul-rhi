// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The Vulkan driver's limits of binding (mrhi-0017): SwiftShader's on the
// Android emulator, whose four descriptor sets and 200 resources a stage
// once found no adapter, and a desktop device's, kept as they are.

#include "capabilities_core.h"
#include "test_harness.h"
#include "vulkan_adapter.h"

static mrhiLimits Granted(const VkPhysicalDeviceLimits* limits, uint32_t vertexBuffers)
{
    mrhiLimits granted = mrhiDefaultLimits();
    granted.bindingTables = limits->maxBoundDescriptorSets;
    granted.vertexBuffers = vertexBuffers;
    mrhiVulkanBindingLimits(limits, &granted);
    return granted;
}

static void TestSwiftShader(void)
{
    const VkPhysicalDeviceLimits limits = {
        .maxBoundDescriptorSets = 4,
        .maxPerStageDescriptorSampledImages = 200,
        .maxPerStageDescriptorSamplers = 64,
        .maxPerStageDescriptorStorageBuffers = 30,
        .maxPerStageDescriptorStorageImages = 16,
        .maxPerStageDescriptorUniformBuffers = 15,
        .maxPerStageResources = 200,
        .maxColorAttachments = 8,
    };
    mrhiLimits granted = Granted(&limits, 16);
    mrhiLimits floor = mrhiDefaultLimits();
    CHECK(granted.bindingsPerTable == floor.bindingsPerTable, "the contract's slots, not 200");
    CHECK(granted.sampledTexturesPerStage + granted.samplersPerStage +
                  granted.storageBuffersPerStage + granted.storageTexturesPerStage +
                  granted.uniformBuffersPerStage <=
              200 - 8,
          "a stage's kinds fit its resources with the attachments");
    CHECK(granted.sampledTexturesPerStage == 67 && granted.samplersPerStage == 64,
          "the largest kind capped");
    CHECK(granted.tablesPlusVertexBuffers == floor.tablesPlusVertexBuffers,
          "four tables and sixteen vertex buffers read as the contract's 24");
    CHECK(mrhiLimitsWithin(&floor, &granted), "the floor met");
}

static void TestDesktop(void)
{
    const VkPhysicalDeviceLimits limits = {
        .maxBoundDescriptorSets = 32,
        .maxPerStageDescriptorSampledImages = 1048576,
        .maxPerStageDescriptorSamplers = 1048576,
        .maxPerStageDescriptorStorageBuffers = 1048576,
        .maxPerStageDescriptorStorageImages = 1048576,
        .maxPerStageDescriptorUniformBuffers = 15,
        .maxPerStageResources = UINT32_MAX,
        .maxColorAttachments = 8,
    };
    mrhiLimits granted = Granted(&limits, 16);
    CHECK(granted.sampledTexturesPerStage == 1048576 && granted.uniformBuffersPerStage == 15,
          "kinds under an unbounded total kept");
    CHECK(granted.tablesPlusVertexBuffers == 48, "the sum when it is larger");
}

int main(void)
{
    TestSwiftShader();
    TestDesktop();
    return s_failures == 0 ? 0 : 1;
}
