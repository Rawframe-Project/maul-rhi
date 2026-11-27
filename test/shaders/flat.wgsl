// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The flat container's WGSL: the entries of flat.comp and flat.gi.comp,
// each writing 1 to the first word from flattened index 0.

requires linear_indexing;

@group(0) @binding(0) var<storage, read_write> words: array<u32>;

@compute @workgroup_size(1, 1, 1)
fn wi(@builtin(workgroup_index) index: u32) {
    if (index == 0u) {
        words[0] = 1u;
    }
}

@compute @workgroup_size(64, 1, 1)
fn gi(@builtin(global_invocation_index) index: u32) {
    if (index == 0u) {
        words[0] = 1u;
    }
}
