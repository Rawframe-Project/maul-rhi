// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The instanced container's fragment entry "fs": the instance's color.

#version 450

layout(location = 0) in vec4 varyingColor;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = varyingColor;
}
