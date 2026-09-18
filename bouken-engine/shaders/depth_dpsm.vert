#version 450

layout(location = 0) in vec3 a_position;

layout(push_constant) uniform PushConstants {
	mat4 model;
	mat4 view;
	float nearPlane;
	float farPlane;
	float hemisphereSign;  // +1.0 = front hemisphere, -1.0 = back hemisphere
} pc;

void main() {
	vec4 ws_position = pc.model * vec4(a_position, 1.0);
	vec4 vs_position4 = pc.view * ws_position;
	vec3 vs_position = vs_position4.xyz / vs_position4.w;

	// Back hemisphere: negate x and z (180-deg rotation about Y)
	vs_position.x *= pc.hemisphereSign;
	vs_position.z *= pc.hemisphereSign;

	float radialDist = length(vs_position);
	vec3 dir = vs_position / max(radialDist, 1e-5);

	// Paraboloid warp
	vec2 warped = dir.xy / (1.0 + dir.z);

	gl_Position = vec4(warped, radialDist / pc.farPlane, 1.0);
}