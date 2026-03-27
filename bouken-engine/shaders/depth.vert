#version 450

layout(location = 0) in vec3 a_position;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 projection;
} pc;

void main() {
    gl_Position = pc.projection * pc.view * pc.model * vec4(a_position, 1.0);
}