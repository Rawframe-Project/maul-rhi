// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The lone_table container's WGSL: the entry of lone_table.comp.

@group(1) @binding(0) var<storage, read_write> words: array<u32>;

@compute @workgroup_size(1)
fn cs() {
    words[0] = 1u;
}
