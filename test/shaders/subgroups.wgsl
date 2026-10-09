// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The subgroups container's WGSL: the entry of subgroups.comp.

enable subgroups;

@group(0) @binding(0) var<storage, read> inputs: array<u32>;
@group(0) @binding(1) var<storage, read_write> outputs: array<u32>;

@compute @workgroup_size(4)
fn cs(@builtin(global_invocation_id) id: vec3u) {
    outputs[id.x] = subgroupAdd(inputs[id.x]);
}
