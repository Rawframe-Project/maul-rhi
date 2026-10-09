// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The dual container's vertex entry "vs": a triangle over the whole
// target from the vertex index.

#version 450

void main()
{
    vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
