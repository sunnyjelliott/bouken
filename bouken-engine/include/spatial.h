#pragma once
#include "pch.h"

// ---------------------------------------------------------------------------
// Spatial primitive types shared across rendering, culling, and future
// physics/raytracing systems.
// ---------------------------------------------------------------------------

struct AABB {
	glm::vec3 min{std::numeric_limits<float>::max()};
	glm::vec3 max{-std::numeric_limits<float>::max()};

	// Returns an AABB that contains both a and b.
	static AABB merge(const AABB& a, const AABB& b) {
		return {glm::min(a.min, b.min), glm::max(a.max, b.max)};
	}

	// Returns the center of the AABB.
	glm::vec3 center() const { return (min + max) * 0.5f; }

	// Returns the half-extents of the AABB.
	glm::vec3 extents() const { return (max - min) * 0.5f; }

	bool isValid() const { return min.x <= max.x; }

	AABB transformed(const glm::mat4& worldMat) const {
		const glm::vec3 corners[8] = {
		    {min.x, min.y, min.z}, {max.x, min.y, min.z}, {min.x, max.y, min.z},
		    {max.x, max.y, min.z}, {min.x, min.y, max.z}, {max.x, min.y, max.z},
		    {min.x, max.y, max.z}, {max.x, max.y, max.z},
		};

		AABB result;
		for (const auto& c : corners) {
			const glm::vec3 ws = glm::vec3(worldMat * glm::vec4(c, 1.0f));
			result.min = glm::min(result.min, ws);
			result.max = glm::max(result.max, ws);
		}
		return result;
	}
};

struct Sphere {
	glm::vec3 center{0.0f};
	float radius{0.0f};
};

struct Ray {
	glm::vec3 origin{0.0f};
	glm::vec3 direction{0.0f, 0.0f, -1.0f};  // unit length assumed
};