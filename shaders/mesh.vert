#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;

void main()
{
    vec4 world = object.model * vec4(inPosition, 1.0);
    mat3 normalMatrix = mat3(object.normalMatrix[0].xyz, object.normalMatrix[1].xyz, object.normalMatrix[2].xyz);

    outWorldPos = world.xyz;
    outNormal = normalMatrix * inNormal;
    outUV = inUV;
    gl_Position = frame.viewProjection * world;
}
