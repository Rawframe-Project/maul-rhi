// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The compute sample's squaring WGSL: the entry of compute_square.comp.

struct Root {
    count: u32,
}

var<immediate> root: Root;

@group(0) @binding(0) var<storage, read_write> values: array<u32>;

@compute @workgroup_size(64)
fn square(@builtin(global_invocation_id) id: vec3u) {
    if (id.x < root.count) {
        values[id.x] = id.x * id.x;
    }
}
