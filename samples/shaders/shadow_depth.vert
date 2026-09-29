// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The shadow sample's depth entry "place": a vertex's position and
// depth from a vertex buffer, seen from the light.

#version 450

layout(location = 0) in vec3 position;

void main()
{
    gl_Position = vec4(position, 1.0);
}
