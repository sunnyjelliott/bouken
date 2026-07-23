#version 450

layout (location = 0) in vec3 v_direction;
layout (location = 0) out vec4 out_hdrColor;

layout(set = 1, binding = 9) uniform samplerCube u_envCubemap;

void main() {
    out_hdrColor = vec4(texture(u_envCubemap, normalize(v_direction)).rgb, 1.0);
}