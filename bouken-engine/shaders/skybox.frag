#version 450

layout (location = 0) in vec3 v_direction;
layout (location = 0) out vec4 out_hdrColor;

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

layout(set = 1, binding = 4) uniform sampler2D u_depth;
layout(set = 1, binding = 9) uniform samplerCube u_envCubemap;

void main() {
    // The lighting pass has no depth attachment (sampling an image that is also
    // an attachment is a feedback loop), so occlusion is resolved here instead.
    // skybox.vert forces depth to the far plane, so any depth < 1.0 means real
    // geometry was drawn closer.
    vec2 uv = gl_FragCoord.xy / u_frame.screenExtent;
    if (texture(u_depth, uv).r < 1.0) discard;

    out_hdrColor = vec4(texture(u_envCubemap, normalize(v_direction)).rgb, 1.0);
}
