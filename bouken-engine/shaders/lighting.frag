#version 450

// -------------------------------------------------------
// Inputs
// -------------------------------------------------------
layout (location = 0) in vec2 v_uv;

// -------------------------------------------------------
// set 1: G-buffer, Lighting
// -------------------------------------------------------
layout(set = 1, binding = 0) uniform sampler2D u_gbuffer0; // baseColor + metallic
layout(set = 1, binding = 1) uniform sampler2D u_gbuffer1; // oct-encoded normal
layout(set = 1, binding = 2) uniform sampler2D u_gbuffer2; // roughness + ao + specular + id
layout(set = 1, binding = 3) uniform sampler2D u_gbuffer3; // emissive + flags
layout(set = 1, binding = 4) uniform sampler2D u_depth;    // depth buffer

layout(set = 1, binding = 6) uniform sampler2D u_shadowMap;
layout(set = 1, binding = 7) uniform SHCoefficients { vec4 c[9]; } u_sh; // SH Coefficients
layout(set = 1, binding = 8) uniform samplerCube u_prefilteredEnv; // prefiltered environment map
layout(set = 1, binding = 9) uniform sampler2D   u_brdfLut; // BRDF LUT
layout(set = 1, binding = 10) uniform samplerCube u_envCubemap; // final cubemap

struct LightData {
    vec4     positionAndRadius;   // xyz = pos (point/spot) or dir (directional), w = radius
    vec4     colorAndIntensity;   // xyz = color, w = intensity
    vec4     directionAndCosOuter; // xyz = direction, w = cos(outerAngle)
    uint     type;                // 0=directional, 1=point, 2=spot
    float    cosInner;
    uint     castsShadow;
    float    _pad;
    mat4     lightSpaceMatrix;
    vec4     shadowAtlasRegion;
};

layout(set = 1, binding = 5, std430) readonly buffer LightBuffer {
    uint      u_lightCount;
    uint      _pad[3];
    LightData u_lights[];
};

// -------------------------------------------------------
// Frame data - set 0
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
// Output - HDR color
// -------------------------------------------------------
layout(location = 0) out vec4 o_hdrColor;

// -------------------------------------------------------
// Constants
// -------------------------------------------------------
const float PI          = 3.14159265359;
const float INV_PI      = 1.0 / PI;
const float EPSILON     = 0.0001;

// -------------------------------------------------------
// Octahedral decode - inverse of geometry pass encode
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
    float k  = (r * r) / 8.0; // Disney remapping - reduces hotspot at low roughness
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

    // F0 - base reflectance
    // Dielectrics use 0.04, metals use base color
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);

    // Specular terms
    float D   = D_GGX(NoH, roughness);
    vec3  F   = F_Schlick(VoH, F0);
    float G   = G_SmithGGX(NoV, NoL, roughness);

    vec3 specular = (D * F * G) / (4.0 * NoV * NoL);

    // Diffuse - energy conserving
    // Metals have no diffuse contribution
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * baseColor * INV_PI;

    return (diffuse + specular) * NoL;
}

// -------------------------------------------------------
// L2 SH irradiance approximation
//
// u_sh holds raw *radiance* coefficients L_lm from the SH projection pass.
// Turning those into Lambertian diffuse needs the cosine-lobe convolution
// (Ramamoorthi & Hanrahan 2001):
//     E(N) = sum_l A_l * sum_m L_lm * Y_lm(N),   A = { PI, 2PI/3, PI/4 }
// followed by Lambert's 1/PI, which cancels the PI out of every A_l and
// leaves the per-band weights below. Without them L1 comes out 1.5x and L2
// 4x too strong, which reads as over-contrasty ambient that clamps to black
// on the shadowed side.
//
// The returned value is already divided by PI, so callers multiply by albedo
// alone - no further 1/PI.
// -------------------------------------------------------
const float SH_BAND0 = 1.0f;         // PI      / PI
const float SH_BAND1 = 2.0f / 3.0f;  // (2PI/3) / PI
const float SH_BAND2 = 0.25f;        // (PI/4)  / PI

