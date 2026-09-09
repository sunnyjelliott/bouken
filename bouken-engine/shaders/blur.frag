#version 450

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;

layout(set = 0, binding = 0) uniform sampler2D u_source;

layout(push_constant) uniform PushConstants {
	vec2 texelDirection;
	vec2 tileUVMin;
	vec2 tileUVMax;
	float atlasSizeInv;
} push;

const float WEIGHTS[5] = float[](0.06136, 0.24477, 0.38774, 0.24477, 0.06136);
//const float WEIGHTS[3] = float[](0.27901, 0.44198, 0.27901);

void main() {
	vec2 atlasUV = gl_FragCoord.xy * push.atlasSizeInv;

	vec4 sum = vec4(0.0);
	for (int i = -2; i <= 2; i++) {
	//for (int i = -1; i <= 1; i++) {
		vec2 offset = push.texelDirection * float(i);
		vec2 sampleUV = clamp(atlasUV + offset, push.tileUVMin, push.tileUVMax);
		sum += texture(u_source, sampleUV) * WEIGHTS[i + 2];
		//sum += texture(u_source, sampleUV) * WEIGHTS[i + 1];
	}
	o_color = sum;
}