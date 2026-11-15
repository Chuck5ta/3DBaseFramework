#version 450
#extension GL_GOOGLE_include_directive : require

// The shadow pass: draws the scene's depth as the sun sees it, into the shadow map.

#include "common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec2 outUV;
layout(location = 1) out float outAlpha;

void main()
{
    outUV = inUV;
    outAlpha = inColor.a;
    gl_Position = frame.shadowMatrix * object.model * vec4(inPosition, 1.0);
}
