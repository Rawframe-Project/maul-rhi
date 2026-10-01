// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Writes the color its view gave it.

#version 450

layout(location = 0) in vec4 color;
layout(location = 0) out vec4 target;

void main()
{
    target = color;
}
