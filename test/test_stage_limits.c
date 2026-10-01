// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Per-stage resource limits fitted under a total: kept when they fit,
// the largest brought down to a common cap otherwise, never below the
// floor, and SwiftShader's limits under its maxPerStageResources.

#include "stage_limits.h"
#include "test_harness.h"

static mrhiLimits With(uint32_t sampled, uint32_t samplers, uint32_t storage,
                       uint32_t storageTextures, uint32_t uniforms)
{
    mrhiLimits limits = mrhiDefaultLimits();
    limits.sampledTexturesPerStage = sampled;
    limits.samplersPerStage = samplers;
    limits.storageBuffersPerStage = storage;
    limits.storageTexturesPerStage = storageTextures;
    limits.uniformBuffersPerStage = uniforms;
    return limits;
}

static uint32_t Sum(const mrhiLimits* limits)
{
    return limits->sampledTexturesPerStage + limits->samplersPerStage +
           limits->storageBuffersPerStage + limits->storageTexturesPerStage +
           limits->uniformBuffersPerStage;
}

static void TestKept(void)
{
    mrhiLimits limits = With(100, 50, 30, 10, 10);
    mrhiFitStageLimits(&limits, 200);
    CHECK(limits.sampledTexturesPerStage == 100 && limits.samplersPerStage == 50 &&
              limits.storageBuffersPerStage == 30 && Sum(&limits) == 200,
          "limits that fit kept, at the total exactly");
    limits = With(1u << 20, 1u << 20, 1u << 20, 1u << 20, 1u << 20);
    mrhiFitStageLimits(&limits, UINT32_MAX);
    CHECK(limits.samplersPerStage == 1u << 20, "an unbounded total keeps everything");
}

static void TestCapped(void)
{
    // SwiftShader's: 200 in all, less 8 color attachments.
    mrhiLimits limits = With(200, 64, 30, 16, 15);
    mrhiFitStageLimits(&limits, 192);
    CHECK(Sum(&limits) <= 192, "under the total");
    CHECK(limits.storageBuffersPerStage == 30 && limits.storageTexturesPerStage == 16 &&
              limits.uniformBuffersPerStage == 15,
          "kinds under the cap kept");
    CHECK(limits.sampledTexturesPerStage == 67 && limits.samplersPerStage == 64 &&
              Sum(&limits) == 192,
          "the largest kind brought to the highest cap that fits");
    limits = With(200, 64, 30, 16, 15);
    mrhiFitStageLimits(&limits, 160);
    CHECK(limits.sampledTexturesPerStage == 49 && limits.samplersPerStage == 49 &&
              Sum(&limits) == 159,
          "both larger kinds brought to one cap, a sum short of the total");
}

static void TestFloor(void)
{
    mrhiLimits floor = mrhiDefaultLimits();
    mrhiLimits limits = With(1000, 1000, 1000, 1000, 1000);
    mrhiFitStageLimits(&limits, 56);
    CHECK(limits.sampledTexturesPerStage == floor.sampledTexturesPerStage &&
              limits.samplersPerStage == floor.samplersPerStage &&
              limits.storageBuffersPerStage == floor.storageBuffersPerStage &&
              limits.storageTexturesPerStage == floor.storageTexturesPerStage &&
              limits.uniformBuffersPerStage == floor.uniformBuffersPerStage,
          "down to the floor's 56 exactly");
    limits = With(1000, 1000, 1000, 1000, 1000);
    mrhiFitStageLimits(&limits, 0);
    CHECK(Sum(&limits) == 56, "never below the floor, though the total is less");
    limits = With(1000, 1000, 1000, 1000, 1000);
    mrhiFitStageLimits(&limits, 120);
    CHECK(Sum(&limits) <= 120 && limits.storageTexturesPerStage >= 4,
          "the floor's kinds above it keep their floor");
}

int main(void)
{
    TestKept();
    TestCapped();
    TestFloor();
    return s_failures == 0 ? 0 : 1;
}
