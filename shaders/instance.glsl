// Instancing, for every vertex shader (include after common.glsl).
//
// One draw call can draw many copies (instances) of a mesh: a forest from one tree. Each
// copy's matrix is in the frame's instance buffer, written by MeshRenderer, and the GPU
// numbers the copies of a draw gl_InstanceIndex = first, first + 1, ...: the buffer index
// of that copy's matrix. A draw of a single copy uses entry 0, which always holds the
// identity matrix, so the same shaders draw both.

layout(set = 0, binding = 3, std430) readonly buffer InstanceMatrices {
    mat4 instances[];
};

// Places this copy: object.model puts the part within the model (or is the whole model
// matrix, for a single copy), then the instance's matrix puts the model in the world.
mat4 instanceMatrix()
{
    return instances[gl_InstanceIndex] * object.model;
}
