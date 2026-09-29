// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The triangle sample's vertex entry "vs": three corners from the
// vertex index alone.

#version 450

void main()
{
    const vec2 corners[3] = vec2[3](vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.0, 0.5));
    gl_Position = vec4(corners[gl_VertexIndex], 0.0, 1.0);
}
