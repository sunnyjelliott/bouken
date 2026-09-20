#version 450

layout(location = 0) in vec3 a_position;

layout(push_constant) uniform PushConstants {
	mat4 model;
	mat4 view;
	float nearPlane;
	float farPlane;
	float hemisphereSign;  // +1.0 = front hemisphere, -1.0 = back hemisphere
} pc;

// gl_ClipDistance has to be sized before it can be written, and Vulkan GLSL
// sizes it by redeclaring the output block.
out gl_PerVertex {
	vec4 gl_Position;
	float gl_ClipDistance[1];
};

void main() {
	vec4 ws_position = pc.model * vec4(a_position, 1.0);
	vec4 vs_position4 = pc.view * ws_position;
	vec3 vs_position = vs_position4.xyz / vs_position4.w;

	// Back hemisphere: negate x and z (180-deg rotation about Y)
	vs_position.x *= pc.hemisphereSign;
	vs_position.z *= pc.hemisphereSign;

	float radialDist = length(vs_position);
	vec3 dir = vs_position / max(radialDist, 1e-5);

	// Paraboloid warp. The denominator vanishes at the *opposite* pole, so a
	// vertex the hemisphere test let through from the wrong side warps to
	// infinity and the clipper can no longer place the boundary vertex - the
	// triangle ends up smeared across the whole tile at a near-constant depth,
	// which then wins every LESS depth test. Floored so the position stays
	// finite and the clip below stays meaningful.
	vec2 warped = dir.xy / max(1.0 + dir.z, 1e-3);

	// The paraboloid is only defined over this hemisphere; the other half maps
	// outside the unit disk and is garbage. Cutting each triangle at the
	// boundary plane is what keeps a straddling caster - which ShadowSystem
	// now hands to *both* tiles - from leaking into the wrong one.
	gl_ClipDistance[0] = vs_position.z;

	gl_Position = vec4(warped, radialDist / pc.farPlane, 1.0);
}
