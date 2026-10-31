// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The fixed-width types of the public headers, as the family rules
// set them: every enumeration of kinds, modes and choices is int32_t,
// as mrhiResult is; bitflags and the packed struct type are uint32_t.
// The structs made only of such types keep the layout that follows.

#include "test_harness.h"

#include "maul-rhi/base.h"
#include "maul-rhi/capabilities.h"
#include "maul-rhi/device.h"
#include "maul-rhi/frame.h"
#include "maul-rhi/heap.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/resources.h"
#include "maul-rhi/shader.h"
#include "maul-rhi/surface.h"

#include <stddef.h>

// A kind: four bytes, signed.
#define KIND(type) CHECK(sizeof(type) == 4 && (type)(-1) < 0, #type " is int32_t")

// A set of bits or a packed value: four bytes, unsigned.
#define BITS(type) CHECK(sizeof(type) == 4 && (type)(-1) > 0, #type " is uint32_t")

static void TestKinds(void)
{
    KIND(mrhiResult);
    KIND(mrhiDiagnosticCode);
    KIND(mrhiDriverKind);
    KIND(mrhiAdapterKind);
    KIND(mrhiPowerPreference);
    KIND(mrhiInstanceNotificationKind);
    KIND(mrhiFormat);
    KIND(mrhiDeviceState);
    KIND(mrhiDeviceLossReason);
    KIND(mrhiFilter);
    KIND(mrhiAddressMode);
    KIND(mrhiCompareFunction);
    KIND(mrhiTextureKind);
    KIND(mrhiTextureAspect);
    KIND(mrhiQueryKind);
    KIND(mrhiHeapEntryKind);
    KIND(mrhiColorPrimaries);
    KIND(mrhiTransfer);
    KIND(mrhiColorRange);
    KIND(mrhiPresentMode);
    KIND(mrhiAlphaMode);
    KIND(mrhiDeviceNotificationKind);
    KIND(mrhiPassClass);
    KIND(mrhiAccessKind);
    KIND(mrhiLoadOp);
    KIND(mrhiStoreOp);
    KIND(mrhiResourceState);
    KIND(mrhiBindingKind);
    KIND(mrhiSamplerBinding);
    KIND(mrhiSampleType);
    KIND(mrhiStorageAccess);
    KIND(mrhiScalarType);
    KIND(mrhiInterpolation);
    KIND(mrhiSampling);
    KIND(mrhiConstantType);
    KIND(mrhiVertexFormat);
    KIND(mrhiVertexStepMode);
    KIND(mrhiPrimitiveTopology);
    KIND(mrhiIndexFormat);
    KIND(mrhiFrontFace);
    KIND(mrhiCullMode);
    KIND(mrhiStencilOperation);
    KIND(mrhiBlendFactor);
    KIND(mrhiBlendOperation);
}

static void TestBits(void)
{
    BITS(mrhiStructType);
    BITS(mrhiSampleCounts);
    BITS(mrhiBufferUsage);
    BITS(mrhiTextureUsage);
    BITS(mrhiPresentModes);
    BITS(mrhiAlphaModes);
    BITS(mrhiShaderStages);
    BITS(mrhiShaderBuiltins);
    BITS(mrhiShaderHeapUses);
    BITS(mrhiColorWrites);
}

// Structs of kinds alone: four bytes a member, no padding.
static void TestLayouts(void)
{
    CHECK(sizeof(mrhiSurfaceColor) == 16 && offsetof(mrhiSurfaceColor, primaries) == 4 &&
              offsetof(mrhiSurfaceColor, transfer) == 8 && offsetof(mrhiSurfaceColor, range) == 12,
          "a surface color");
    CHECK(sizeof(mrhiBlendComponent) == 12 && offsetof(mrhiBlendComponent, dstFactor) == 4 &&
              offsetof(mrhiBlendComponent, operation) == 8,
          "a blend component");
    CHECK(sizeof(mrhiStencilFace) == 16 && offsetof(mrhiStencilFace, failOp) == 4 &&
              offsetof(mrhiStencilFace, depthFailOp) == 8 &&
              offsetof(mrhiStencilFace, passOp) == 12,
          "a stencil face");
    CHECK(sizeof(mrhiDiagnostic) == 8 && offsetof(mrhiDiagnostic, callsRefused) == 4,
          "a diagnostic");
    CHECK(sizeof(mrhiFormatCaps) == 12 && offsetof(mrhiFormatCaps, sampleCounts) == 8,
          "format caps: five bools, then the mask at four bytes");
    CHECK(sizeof(mrhiChain) == sizeof(void*) * 2 && offsetof(mrhiChain, type) == sizeof(void*),
          "a chain node: its next, then its type");
}

int main(void)
{
    TestKinds();
    TestBits();
    TestLayouts();
    return s_failures == 0 ? 0 : 1;
}
