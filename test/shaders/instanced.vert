// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The instanced container's vertex entry "vs": a position per vertex and
// a color per instance, the color passed on.

#version 450

layout(location = 0) in vec4 position;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 varyingColor;

void main()
{
    varyingColor = color;
    gl_Position = position;
}