vec3 evaluateSHIrradiance(vec3 N) {
    float x = N.x, y = N.y, z = N.z;

    vec3 irradiance =
        SH_BAND0 * (u_sh.c[0].rgb * 0.282095f)

      + SH_BAND1 * (u_sh.c[1].rgb * 0.488603f * y
                  + u_sh.c[2].rgb * 0.488603f * z
                  + u_sh.c[3].rgb * 0.488603f * x)

      + SH_BAND2 * (u_sh.c[4].rgb * 1.092548f * x * y
                  + u_sh.c[5].rgb * 1.092548f * y * z
                  + u_sh.c[6].rgb * 0.315392f * (3.0f * z * z - 1.0f)
                  + u_sh.c[7].rgb * 1.092548f * x * z
                  + u_sh.c[8].rgb * 0.546274f * (x * x - y * y));

    return max(irradiance, vec3(0.0f));
}

// -------------------------------------------------------
// IBL Specular Evaluation
// -------------------------------------------------------
layout(constant_id = 0) const uint IBL_PREFILTERED_MIP_LEVELS = 5;

vec3 evaluateIBLSpecular(vec3 N, vec3 V, float roughness, vec3 F0) {
    vec3  R      = reflect(-V, N);
    float NdotV  = max(dot(N, V), 0.0f);

    // Sample prefiltered env at the mip level corresponding to roughness
    float mipLevel       = roughness * float(IBL_PREFILTERED_MIP_LEVELS - 1);
    vec3  prefilteredColor = textureLod(u_prefilteredEnv, R, mipLevel).rgb;

    // Split-sum second term
    vec2 brdf = texture(u_brdfLut, vec2(NdotV, roughness)).rg;

    return prefilteredColor * (F0 * brdf.r + brdf.g);
}

// -------------------------------------------------------
// Shadow Map Sampling
// -------------------------------------------------------

const mat4 MOMENT_DECODE = mat4(
	0.2227744146,  0.1549679261,  0.1451988946,  0.163127443,
	0.0771972861,  0.1394629426,  0.2120202157,  0.2591432266,
	0.7926986636,  0.7963415838,  0.7258694464,  0.6539092497,
	0.0319417555, -0.1722823173, -0.2758014811, -0.3376131734
);
const float MOMENT_DECODE_BIAS = 0.035955884801;

// 4-moment shadow reconstruction (Peters & Klein, "Moment Shadow Mapping").
// moments = (E[z], E[z^2], E[z^3], E[z^4]) sampled from the resolved (blurred)
// moments texture. fragmentDepth is the receiving fragment's depth in the
// light's NDC space. Returns visibility: 1.0 = fully lit, 0.0 = fully shadowed.
float momentShadow4MSM(vec4 encodedMoments, float fragmentDepth) {

    vec4 quantized = encodedMoments;
	quantized.x -= MOMENT_DECODE_BIAS;
	vec4 moments = MOMENT_DECODE * quantized;

	const float depthBias = 0.0005;   // same role as the old hard-compare bias
	const float momentBias = 3e-5;    // blends toward (0.5,0.5,0.5,0.5) - light-bleed control

	vec4 b = mix(moments, vec4(0.5), momentBias);

	// Solve for the two-node quadrature via a Cholesky-style factorization
	// of the Hankel matrix built from the moments.
	float L32D22 = fma(-b.x, b.y, b.z);
	float D22 = fma(-b.x, b.x, b.y);
	float squaredDepthVariance = fma(-b.y, b.y, b.w);
	float D33D22 = dot(vec2(squaredDepthVariance, -L32D22), vec2(D22, L32D22));
	float InvD22 = 1.0 / D22;
	float L32 = L32D22 * InvD22;

	vec3 z;
	z[0] = fragmentDepth - depthBias;

	vec3 c = vec3(1.0, z[0], z[0] * z[0]);
	c[1] -= b.x;
	c[2] -= b.y + L32 * c[1];
	c[1] /= D22;
	c[2] *= D22 / D33D22;
	c[1] -= L32 * c[2];
	c[0] -= dot(c.yz, b.xy);

	float InvC2 = 1.0 / c[2];
	float p = c[1] * InvC2;
	float q = c[0] * InvC2;
	float r = sqrt(max(p * p * 0.25 - q, 0.0));

	z[1] = -p * 0.5 - r;
	z[2] = -p * 0.5 + r;

	vec4 switchVal =
	    (z[2] < z[0])
	        ? vec4(z[1], z[0], 1.0, 1.0)
	        : ((z[1] < z[0]) ? vec4(z[0], z[1], 0.0, 1.0) : vec4(0.0));

	float quotient = (switchVal[0] * z[2] - b[0] * (switchVal[0] + z[2]) + b[1]) /
	                 ((z[2] - switchVal[1]) * (z[0] - z[1]));

	float shadowIntensity = switchVal[2] + switchVal[3] * quotient;
	return clamp(1.0 - shadowIntensity, 0.0, 1.0);
}

