// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The noop container's WGSL: the entries of noop.vert, .frag and .comp.

@vertex
fn vs() -> @builtin(position) vec4f {
    return vec4f(0.0, 0.0, 0.0, 1.0);
}

@fragment
fn fs() -> @location(0) vec4f {
    return vec4f(0.0, 0.0, 0.0, 1.0);
}

@compute @workgroup_size(1)
fn cs() {
}
