#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

void main() {
    // Simple directional light
    vec3 lightDir = normalize(vec3(0.5, -1.0, 0.3));
    vec3 normal = normalize(fragNormal);
    
    // Lambertian diffuse
    float diffuse = max(dot(normal, -lightDir), 0.0);
    
    // Ambient
    vec3 ambient = vec3(0.3);
    
    // Combine
    vec3 lighting = ambient + diffuse * vec3(0.7);
    outColor = vec4(fragColor * lighting, 1.0);
}