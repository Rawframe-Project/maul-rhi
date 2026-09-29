// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The shadow sample's scene WGSL: the entries of shadow_scene.vert,
// .lit.frag and .solid.frag.

struct Root {
    color: vec4f,
}

var<immediate> root: Root;

@group(0) @binding(0) var shadowMap: texture_depth_2d;
@group(0) @binding(1) var shadowSampler: sampler_comparison;

struct Varyings {
    @builtin(position) position: vec4f,
    @location(0) light: vec3f,
}

@vertex
fn place(@location(0) position: vec3f) -> Varyings {
    var out: Varyings;
    out.light = vec3f(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5, position.z);
    out.position = vec4f(position, 1.0);
    return out;
}

@fragment
fn lit(@location(0) light: vec3f) -> @location(0) vec4f {
    let shade = textureSampleCompare(shadowMap, shadowSampler, light.xy, light.z + 0.01);
    return vec4f(mix(vec3f(0.25), vec3f(1.0), shade), 1.0);
}

@fragment
fn solid() -> @location(0) vec4f {
    return root.color;
}
