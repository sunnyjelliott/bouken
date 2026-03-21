#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inColor;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec3 fragWorldPos;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec3 fragColor;

layout(push_constant) uniform PushConstants {
    mat4 model;
    mat4 view;
    mat4 projection;
} push;

void main() {
    vec4 worldPos = push.model * vec4(inPosition, 1.0);
    gl_Position = push.projection * push.view * worldPos;
    
    fragWorldPos = worldPos.xyz;
    
    // Transform normal to world space (assuming uniform scale for now)
    fragNormal = mat3(push.model) * inNormal;
    
    fragUV = inUV;
    fragColor = inColor;
}