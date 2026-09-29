// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// The shadow sample's fragment entry "lit": white where nothing nearer
// the light covers the fragment, dark gray in shadow. Depth is reversed,
// nearer is greater, so the comparison sampler passes where the
// fragment's depth, with a small bias, is at least the shadow map's.

#version 450

layout(set = 0, binding = 0) uniform texture2D shadowMap;
layout(set = 0, binding = 1) uniform samplerShadow shadowSampler;

layout(location = 0) in vec3 light;
layout(location = 0) out vec4 outColor;

void main()
{
    float lit = texture(sampler2DShadow(shadowMap, shadowSampler), vec3(light.xy, light.z + 0.01));
    outColor = vec4(mix(vec3(0.25), vec3(1.0), lit), 1.0);
}
