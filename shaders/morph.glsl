// Morph targets (Blender: shape keys), for every vertex shader (include after common.glsl).
//
// A morph target moves each vertex by its own offset (and turns its normal a little);
// the target's weight says how much of it to add: 0 = none, 1 = all of it. A "Belly" at
// 0.7 plus a "Shoulders" at 0.3 is base + 0.7 * belly + 0.3 * shoulders. It happens before
// skinning, in the mesh's own space, as glTF specifies.
//
// MeshRenderer passes three integers (bit for bit) in object.surface:
//   y = where this draw's weights start in the frame's weight buffer,
//   z = how many targets the mesh has (0 = not morphed: nothing to do),
//   w = where vertex 0's offsets would start in the model's offset buffer (set 1).
// Each vertex's offsets for all targets are together: vertex v, target t is entry
// w + v * z + t, with v = gl_VertexIndex (counted from the start of the model's
// vertices, which is why w is shifted back by the part's first vertex).

layout(set = 0, binding = 4, std430) readonly buffer MorphWeights {
    float morphWeights[];
};

// Six floats per entry: the position offset, then the normal offset (MorphDelta in Model.h).
layout(set = 1, binding = 3, std430) readonly buffer MorphDeltas {
    float morphDeltas[];
};

int morphTargetCount()
{
    return floatBitsToInt(object.surface.z);
}

// Adds this vertex's weighted offsets to its position and normal.
void applyMorphs(inout vec3 position, inout vec3 normal)
{
    int count = morphTargetCount();
    if (count <= 0)
        return;
    int firstWeight = floatBitsToInt(object.surface.y);
    int entry = floatBitsToInt(object.surface.w) + gl_VertexIndex * count;
    for (int t = 0; t < count; ++t) {
        float w = morphWeights[firstWeight + t];
        if (w != 0.0) { // the same for every vertex of the draw: unused targets cost little
            int i = (entry + t) * 6;
            position += w * vec3(morphDeltas[i], morphDeltas[i + 1], morphDeltas[i + 2]);
            normal += w * vec3(morphDeltas[i + 3], morphDeltas[i + 4], morphDeltas[i + 5]);
        }
    }
}

// The same for the shadow pass, which only needs the position.
void applyMorphs(inout vec3 position)
{
    int count = morphTargetCount();
    if (count <= 0)
        return;
    int firstWeight = floatBitsToInt(object.surface.y);
    int entry = floatBitsToInt(object.surface.w) + gl_VertexIndex * count;
    for (int t = 0; t < count; ++t) {
        float w = morphWeights[firstWeight + t];
        if (w != 0.0) {
            int i = (entry + t) * 6;
            position += w * vec3(morphDeltas[i], morphDeltas[i + 1], morphDeltas[i + 2]);
        }
    }
}
