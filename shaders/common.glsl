// Shared declarations for mesh.vert / mesh.frag. Included with
// #extension GL_GOOGLE_include_directive.

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
} frame;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 color;         // linear base colour and alpha
    vec4 emissive;      // rgb = linear glow added on top; a = alpha cutoff (0 = none)
    vec4 material;      // x = roughness (0 glossy .. 1 matte), y = metallic, z = 1 if unlit
} object;
