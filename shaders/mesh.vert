#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"
#include "instance.glsl"
#include "tangents.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUV;
layout(location = 3) out vec4 outColor;

void main()
{
    mat4 model = instanceMatrix();
    vec4 world = model * vec4(inPosition, 1.0);
    // Inverse-transpose keeps normals correct under non-uniform scaling.
    mat3 normalMatrix = transpose(inverse(mat3(model)));

    outWorldPos = world.xyz;
    outNormal = normalize(normalMatrix * inNormal);
    writeTangentFrame(model, outNormal);
    outUV = inUV;
    outColor = inColor;
    gl_Position = frame.viewProjection * world;
}
