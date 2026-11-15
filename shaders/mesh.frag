#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap; // compares: 1 = lit, 0 = shadowed

layout(set = 1, binding = 0) uniform sampler2D albedoTexture;
layout(set = 1, binding = 1) uniform sampler2D emissiveTexture; // white if the material has none

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec4 outColor;

// How much sunlight reaches this point: 1 = all, 0 = none (in shadow).
float sunlight(vec3 worldPos, vec3 N)
{
    float strength = frame.shadow.x;
    if (strength <= 0.0)
        return 1.0; // no shadow pass this frame
    float dist = length(frame.cameraPosition.xyz - worldPos);
    float fade = smoothstep(frame.shadowFade.x, frame.shadowFade.y, dist);
    if (fade >= 1.0)
        return 1.0; // beyond the shadow distance

    // Where this point is in the shadow map, looked up a little off the surface (normal
    // offset) and a little nearer the sun (depth bias), so that a surface doesn't shadow
    // itself in stripes where its own depth and the map's disagree by a rounding error.
    vec4 s = frame.shadowMatrix * vec4(worldPos + N * frame.shadow.y, 1.0);
    vec2 uv = s.xy * 0.5 + 0.5;
    float depth = s.z - frame.shadow.z;
    if (depth >= 1.0)
        return 1.0; // further from the sun than the map reaches

    // Average a 3x3 grid of lookups (each one already blends four texels) for soft edges.
    float spread = frame.shadowFade.z * frame.shadow.w;
    float lit = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            lit += texture(shadowMap, vec3(uv + vec2(x, y) * spread, depth));
    lit /= 9.0;

    return mix(1.0, lit, strength * (1.0 - fade));
}

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
        // Shadows block the sun only; the sky's ambient light still gets in.
        float sun = NdotL > 0.0 ? sunlight(inWorldPos, N) : 0.0;
        vec3 ambient = mix(frame.groundColor.rgb, frame.skyColor.rgb, N.y * 0.5 + 0.5);
        vec3 diffuse = frame.sunColor.rgb * NdotL * sun;
        float specular = pow(max(dot(N, H), 0.0), shininess) * strength * sun;
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
