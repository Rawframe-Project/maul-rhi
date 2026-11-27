// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The constants container's WGSL: the entry of constants.comp.

@id(0) override flag: bool = false;
@id(1) override offset: i32 = 7;
@id(2) override scale: f32 = 0.5;
@id(3) override count: u32 = 9u;

@group(0) @binding(0) var<storage, read_write> outputs: array<u32>;

@compute @workgroup_size(1)
fn cs() {
    outputs[0] = select(0u, 1u, flag);
    outputs[1] = bitcast<u32>(offset);
    outputs[2] = bitcast<u32>(scale);
    outputs[3] = count;
}