// atlasRegion: xy = the caster's tile offset in atlas UV, zw = its tile scale.
// The bounds check below must run in tile-local [0,1] space, before the remap.
float sampleShadow(vec3 worldPos, mat4 lightSpaceMatrix, vec4 atlasRegion) {
    vec4 shadowClip = lightSpaceMatrix * vec4(worldPos, 1.0);
    vec3 shadowNDC  = shadowClip.xyz / shadowClip.w;
    vec2 shadowUV   = shadowNDC.xy * 0.5 + 0.5;

    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
        shadowUV.y < 0.0 || shadowUV.y > 1.0 || shadowNDC.z > 1.0) {
        return 1.0; // outside the caster's frustum - unshadowed
    }

	vec2 atlasUV = atlasRegion.xy + shadowUV * atlasRegion.zw;

	vec4 moments = texture(u_shadowMap, atlasUV);
	return momentShadow4MSM(moments, shadowNDC.z);
}

// -------------------------------------------------------
// Cascade Shadow Mapping
// -------------------------------------------------------
layout(set = 1, binding = 11) uniform CascadeData {
    mat4 cascadeMatrices[3];
    vec4 splitDepths;          // xyz = view-space split-far distances, w unused
    uint hasDirectionalShadow;
} u_cascades;

layout(set = 1, binding = 12) uniform sampler2D u_cascadeShadowMap0;
layout(set = 1, binding = 13) uniform sampler2D u_cascadeShadowMap1;
layout(set = 1, binding = 14) uniform sampler2D u_cascadeShadowMap2;

