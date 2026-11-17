// Shared declarations for mesh.vert / mesh.frag and shadow.vert / shadow.frag (and their
// skinned versions). Included with #extension GL_GOOGLE_include_directive. The vertex
// shaders also include instance.glsl (copies of a mesh in one draw) and, when skinned,
// skin.glsl; mesh.vert and mesh_skinned.vert include tangents.glsl (for normal maps).

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
    mat4 model;         // the model matrix (one copy), or the part's matrix within the
                        // model (instanced: see instance.glsl)
    vec4 color;         // linear base colour and alpha
    vec4 emissive;      // rgb = linear glow added on top; a = alpha cutoff (0 = none)
    vec4 material;      // x = roughness (0 glossy .. 1 matte), y = metallic, z = 1 if unlit,
                        // w = first joint matrix (skinned meshes, see skin.glsl)
    vec4 surface;       // x = normal map strength (0 = none); yzw unused. 128 bytes in all:
                        // the most every GPU is sure to allow
} object;
