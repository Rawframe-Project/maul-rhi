// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The textured sample's vertex entry "vs": a position and texture
// coordinates from a vertex buffer, scaled and moved by a uniform
// buffer in table 0.

#version 450

layout(set = 0, binding = 0) uniform Transform
{
    vec4 scaleOffset;
} transform;

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 uv;
layout(location = 0) out vec2 outUv;

void main()
{
    outUv = uv;
    gl_Position = vec4(position * transform.scaleOffset.xy + transform.scaleOffset.zw, 0.0, 1.0);
}
