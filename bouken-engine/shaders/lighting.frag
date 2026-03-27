#version 450

// -------------------------------------------------------
// Inputs
// -------------------------------------------------------
layout (location = 0) in vec2 v_uv;

// -------------------------------------------------------
// G-buffer samplers — set 1
// -------------------------------------------------------
layout(set = 1, binding = 0) uniform sampler2D u_gbuffer0; // baseColor + metallic
layout(set = 1, binding = 1) uniform sampler2D u_gbuffer1; // oct-encoded normal
layout(set = 1, binding = 2) uniform sampler2D u_gbuffer2; // roughness + ao + specular + id
layout(set = 1, binding = 3) uniform sampler2D u_gbuffer3; // emissive + flags
layout(set = 1, binding = 4) uniform sampler2D u_depth;    // depth buffer

// -------------------------------------------------------
// Frame data — set 0
// Must match FrameUBO in framedata.h
// -------------------------------------------------------
layout(set = 0, binding = 0) uniform FrameData {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 invProjection;
    mat4 invView;
    vec4 cameraPosition;  // w unused
    vec2 screenExtent;
    float time;
    float _pad;
} u_frame;

// -------------------------------------------------------
// Output — HDR color
// -------------------------------------------------------
layout(location = 0) out vec4 out_hdrColor;

// -------------------------------------------------------
// Constants
// -------------------------------------------------------
const float PI          = 3.14159265359;
const float INV_PI      = 1.0 / PI;
const float EPSILON     = 0.0001;

// -------------------------------------------------------
// Hardcoded directional light
// TODO: replace with light buffer when LightSystem exists
// -------------------------------------------------------
const vec3  LIGHT_DIRECTION = normalize(vec3(-0.0, -1.0, -0.0));
const vec3  LIGHT_COLOR     = vec3(1.0, 0.98, 0.95); // slightly warm white
const float LIGHT_INTENSITY = 3.0;
const vec3  AMBIENT_COLOR   = vec3(0.03, 0.03, 0.04); // dim cool ambient

// -------------------------------------------------------
// Octahedral decode — inverse of geometry pass encode
// -------------------------------------------------------
vec3 octDecode(vec2 encoded) {
    vec3 n    = vec3(encoded.x, encoded.y,
                     1.0 - abs(encoded.x) - abs(encoded.y));
    float t   = max(-n.z, 0.0);
    n.x      += (n.x >= 0.0) ? -t : t;
    n.y      += (n.y >= 0.0) ? -t : t;
    return normalize(n);
}

// -------------------------------------------------------
// World position reconstruction from depth
// -------------------------------------------------------
vec3 reconstructWorldPos(vec2 uv, float depth) {
    // Remap uv and depth to NDC
    vec4 ndcPos = vec4(uv * 2.0 - 1.0, depth, 1.0);

    // Unproject to view space
    vec4 viewPos       = u_frame.invProjection * ndcPos;
    viewPos           /= viewPos.w;

    // Transform to world space
    vec4 worldPos = u_frame.invView * viewPos;

    return worldPos.xyz;
}

// -------------------------------------------------------
// GGX Normal Distribution Function
// -------------------------------------------------------
float D_GGX(float NoH, float roughness) {
    float a     = roughness * roughness;
    float a2    = a * a;
    float denom = (NoH * NoH) * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// -------------------------------------------------------
// Schlick Fresnel approximation
// -------------------------------------------------------
vec3 F_Schlick(float cosTheta, vec3 F0) {
    float x = max(1.0 - cosTheta, 0.0);
    float x2 = x * x;

    return F0 + (1.0 - F0) * (x2 * x2 * x);
}

// -------------------------------------------------------
// Smith GGX Geometry function
// -------------------------------------------------------
float G_SmithGGX(float NoV, float NoL, float roughness) {
    float r  = roughness + 1.0;
    float k  = (r * r) / 8.0; // Disney remapping — reduces hotspot at low roughness
    float gV = NoV / (NoV * (1.0 - k) + k);
    float gL = NoL / (NoL * (1.0 - k) + k);
    return gV * gL;
}

// -------------------------------------------------------
// Cook-Torrance BRDF evaluation for one directional light
// -------------------------------------------------------
vec3 evaluateBRDF(vec3 N, vec3 V, vec3 L,
                  vec3 baseColor, float metallic, float roughness) {
    vec3 H = normalize(V + L);

    float NoV = max(dot(N, V), EPSILON);
    float NoL = max(dot(N, L), EPSILON);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    // F0 — base reflectance
    // Dielectrics use 0.04, metals use base color
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);

    // Specular terms
    float D   = D_GGX(NoH, roughness);
    vec3  F   = F_Schlick(VoH, F0);
    float G   = G_SmithGGX(NoV, NoL, roughness);

    vec3 specular = (D * F * G) / (4.0 * NoV * NoL);

    // Diffuse — energy conserving
    // Metals have no diffuse contribution
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * baseColor * INV_PI;

    return (diffuse + specular) * NoL;
}

void main() {
    // -------------------------------------------------------
    // Sample G-buffer
    // -------------------------------------------------------
    vec4 gbuffer0 = texture(u_gbuffer0, v_uv);
    vec2 gbuffer1 = texture(u_gbuffer1, v_uv).rg;
    vec4 gbuffer2 = texture(u_gbuffer2, v_uv);
    vec4 gbuffer3 = texture(u_gbuffer3, v_uv);
    float depth   = texture(u_depth, v_uv).r;

    // -------------------------------------------------------
    // Unpack G-buffer
    // -------------------------------------------------------
    vec3  baseColor = gbuffer0.rgb;
    float metallic  = gbuffer0.a;

    vec3  ws_normal  = octDecode(gbuffer1);

    float roughness  = gbuffer2.r;
    float ao         = gbuffer2.g;
    // gbuffer2.b = specular (unused in direct lighting for now)
    // gbuffer2.a = materialID (unused until material buffer lands)

    vec3  emissive   = gbuffer3.rgb;
    float emissiveFlag = gbuffer3.a;

    // -------------------------------------------------------
    // Reconstruct world position
    // -------------------------------------------------------
    vec3 ws_position = reconstructWorldPos(v_uv, depth);

    // -------------------------------------------------------
    // View vector
    // -------------------------------------------------------
    vec3 V = normalize(u_frame.cameraPosition.xyz - ws_position);

    // -------------------------------------------------------
    // Direct lighting — single directional light
    // -------------------------------------------------------
    vec3 L          = -LIGHT_DIRECTION; // light direction points toward surface
    vec3 lightRadiance = LIGHT_COLOR * LIGHT_INTENSITY;

    vec3 directLight = evaluateBRDF(ws_normal, V, L,
                                    baseColor, metallic, roughness)
                     * lightRadiance;

    // -------------------------------------------------------
    // Ambient — very simple, modulated by AO
    // Will be replaced by IBL when cubemap arrives
    // -------------------------------------------------------
    vec3 ambient = AMBIENT_COLOR * baseColor * ao;

    // -------------------------------------------------------
    // Emissive contribution
    // Early out on flag avoids redundant add for most surfaces
    // -------------------------------------------------------
    vec3 emissiveContrib = (emissiveFlag > 0.5) ? emissive : vec3(0.0);

    // -------------------------------------------------------
    // Final HDR output
    // -------------------------------------------------------
    vec3 hdrColor = directLight + ambient + emissiveContrib;
    out_hdrColor  = vec4(hdrColor, 1.0);
}