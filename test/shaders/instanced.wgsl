// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The instanced container's WGSL: the entries of instanced.vert and
// instanced.frag.

struct Varying {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
}

@vertex
fn vs(@location(0) position: vec4f, @location(1) color: vec4f) -> Varying {
    return Varying(position, color);
}

@fragment
fn fs(@location(0) color: vec4f) -> @location(0) vec4f {
    return color;
}
