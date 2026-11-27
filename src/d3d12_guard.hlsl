// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Guards an indirect dispatch's workgroup counts as WebGPU's
// dispatchWorkgroupsIndirect does: the three counts at t0 are copied to
// u0, or zeros when one passes `most`, the device's workgroupsPerDimension,
// or when the entry reads a flattened index (`flat`: 1 for
// workgroup_index, 2 for global_invocation_index) that the dispatch's
// workgroups, or its workgroups times `invocations`, pass 32 bits for.
// tools/gen_kernels.py compiles it to src/generated/d3d12_guard.h.

#define GUARD_ROOT                                                                                 \
    "RootConstants(num32BitConstants=3, b0), SRV(t0), UAV(u0)"

cbuffer Shape : register(b0)
{
    uint most;
    uint flat;
    uint invocations;
};

ByteAddressBuffer arguments : register(t0);
RWByteAddressBuffer guarded : register(u0);

// Whether a times b stays within 32 bits, and the product when it does.
bool Times(uint a, uint b, out uint product)
{
    product = a * b;
    return a == 0 || b <= 0xFFFFFFFFu / a;
}

[RootSignature(GUARD_ROOT)]
[numthreads(1, 1, 1)]
void main()
{
    uint3 counts = arguments.Load3(0);
    bool fits = counts.x <= most && counts.y <= most && counts.z <= most;
    uint workgroups = 0;
    uint total = 0;
    bool grouped = Times(counts.x, counts.y, workgroups) && Times(workgroups, counts.z, total);
    uint threads = 0;
    bool spread = grouped && Times(total, invocations, threads);
    fits = fits && ((flat & 1u) == 0 || grouped) && ((flat & 2u) == 0 || spread);
    guarded.Store3(0, fits ? counts : uint3(0, 0, 0));
}
