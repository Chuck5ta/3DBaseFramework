#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(set = 1, binding = 0) uniform sampler2D albedoTexture;
layout(set = 1, binding = 1) uniform sampler2D emissiveTexture; // white if the material has none

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 albedo = texture(albedoTexture, inUV) * object.color * inColor;
    if (albedo.a < object.emissive.a)
        discard; // alpha mask: cut-out leaves, fences, decals

    vec3 N = normalize(inNormal);
    if (!gl_FrontFacing)
        N = -N; // correct lighting for double-sided / wireframe draws
    vec3 L = normalize(frame.sunDirection.xyz);
    vec3 V = normalize(frame.cameraPosition.xyz - inWorldPos);
    vec3 H = normalize(L + V);

    vec3 color;
    if (object.material.z > 0.5) {
        color = albedo.rgb; // unlit: the colour as it is
    } else {
        // Hemisphere ambient + Lambert diffuse + Blinn-Phong specular. Roughness sets the
        // highlight's size and strength (0.5 = the classic shininess 64, strength 0.25);
        // metals reflect in their own colour and have less diffuse light. A cheap stand-in
        // for full PBR.
        float roughness = clamp(object.material.x, 0.0, 1.0);
        float metallic = clamp(object.material.y, 0.0, 1.0);
        float shininess = exp2(10.0 * (1.0 - roughness) + 1.0);
        float strength = 0.5 * (1.0 - roughness) * mix(1.0, 3.0, metallic);

        float NdotL = max(dot(N, L), 0.0);
        vec3 ambient = mix(frame.groundColor.rgb, frame.skyColor.rgb, N.y * 0.5 + 0.5);
        vec3 diffuse = frame.sunColor.rgb * NdotL;
        float specular = pow(max(dot(N, H), 0.0), shininess) * strength * step(0.0, NdotL);
        vec3 specularColor = mix(vec3(1.0), albedo.rgb, metallic);

        color = albedo.rgb * (1.0 - 0.7 * metallic) * (ambient + diffuse) +
                frame.sunColor.rgb * specularColor * specular;
    }
    color += object.emissive.rgb * texture(emissiveTexture, inUV).rgb; // glow: LEDs, screens, lava

    // Exponential-squared distance fog.
    float dist = length(frame.cameraPosition.xyz - inWorldPos);
    float fogAmount = 1.0 - exp(-pow(frame.fog.a * dist, 2.0));
    color = mix(color, frame.fog.rgb, fogAmount);

    // Linear output; the sRGB swapchain encodes it.
    outColor = vec4(color, albedo.a);
}
