#version 450
#extension GL_GOOGLE_include_directive : require

// The shadow pass writes depth only. Alpha-masked materials (leaves, fences) cut their
// holes out of the shadow too.

#include "common.glsl"

layout(set = 1, binding = 0) uniform sampler2D albedoTexture;

layout(location = 0) in vec2 inUV;
layout(location = 1) in float inAlpha;

void main()
{
    float cutoff = object.emissive.a;
    if (cutoff > 0.0 && texture(albedoTexture, inUV).a * object.color.a * inAlpha < cutoff)
        discard;
}
