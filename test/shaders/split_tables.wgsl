// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The split_tables container's WGSL: the entry of split_tables.comp.

@group(0) @binding(0) var<storage, read> inputs: array<u32>;
@group(2) @binding(0) var<storage, read_write> outputs: array<u32>;

@compute @workgroup_size(1)
fn cs() {
    outputs[0] = inputs[0] + 1u;
}
