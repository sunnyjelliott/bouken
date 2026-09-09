#version 450

layout(location = 0) out vec4 o_moments;

// Optimized quantization basis (Peters & Klein, MSM supplementary, Listing 1).
// Each row below is one HLSL-listed row, fed as a GLSL constructor
// column-group - this reproduces mul(Moments, M) via GLSL's M * v.
const mat4 MOMENT_ENCODE = mat4(
	-2.07224649,    13.7948857237,  0.105877704,   9.7924062118,
	32.23703778,   -59.4683975703, -1.9077466311, -33.7652110555,
	-68.571074599,  82.0359750338,  9.3496555107,  47.9456096605,
	39.3703274134, -35.364903257,  -6.6543490743, -23.9728048165
);
const float MOMENT_ENCODE_BIAS = 0.035955884801;  // added to component 0 only

void main() {
	float z  = gl_FragCoord.z;
	float z2 = z * z;
	vec4 rawMoments = vec4(z, z2, z2 * z, z2 * z2);

	vec4 encoded = MOMENT_ENCODE * rawMoments;
	encoded.x += MOMENT_ENCODE_BIAS;

	o_moments = encoded;
}