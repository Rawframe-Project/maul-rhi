// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The conformance container's fragment entry "fr": the root block's
// color alone.

#version 450

layout(push_constant) uniform Root
{
    vec4 tint;
} root;

layout(location = 0) out vec4 outColor;

void main()
{
    outColor = root.tint;
}
