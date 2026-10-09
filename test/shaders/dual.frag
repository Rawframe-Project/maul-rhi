// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The dual container's fragment entry "fs" (dualSourceBlending): two
// blend sources at location 0.

#version 450

layout(location = 0, index = 0) out vec4 first;
layout(location = 0, index = 1) out vec4 second;

void main()
{
    first = vec4(0.25, 0.5, 0.75, 1.0);
    second = vec4(0.5, 0.25, 0.25, 0.5);
}
