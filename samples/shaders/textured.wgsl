// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The textured sample's WGSL: the entries of textured.vert and .frag.

struct Transform {
    scaleOffset: vec4f,
}

@group(0) @binding(0) var<uniform> transform: Transform;
@group(1) @binding(0) var image: texture_2d<f32>;
@group(1) @binding(1) var imageSampler: sampler;

struct Varyings {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@vertex
fn vs(@location(0) position: vec2f, @location(1) uv: vec2f) -> Varyings {
    var out: Varyings;
    out.uv = uv;
    out.position = vec4f(position * transform.scaleOffset.xy + transform.scaleOffset.zw, 0.0, 1.0);
    return out;
}

@fragment
fn fs(@location(0) uv: vec2f) -> @location(0) vec4f {
    return textureSample(image, imageSampler, uv);
}
