// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The dual container's WGSL: the entries of dual.vert and dual.frag.

enable dual_source_blending;

struct Sources {
    @location(0) @blend_src(0) first: vec4f,
    @location(0) @blend_src(1) second: vec4f,
}

@vertex
fn vs(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    let corner = vec2f(f32((index << 1u) & 2u), f32(index & 2u));
    return vec4f(corner * 2.0 - 1.0, 0.0, 1.0);
}

@fragment
fn fs() -> Sources {
    return Sources(vec4f(0.25, 0.5, 0.75, 1.0), vec4f(0.5, 0.25, 0.25, 0.5));
}
