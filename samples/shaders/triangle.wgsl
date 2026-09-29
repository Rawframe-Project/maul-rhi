// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The triangle sample's WGSL: the entries of triangle.vert and .frag.

struct Root {
    color: vec4f,
}

var<immediate> root: Root;

@vertex
fn vs(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    var corners = array<vec2f, 3>(vec2f(-0.5, -0.5), vec2f(0.5, -0.5), vec2f(0.0, 0.5));
    return vec4f(corners[index], 0.0, 1.0);
}

@fragment
fn fs() -> @location(0) vec4f {
    return root.color;
}
