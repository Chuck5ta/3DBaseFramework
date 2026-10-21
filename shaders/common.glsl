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
    vec4 normalMatrix[3];
    vec4 color;         // linear
} object;
