// Skinning, for mesh_skinned.vert and shadow_skinned.vert (include after common.glsl).
//
// Each vertex names up to four joints and how much each moves it (weights adding up to
// 1). The joints' matrices for this draw start at object.material.w in the frame's joint
// buffer, written by MeshRenderer for the pose being drawn. Blending them gives the one
// matrix that takes this vertex from the bind pose to where the pose puts it (in model
// space): near an elbow, part upper arm and part forearm, so the elbow bends smoothly.

layout(location = 4) in uvec4 inJoints;
layout(location = 5) in vec4 inWeights;

layout(set = 0, binding = 2, std430) readonly buffer JointMatrices {
    mat4 joints[];
};

mat4 skinMatrix()
{
    uint first = uint(object.material.w);
    return inWeights.x * joints[first + inJoints.x] +
           inWeights.y * joints[first + inJoints.y] +
           inWeights.z * joints[first + inJoints.z] +
           inWeights.w * joints[first + inJoints.w];
}
