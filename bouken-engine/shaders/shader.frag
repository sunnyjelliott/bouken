#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec3 fragWorldPos;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragColor;

layout(location = 0) out vec4 outColor;

// Texture samplers
layout(set = 0, binding = 0) uniform sampler2D albedoTexture;
layout(set = 0, binding = 1) uniform sampler2D normalTexture;

void main() {
    // Sample albedo texture
    vec3 albedo = texture(albedoTexture, fragUV).rgb;
    
    // Normalize the normal (interpolation can denormalize it)
    vec3 normal = normalize(fragNormal);
    
    // Simple directional light
    vec3 lightDir = normalize(vec3(0.5, -1.0, 0.3));
    float diff = max(dot(normal, -lightDir), 0.0);
    
    // Ambient + diffuse
    vec3 ambient = vec3(0.3);
    vec3 diffuse = vec3(0.7) * diff;

    vec3 finalColor = (ambient + diffuse) * albedo;
    
    outColor = vec4(finalColor, 1.0);
}