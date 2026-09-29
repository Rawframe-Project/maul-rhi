// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The conformance container's vertex entry "vp": a vertex placed by a
// vertex buffer.

#version 450

layout(location = 0) in vec4 position;

void main()
{
    gl_Position = position;
}
