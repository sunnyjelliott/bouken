#version 450

layout(location = 0) in vec3 a_position;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 projection;
} pc;

void main() {
    // Must match geometry.vert's expression exactly, including the association
    // of the multiplies - the geometry pass depth-tests against what this pass
    // wrote, so any last-bit difference would drop fragments.
    vec4 ws_position = pc.model * vec4(a_position, 1.0);

    gl_Position = pc.projection * pc.view * ws_position;
}