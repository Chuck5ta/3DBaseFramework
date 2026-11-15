// Shared declarations for mesh.vert / mesh.frag and shadow.vert / shadow.frag. Included
// with #extension GL_GOOGLE_include_directive.

layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 sunDirection;  // towards the sun
    vec4 sunColor;      // linear * intensity
    vec4 skyColor;      // linear * ambient intensity
    vec4 groundColor;
    vec4 fog;           // rgb colour, a = density
    mat4 shadowMatrix;  // world -> the sun's clip space (the shadow map)
    vec4 shadow;        // x = strength (0 = off), y = normal offset (m), z = depth bias, w = texel (uv)
    vec4 shadowFade;    // x, y = fade out from/to this distance from the camera; z = softness (texels)
} frame;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;         // linear base colour and alpha
    vec4 emissive;      // rgb = linear glow added on top; a = alpha cutoff (0 = none)
    vec4 material;      // x = roughness (0 glossy .. 1 matte), y = metallic, z = 1 if unlit
} object;
