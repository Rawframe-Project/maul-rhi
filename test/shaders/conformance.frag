// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The conformance container's fragment entry "fs": a color from the
// root block, a uniform buffer in table 0 and a sampled texture in
// table 1.

#version 450

layout(push_constant) uniform Root
{
    vec4 tint;
} root;

layout(set = 0, binding = 0) uniform Scene
{
    vec4 color;
} scene;

layout(set = 1, binding = 0) uniform texture2D image;
layout(set = 1, binding = 1) uniform sampler linearSampler;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 texel = texture(sampler2D(image, linearSampler), vec2(0.5));
    outColor = scene.color * root.tint * texel;
}
