#pragma once
#include "pch.h"
#include "spatial.h"

struct Frustum {
	glm::vec4 planes[6];

	static Frustum fromViewProjection(const glm::mat4& vp);

	bool intersects(const AABB& aabb) const;

	bool intersects(glm::vec3 center, float radius) const;
};