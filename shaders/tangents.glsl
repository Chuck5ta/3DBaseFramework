// Normal maps, for mesh.vert and mesh_skinned.vert (include after common.glsl).
//
// A normal map's texels are directions relative to the surface: x along the tangent (the
// way the texture's u runs), y along the bitangent (the way v runs, up the image), z along
// the normal. The vertex shader hands the fragment shader those three directions, in the
// world; mesh.frag combines them with the map.
//
// This is the MikkTSpace way (see render3d/Tangents.h), the one Blender's and other bakers
// assume: each vertex's normal and tangent made unit length here, the bitangent rebuilt as
// sign * cross(normal, tangent), and the three used as interpolated, without normalising
// them again per pixel. Done otherwise, a baked map shows seams and wrong shading.

layout(location = 6) in vec4 inTangent; // xyz = tangent, w = +1/-1 (0 = no tangents)

layout(location = 4) out vec3 outTangent;
layout(location = 5) out vec3 outBitangent;

// `model` places the vertex; `worldNormal` is its normal in the world, unit length.
void writeTangentFrame(mat4 model, vec3 worldNormal)
{
    if (inTangent.w == 0.0) {
        outTangent = vec3(0.0); // none: mesh.frag ignores the normal map
        outBitangent = vec3(0.0);
        return;
    }
    vec3 T = normalize(mat3(model) * inTangent.xyz);
    // A mirrored copy (negative scale) turns the frame inside out: flip the bitangent back.
    float handedness = inTangent.w * (determinant(mat3(model)) < 0.0 ? -1.0 : 1.0);
    outTangent = T;
    outBitangent = handedness * cross(worldNormal, T);
}
