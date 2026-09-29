// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The triangle sample's fragment entry "fs": the root block's color.

#version 450

layout(push_constant) uniform Root
{
    vec4 color;
} root;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = root.color;
}
