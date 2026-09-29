// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The textured sample's fragment entry "fs": the texture in table 1,
// through the sampler beside it.

#version 450

layout(set = 1, binding = 0) uniform texture2D image;
layout(set = 1, binding = 1) uniform sampler imageSampler;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = texture(sampler2D(image, imageSampler), uv);
}
