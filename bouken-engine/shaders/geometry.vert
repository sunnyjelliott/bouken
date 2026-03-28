#version 450

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_tangent;
layout(location = 3) in vec2 a_uv;
layout(location = 4) in vec4 a_color;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 projection;
} pc;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) out vec3 v_ws_normal;
layout(location = 3) out vec3 v_ws_tangent;
layout(location = 4) out vec3 v_ws_bitangent;

void main() {
    vec4 ws_position = pc.model * vec4(a_position, 1.0);

    mat3 normalMatrix = transpose(inverse(mat3(pc.model)));

    vec3 ws_normal  = normalize(normalMatrix * a_normal);
    vec3 ws_tangent = normalize(normalMatrix * a_tangent.xyz);

    // re-orthogonalize tangent to normal (corrects for floating point drift)
    ws_tangent = normalize(ws_tangent - dot(ws_tangent, ws_normal) * ws_normal);

    vec3 ws_bitangent = cross(ws_normal, ws_tangent) * a_tangent.w;

    //v_uv           = a_uv;
    v_uv = vec2(a_uv.x, 1.0 - a_uv.y);
    v_color        = a_color;
    v_ws_normal    = ws_normal;
    v_ws_tangent   = ws_tangent;
    v_ws_bitangent = ws_bitangent;

    gl_Position = pc.projection * pc.view * ws_position;
}