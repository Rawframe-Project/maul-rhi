// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The noop container's vertex entry "vs": a point at the origin, for the
// CTS cases' pipelines that read nothing (record mrhi-0021).

#version 450

void main()
{
    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
}
