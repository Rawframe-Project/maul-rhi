// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The conformance container's WGSL: the entries of conformance.vert,
// .frag and .comp.

struct Root {
    tint: vec4f,
}

var<immediate> root: Root;

struct Scene {
    color: vec4f,
}

@group(0) @binding(0) var<uniform> scene: Scene;
@group(0) @binding(1) var<storage, read_write> data: array<u32>;
@group(1) @binding(0) var image: texture_2d<f32>;
@group(1) @binding(1) var linearSampler: sampler;

@id(0) override scale: u32 = 1u;

@vertex
fn vs(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    let corner = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
    return vec4f(corner * 2.0 - 1.0, 0.0, 1.0);
}

@fragment
fn fs() -> @location(0) vec4f {
    let texel = textureSample(image, linearSampler, vec2f(0.5));
    return scene.color * root.tint * texel;
}

@compute @workgroup_size(8)
fn cs(@builtin(global_invocation_id) id: vec3u) {
    data[id.x] *= scale;
}