float sampleCascade(int index, vec3 worldPos) {
    mat4 vp = u_cascades.cascadeMatrices[index];
    vec4 shadowClip = vp * vec4(worldPos, 1.0);
    vec3 shadowNDC  = shadowClip.xyz / shadowClip.w;
    vec2 shadowUV   = shadowNDC.xy * 0.5 + 0.5;

    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
        shadowUV.y < 0.0 || shadowUV.y > 1.0 || shadowNDC.z > 1.0) {
        return 1.0;
    }

    vec4 moments;
    if (index == 0)      moments = texture(u_cascadeShadowMap0, shadowUV);
    else if (index == 1) moments = texture(u_cascadeShadowMap1, shadowUV);
    else                 moments = texture(u_cascadeShadowMap2, shadowUV);

    return momentShadow4MSM(moments, shadowNDC.z);
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

    vec3 directLight = vec3(0.0);

    for (uint i = 0; i < u_lightCount; i++) {
        LightData light = u_lights[i];
        vec3 lightColor = light.colorAndIntensity.rgb
                        * light.colorAndIntensity.w;

        if (light.type == 0u) {
            vec3 L = normalize(-light.positionAndRadius.xyz);

            float shadow = 1.0;
            if (u_cascades.hasDirectionalShadow == 1u) {
                float viewDepth = (u_frame.view * vec4(ws_position, 1.0)).z;
                float cascadeDistance = -viewDepth;  // camera looks down -Z in view space

                // Beyond the shadow distance entirely - no cascade covers it, stays
                // unshadowed rather than clamping into cascade 2's stretched-thin
                // far edge.
                if (cascadeDistance <= u_cascades.splitDepths.z) {
                    int cascadeIndex = 2;
                    if (cascadeDistance < u_cascades.splitDepths.x) cascadeIndex = 0;
                    else if (cascadeDistance < u_cascades.splitDepths.y) cascadeIndex = 1;

                    shadow = sampleCascade(cascadeIndex, ws_position);
                }
            }

            directLight += evaluateBRDF(ws_normal, V, L, baseColor, metallic, roughness)
                        * lightColor * shadow;
        } else if (light.type == 1u) {
            // Point - inverse square attenuation
            vec3  toLight = light.positionAndRadius.xyz - ws_position;
            float dist    = length(toLight);
            float radius  = light.positionAndRadius.w;

            // Windowed inverse square - clean falloff at radius boundary
            float attenuation = pow(max(1.0 - pow(dist / radius, 4.0), 0.0), 2.0)
                            / (dist * dist + 1.0);

            if (attenuation > 0.0001) {
                vec3 L = normalize(toLight);
                directLight += evaluateBRDF(ws_normal, V, L,
                                            baseColor, metallic, roughness)
                            * lightColor * attenuation;
            }

        } else if (light.type == 2u) {
            // Spot - point light with angular falloff
            vec3  toLight = light.positionAndRadius.xyz - ws_position;
            float dist    = length(toLight);
            float radius  = light.positionAndRadius.w;

            float attenuation = pow(max(1.0 - pow(dist / radius, 4.0), 0.0), 2.0)
                            / (dist * dist + 1.0);

            if (attenuation > 0.0001) {
                vec3  L        = normalize(toLight);
                vec3  spotDir  = normalize(-light.directionAndCosOuter.xyz);
                float cosAngle = dot(L, spotDir);
                float cosOuter = light.directionAndCosOuter.w;
                float cosInner = light.cosInner;

                // Smooth angular falloff between inner and outer cone
                float spotFalloff = smoothstep(cosOuter, cosInner, cosAngle);

                float shadow = (light.castsShadow == 1u)
                    ? sampleShadow(ws_position, light.lightSpaceMatrix,
                                   light.shadowAtlasRegion)
                    : 1.0;

                directLight += evaluateBRDF(ws_normal, V, L,
                                            baseColor, metallic, roughness)
                            * lightColor * attenuation * spotFalloff * shadow;
            }
        }
    }

    // -------------------------------------------------------
    // IBL
    // -------------------------------------------------------
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);

    float NoV_ibl = max(dot(ws_normal, V), 0.0);
    vec3 F_approx = F0 + (1.0 - F0) * pow(1.0 - NoV_ibl, 5.0);

    vec3 kD_ibl = (1.0 - F_approx) * (1.0 - metallic);
    vec3 ibl_diffuse = kD_ibl * evaluateSHIrradiance(ws_normal) * baseColor * ao;

    vec3 ibl_specular = evaluateIBLSpecular(ws_normal, V, roughness, F0) * ao;

    vec3 ambient = ibl_diffuse + ibl_specular;

    // -------------------------------------------------------
    // Emissive contribution
    // Early out on flag avoids redundant add for most surfaces
    // -------------------------------------------------------
    vec3 emissiveContrib = (emissiveFlag > 0.5) ? emissive : vec3(0.0);

    // -------------------------------------------------------
    // Final HDR output
    // -------------------------------------------------------
    vec3 hdrColor = directLight + ambient + emissiveContrib;
    o_hdrColor  = vec4(hdrColor, 1.0);
}