// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The compute sample's planning WGSL: the entry of compute_plan.comp.

struct Root {
    count: u32,
}

var<immediate> root: Root;

@group(0) @binding(0) var<storage, read_write> groups: array<u32, 3>;

@compute @workgroup_size(1)
fn plan() {
    groups[0] = (root.count + 63u) / 64u;
    groups[1] = 1u;
    groups[2] = 1u;
}
