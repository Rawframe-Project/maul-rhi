// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// A triangle covering the target, colored by the view it is drawn in:
// red in view 0, green in view 1 (record mrhi-0020).

#version 450
#extension GL_EXT_multiview : require

layout(location = 0) out vec4 color;

void main()
{
    vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
    color = gl_ViewIndex == 0 ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 1.0, 0.0, 1.0);
}
