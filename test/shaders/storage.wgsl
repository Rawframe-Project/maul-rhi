// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The storage container's WGSL: the entry of storage.comp.

@group(0) @binding(0) var<storage, read> inputs: array<u32>;
@group(0) @binding(1) var<storage, read_write> outputs: array<u32>;
@group(0) @binding(2) var image: texture_storage_2d<rgba8unorm, write>;

@compute @workgroup_size(4)
fn cs(@builtin(global_invocation_id) id: vec3u) {
    let value = inputs[id.x];
    outputs[id.x] = value * 2u + 1u;
    textureStore(image, vec2i(i32(id.x), 0), vec4f(f32(value) / 255.0, 0.0, 0.0, 1.0));
}
