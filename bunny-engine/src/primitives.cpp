#include "primitives.h"

PrimitiveGeometry Primitives::createCube(float size) {
	PrimitiveGeometry geom;
	float half = size * 0.5f;

	geom.vertices = {
	    // Front face (normal: +Z)
	    {{-half, -half, half}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
	    {{half, -half, half}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
	    {{half, half, half}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},
	    {{-half, half, half}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}},

	    // Back face (normal: -Z)
	    {{half, -half, -half}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
	    {{-half, -half, -half}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
	    {{-half, half, -half}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
	    {{half, half, -half}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},

	    // Top face (normal: +Y)
	    {{-half, half, half}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
	    {{half, half, half}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
	    {{half, half, -half}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
	    {{-half, half, -half}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},

	    // Bottom face (normal: -Y)
	    {{-half, -half, -half}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
	    {{half, -half, -half}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
	    {{half, -half, half}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
	    {{-half, -half, half}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},

	    // Right face (normal: +X)
	    {{half, -half, half}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}},
	    {{half, -half, -half}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}},
	    {{half, half, -half}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}},
	    {{half, half, half}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}},

	    // Left face (normal: -X)
	    {{-half, -half, -half}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
	    {{-half, -half, half}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
	    {{-half, half, half}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
	    {{-half, half, -half}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
	};

	geom.indices = {
	    // Front
	    0,
	    1,
	    2,
	    0,
	    2,
	    3,
	    // Back
	    4,
	    5,
	    6,
	    4,
	    6,
	    7,
	    // Top
	    8,
	    9,
	    10,
	    8,
	    10,
	    11,
	    // Bottom
	    12,
	    13,
	    14,
	    12,
	    14,
	    15,
	    // Right
	    16,
	    17,
	    18,
	    16,
	    18,
	    19,
	    // Left
	    20,
	    21,
	    22,
	    20,
	    22,
	    23,
	};

	return geom;
}

PrimitiveGeometry Primitives::createSphere(float radius, int rings,
                                           int segments) {
	PrimitiveGeometry geom;

	// Generate vertices
	for (int ring = 0; ring <= rings; ring++) {
		float phi = glm::pi<float>() * ring / rings;
		float y = radius * cos(phi);
		float ringRadius = radius * sin(phi);

		for (int segment = 0; segment <= segments; segment++) {
			float theta = 2.0f * glm::pi<float>() * segment / segments;
			float x = ringRadius * cos(theta);
			float z = ringRadius * sin(theta);

			Vertex v;
			v.position = glm::vec3(x, y, z);
			v.normal = glm::normalize(v.position);
			// Color based on height (gradient from bottom to top)
			float t = (y + radius) / (2.0f * radius);
			v.color = glm::vec3(t, 0.5f, 1.0f - t);
			geom.vertices.push_back(v);
		}
	}

	// Generate indices
	for (int ring = 0; ring < rings; ring++) {
		for (int segment = 0; segment < segments; segment++) {
			int current = ring * (segments + 1) + segment;
			int next = current + segments + 1;

			// Two triangles per quad
			geom.indices.push_back(current);
			geom.indices.push_back(next);
			geom.indices.push_back(current + 1);

			geom.indices.push_back(current + 1);
			geom.indices.push_back(next);
			geom.indices.push_back(next + 1);
		}
	}

	return geom;
}

PrimitiveGeometry Primitives::createCone(float radius, float height,
                                         int segments) {
	PrimitiveGeometry geom;
	float halfHeight = height * 0.5f;

	// --- Side face vertices ---

	// Apex (index 0)
	geom.vertices.push_back({{0.0f, halfHeight, 0.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}});

	// Base ring for sides (indices 1..segments+1)
	// Outward-perpendicular to the lateral surface: normalize(height*cos, radius, height*sin)
	for (int i = 0; i <= segments; i++) {
		float theta = 2.0f * glm::pi<float>() * i / segments;
		float x = radius * cos(theta);
		float z = radius * sin(theta);
		glm::vec3 sideNormal = glm::normalize(glm::vec3(height * cos(theta), radius, height * sin(theta)));
		geom.vertices.push_back({{x, -halfHeight, z}, sideNormal, {1.0f, 1.0f, 1.0f}});
	}

	// Side triangles (apex to base edge)
	for (int i = 0; i < segments; i++) {
		geom.indices.push_back(0);          // Apex
		geom.indices.push_back(1 + i);      // Base edge
		geom.indices.push_back(1 + i + 1);  // Next base edge
	}

	// --- Base cap vertices ---

	uint32_t baseCenterIdx = static_cast<uint32_t>(geom.vertices.size());
	geom.vertices.push_back({{0.0f, -halfHeight, 0.0f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}});

	uint32_t baseRingStart = static_cast<uint32_t>(geom.vertices.size());
	for (int i = 0; i <= segments; i++) {
		float theta = 2.0f * glm::pi<float>() * i / segments;
		float x = radius * cos(theta);
		float z = radius * sin(theta);
		geom.vertices.push_back({{x, -halfHeight, z}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}});
	}

	// Base triangles (base center to base edge, reversed winding)
	for (int i = 0; i < segments; i++) {
		geom.indices.push_back(baseCenterIdx);
		geom.indices.push_back(baseRingStart + i + 1);
		geom.indices.push_back(baseRingStart + i);
	}

	return geom;
}

PrimitiveGeometry Primitives::createCylinder(float radius, float height,
                                             int segments) {
	PrimitiveGeometry geom;
	float halfHeight = height * 0.5f;

	// --- Top cap (normal: +Y) ---

	uint32_t topCenter = static_cast<uint32_t>(geom.vertices.size());
	geom.vertices.push_back({{0.0f, halfHeight, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.5f, 1.0f, 0.5f}});

	uint32_t topCapRingStart = static_cast<uint32_t>(geom.vertices.size());
	for (int i = 0; i <= segments; i++) {
		float theta = 2.0f * glm::pi<float>() * i / segments;
		float x = radius * cos(theta);
		float z = radius * sin(theta);
		geom.vertices.push_back({{x, halfHeight, z}, {0.0f, 1.0f, 0.0f}, {0.5f, 1.0f, 0.5f}});
	}

	for (int i = 0; i < segments; i++) {
		geom.indices.push_back(topCenter);
		geom.indices.push_back(topCapRingStart + i);
		geom.indices.push_back(topCapRingStart + i + 1);
	}

	// --- Bottom cap (normal: -Y) ---

	uint32_t bottomCenter = static_cast<uint32_t>(geom.vertices.size());
	geom.vertices.push_back({{0.0f, -halfHeight, 0.0f}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.0f, 0.5f}});

	uint32_t bottomCapRingStart = static_cast<uint32_t>(geom.vertices.size());
	for (int i = 0; i <= segments; i++) {
		float theta = 2.0f * glm::pi<float>() * i / segments;
		float x = radius * cos(theta);
		float z = radius * sin(theta);
		geom.vertices.push_back({{x, -halfHeight, z}, {0.0f, -1.0f, 0.0f}, {0.5f, 0.0f, 0.5f}});
	}

	for (int i = 0; i < segments; i++) {
		geom.indices.push_back(bottomCenter);
		geom.indices.push_back(bottomCapRingStart + i + 1);
		geom.indices.push_back(bottomCapRingStart + i);
	}

	// --- Side faces (outward radial normals) ---

	uint32_t sideTopStart = static_cast<uint32_t>(geom.vertices.size());
	for (int i = 0; i <= segments; i++) {
		float theta = 2.0f * glm::pi<float>() * i / segments;
		float x = radius * cos(theta);
		float z = radius * sin(theta);
		glm::vec3 outward = {cos(theta), 0.0f, sin(theta)};
		geom.vertices.push_back({{x, halfHeight, z}, outward, {0.5f, 1.0f, 0.5f}});
	}

	uint32_t sideBottomStart = static_cast<uint32_t>(geom.vertices.size());
	for (int i = 0; i <= segments; i++) {
		float theta = 2.0f * glm::pi<float>() * i / segments;
		float x = radius * cos(theta);
		float z = radius * sin(theta);
		glm::vec3 outward = {cos(theta), 0.0f, sin(theta)};
		geom.vertices.push_back({{x, -halfHeight, z}, outward, {0.5f, 0.0f, 0.5f}});
	}

	for (int i = 0; i < segments; i++) {
		uint32_t topCurrent = sideTopStart + i;
		uint32_t topNext = sideTopStart + i + 1;
		uint32_t bottomCurrent = sideBottomStart + i;
		uint32_t bottomNext = sideBottomStart + i + 1;

		geom.indices.push_back(topCurrent);
		geom.indices.push_back(bottomCurrent);
		geom.indices.push_back(topNext);

		geom.indices.push_back(topNext);
		geom.indices.push_back(bottomCurrent);
		geom.indices.push_back(bottomNext);
	}

	return geom;
}

PrimitiveGeometry Primitives::createPlane(float width, float height) {
	PrimitiveGeometry geom;
	float halfW = width * 0.5f;
	float halfH = height * 0.5f;

	geom.vertices = {
	    {{-halfW, 0.0f, -halfH}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.8f, 0.8f}},
	    {{halfW, 0.0f, -halfH}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.8f, 0.8f}},
	    {{halfW, 0.0f, halfH}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.8f, 0.8f}},
	    {{-halfW, 0.0f, halfH}, {0.0f, 1.0f, 0.0f}, {0.8f, 0.8f, 0.8f}},
	};

	geom.indices = {0, 1, 2, 0, 2, 3};

	return geom;
}
