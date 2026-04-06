#version 450

// -------------------------------------------------------
// Inputs
// -------------------------------------------------------
layout(location = 0) in vec2 v_uv;

// -------------------------------------------------------
// HDR input - set 0 (tonemap pass only uses one set)
// -------------------------------------------------------
layout(set = 0, binding = 0) uniform sampler2D u_hdrBuffer;

// -------------------------------------------------------
// Output - LDR color for presentation
// -------------------------------------------------------
layout(location = 0) out vec4 out_color;

// -------------------------------------------------------
// Constants
// -------------------------------------------------------
const float GAMMA     = 2.2;
const float INV_GAMMA = 1.0 / GAMMA;
const float EXPOSURE  = 1.0; // TODO: wire to camera exposure UBO

// -------------------------------------------------------
// ACES filmic tonemapper
// Narkowicz 2015 approximation of the full ACES curve
// Slightly biased toward contrast and warm highlights
// -------------------------------------------------------
vec3 tonemapACES(vec3 x) {
    const float A = 2.51;
    const float B = 0.03;
    const float C = 2.43;
    const float D = 0.59;
    const float E = 0.14;
    return clamp((x * (A * x + B)) / (x * (C * x + D) + E), 0.0, 1.0);
}

// -------------------------------------------------------
// Reinhard - kept for debug comparison
// Uncomment and swap in main() if needed
// -------------------------------------------------------
// vec3 tonemapReinhard(vec3 x) {
//     return x / (x + vec3(1.0));
// }

// -------------------------------------------------------
// sRGB gamma correction
// Applied after tonemapping - converts linear to display space
// -------------------------------------------------------
vec3 linearToSRGB(vec3 color) {
    return pow(color, vec3(INV_GAMMA));
}

void main() {
    vec3 hdrColor = texture(u_hdrBuffer, v_uv).rgb;

    // Apply exposure before tonemapping
    hdrColor *= EXPOSURE;

    // Tonemap to LDR
    vec3 ldrColor = tonemapACES(hdrColor);

    // Gamma correct for display
    vec3 displayColor = linearToSRGB(ldrColor);

    out_color = vec4(displayColor, 1.0);
}