#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(set = 1, binding = 0) uniform sampler2D albedoTexture;

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 albedo = texture(albedoTexture, inUV) * object.color;

    vec3 N = normalize(inNormal);
    if (!gl_FrontFacing)
        N = -N; // correct lighting for double-sided / wireframe draws
    vec3 L = normalize(frame.sunDirection.xyz);
    vec3 V = normalize(frame.cameraPosition.xyz - inWorldPos);
    vec3 H = normalize(L + V);

    // Hemisphere ambient + Lambert diffuse + Blinn-Phong specular.
    float NdotL = max(dot(N, L), 0.0);
    vec3 ambient = mix(frame.groundColor.rgb, frame.skyColor.rgb, N.y * 0.5 + 0.5);
    vec3 diffuse = frame.sunColor.rgb * NdotL;
    float specular = pow(max(dot(N, H), 0.0), 64.0) * 0.25 * step(0.0, NdotL);

    vec3 color = albedo.rgb * (ambient + diffuse) + frame.sunColor.rgb * specular;

    // Exponential-squared distance fog.
    float dist = length(frame.cameraPosition.xyz - inWorldPos);
    float fogAmount = 1.0 - exp(-pow(frame.fog.a * dist, 2.0));
    color = mix(color, frame.fog.rgb, fogAmount);

    // Linear output; the sRGB swapchain encodes it.
    outColor = vec4(color, albedo.a);
}
