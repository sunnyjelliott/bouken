#version 450

// -------------------------------------------------------
// Inputs from vertex shader
// -------------------------------------------------------
layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec3 v_ws_normal;
layout(location = 3) in vec3 v_ws_tangent;
layout(location = 4) in vec3 v_ws_bitangent;

// -------------------------------------------------------
// Material textures - set 2
// -------------------------------------------------------
layout(set = 2, binding = 0) uniform sampler2D u_albedoMap;
layout(set = 2, binding = 1) uniform sampler2D u_normalMap;
layout(set = 2, binding = 2) uniform sampler2D u_metallicMap;
layout(set = 2, binding = 3) uniform sampler2D u_roughnessMap;
layout(set = 2, binding = 4) uniform sampler2D u_aoMap;
layout(set = 2, binding = 5) uniform sampler2D u_emissiveMap;
layout(set = 2, binding = 6) uniform MaterialConstants {
    vec4     u_baseColor;
    vec4     u_emissiveColor;
    float    u_metallic;
    float    u_roughness;
    float    u_occlusion;
    float    u_opacity;
    uint     u_textureFlags;
    uint     _pad[3];
} u_material;

bool hasTexture(uint flag) {
    return (u_material.u_textureFlags & flag) != 0;
}

const uint FLAG_HAS_ALBEDO    = 1u << 0;
const uint FLAG_HAS_NORMAL    = 1u << 1;
const uint FLAG_HAS_METALLIC  = 1u << 2;
const uint FLAG_HAS_ROUGHNESS = 1u << 3;
const uint FLAG_HAS_AO        = 1u << 4;
const uint FLAG_HAS_EMISSIVE  = 1u << 5;

// -------------------------------------------------------
// G-buffer outputs
// GBuffer0 RGBA8_UNORM  : baseColor.rgb, metallic
// GBuffer1 RG16_SNORM   : oct-encoded normal xy
// GBuffer2 RGBA8_UNORM  : roughness, ao, specular, materialID
// GBuffer3 RGBA16F      : emissive.rgb, flags
// -------------------------------------------------------
layout(location = 0) out vec4 out_baseColorMetallic;
layout(location = 1) out vec2 out_normals;
layout(location = 2) out vec4 out_roughnessAOSpecID;
layout(location = 3) out vec4 out_emissiveFlags;

// -------------------------------------------------------
// Constants
// -------------------------------------------------------
const float DEFAULT_ROUGHNESS = 0.5;
const float DEFAULT_METALLIC  = 0.0;
const float DEFAULT_SPECULAR  = 0.5;
const float DEFAULT_AO        = 1.0;

// -------------------------------------------------------
// Octahedral encoding
// Maps a unit normal to a [-1, 1] x [-1, 1] square
// -------------------------------------------------------
vec2 octEncode(vec3 n) {
    // Project onto octahedron
    float l1norm = abs(n.x) + abs(n.y) + abs(n.z);
    vec2 result  = n.xy * (1.0 / l1norm);

    // Fold negative hemisphere
    if (n.z < 0.0) {
        result = (1.0 - abs(result.yx)) * sign(result.xy);
    }
    return result;
}

void main() {
    // Base color
    vec4 albedoSample = hasTexture(FLAG_HAS_ALBEDO)
        ? texture(u_albedoMap, v_uv)
        : vec4(u_material.u_baseColor.rgb, u_material.u_baseColor.a);

    vec3 baseColor = albedoSample.rgb;

    // Blend with vertex color when no albedo texture
    if (!hasTexture(FLAG_HAS_ALBEDO)) {
        baseColor *= v_color.rgb;
    }

    // TODO: support translucency
    if (u_material.u_opacity < 1.0) {
        discard;
    }

    // Metallic
    float metallic = hasTexture(FLAG_HAS_METALLIC)
        ? texture(u_metallicMap, v_uv).r
        : u_material.u_metallic;
    
    // Roughness
    float roughness = hasTexture(FLAG_HAS_ROUGHNESS)
        ? texture(u_roughnessMap, v_uv).r
        : u_material.u_roughness;

    // AO
    float ao = hasTexture(FLAG_HAS_AO)
        ? texture(u_aoMap, v_uv).r
        : u_material.u_occlusion;

    // Normal
    vec3 ws_normal;
    if (hasTexture(FLAG_HAS_NORMAL)) {
        vec3 ts_normal = texture(u_normalMap, v_uv).rgb * 2.0 - 1.0;
        mat3 TBN       = mat3(v_ws_tangent, v_ws_bitangent, v_ws_normal);
        ws_normal      = normalize(TBN * ts_normal);
    } else {
        ws_normal = normalize(v_ws_normal);
    }

    // Emissive
    vec3 emissive = hasTexture(FLAG_HAS_EMISSIVE)
        ? texture(u_emissiveMap, v_uv).rgb
        : u_material.u_emissiveColor.rgb;

    // -------------------------------------------------------
    // Pack G-buffer
    // -------------------------------------------------------

    // GBuffer0: baseColor.rgb + metallic in alpha
    out_baseColorMetallic = vec4(baseColor, metallic);

    // GBuffer1: octahedral-encoded normal
    out_normals = octEncode(ws_normal);

    // GBuffer2: roughness, ao, specular (constant for now), materialID
    // materialID packed as normalised byte - 0 for now, lighting pass reads it
    out_roughnessAOSpecID = vec4(roughness, ao, DEFAULT_SPECULAR, 0.0);

    // GBuffer3: emissive + flags
    // flags byte: bit 0 = has emissive, others reserved
    float flags = (dot(emissive, emissive) > 0.0) ? 1.0 : 0.0;
    out_emissiveFlags = vec4(emissive, flags);

    // DEBUG: visualize world-space normal
    // Replace the G-buffer pack block temporarily
    // out_baseColorMetallic = vec4(ws_normal * 0.5 + 0.5, 1.0);
    // out_normals           = octEncode(ws_normal);
    // out_roughnessAOSpecID = vec4(0.5, 1.0, 0.5, 0.0);
    // out_emissiveFlags     = vec4(0.0);
}