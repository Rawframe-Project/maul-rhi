// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The f16 container's WGSL: the entry of f16.comp.

enable f16;

@group(0) @binding(0) var<storage, read> inputs: array<f16>;
@group(0) @binding(1) var<storage, read_write> outputs: array<f16>;

@compute @workgroup_size(4)
fn cs(@builtin(global_invocation_id) id: vec3u) {
    outputs[id.x] = inputs[id.x * 2u] * inputs[id.x * 2u + 1u] + 0.5h;
}
