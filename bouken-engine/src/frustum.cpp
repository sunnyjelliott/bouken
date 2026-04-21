#include "frustum.h"

Frustum Frustum::fromViewProjection(const glm::mat4& vp) {
	Frustum f;

	f.planes[0] = glm::vec4(vp[0][3] + vp[0][0], vp[1][3] + vp[1][0],
	                        vp[2][3] + vp[2][0], vp[3][3] + vp[3][0]);
	f.planes[1] = glm::vec4(vp[0][3] - vp[0][0], vp[1][3] - vp[1][0],
	                        vp[2][3] - vp[2][0], vp[3][3] - vp[3][0]);
	f.planes[2] = glm::vec4(vp[0][3] + vp[0][1], vp[1][3] + vp[1][1],
	                        vp[2][3] + vp[2][1], vp[3][3] + vp[3][1]);
	f.planes[3] = glm::vec4(vp[0][3] - vp[0][1], vp[1][3] - vp[1][1],
	                        vp[2][3] - vp[2][1], vp[3][3] - vp[3][1]);
	f.planes[4] = glm::vec4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
	f.planes[5] = glm::vec4(vp[0][3] - vp[0][2], vp[1][3] - vp[1][2],
	                        vp[2][3] - vp[2][2], vp[3][3] - vp[3][2]);

	for (auto& plane : f.planes) {
		const float len = glm::length(glm::vec3(plane));
		if (len > 1e-6f) plane /= len;
	}

	return f;
}

bool Frustum::intersects(const AABB& aabb) const {
	for (const auto& plane : planes) {
		const glm::vec3 positive(plane.x >= 0.0f ? aabb.max.x : aabb.min.x,
		                         plane.y >= 0.0f ? aabb.max.y : aabb.min.y,
		                         plane.z >= 0.0f ? aabb.max.z : aabb.min.z);

		if (glm::dot(glm::vec3(plane), positive) + plane.w < 0.0f) return false;
	}
	return true;
}

bool Frustum::intersects(glm::vec3 center, float radius) const {
	for (const auto& plane : planes) {
		if (glm::dot(glm::vec3(plane), center) + plane.w < -radius)
			return false;
	}
	return true;
}