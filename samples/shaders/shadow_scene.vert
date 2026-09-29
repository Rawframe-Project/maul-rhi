// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The shadow sample's scene entry "place": a vertex placed as the depth
// entry places it, with where it falls in the shadow map and its depth
// there. The camera looks as the light does.

#version 450

layout(location = 0) in vec3 position;
layout(location = 0) out vec3 light;

void main()
{
    light = vec3(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5, position.z);
    gl_Position = vec4(position, 1.0);
}
