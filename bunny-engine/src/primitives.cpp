#include "primitives.h"

PrimitiveGeometry Primitives::createCube(float size) {
	PrimitiveGeometry geom;
	float half = size * 0.5f;

	// 8 unique vertices
	geom.vertices = {
	    {{-half, -half, -half}, {0.0f, 1.0f, 1.0f}},  // 0: back-bottom-left
	    {{half, -half, -half}, {0.0f, 1.0f, 0.0f}},   // 1: back-bottom-right
	    {{half, half, -half}, {0.0f, 0.0f, 1.0f}},    // 2: back-top-right
	    {{-half, half, -half}, {0.0f, 1.0f, 0.0f}},   // 3: back-top-left
	    {{-half, -half, half}, {1.0f, 0.0f, 0.0f}},   // 4: front-bottom-left
	    {{half, -half, half}, {1.0f, 0.0f, 0.0f}},    // 5: front-bottom-right
	    {{half, half, half}, {1.0f, 0.0f, 1.0f}},     // 6: front-top-right
	    {{-half, half, half}, {0.0f, 0.0f, 1.0f}},    // 7: front-top-left
	};

	geom.indices = {
	    // Front face
	    4,
	    5,
	    6,
	    4,
	    6,
	    7,
	    // Back face
	    1,
	    0,
	    3,
	    1,
	    3,
	    2,
	    // Top face
	    7,
	    6,
	    2,
	    7,
	    2,
	    3,
	    // Bottom face
	    0,
	    1,
	    5,
	    0,
	    5,
	    4,
	    // Right face
	    5,
	    1,
	    2,
	    5,
	    2,
	    6,
	    // Left face
	    0,
	    4,
	    7,
	    0,
	    7,
	    3,
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

	// Apex vertex
	geom.vertices.push_back({{0.0f, halfHeight, 0.0f}, {1.0f, 1.0f, 0.0f}});

	// Base center vertex
	geom.vertices.push_back({{0.0f, -halfHeight, 0.0f}, {1.0f, 1.0f, 1.0f}});

	// Base circle vertices
	for (int i = 0; i <= segments; i++) {
		float theta = 2.0f * glm::pi<float>() * i / segments;
		float x = radius * cos(theta);
		float z = radius * sin(theta);

		geom.vertices.push_back({{x, -halfHeight, z}, {1.0f, 1.0f, 1.0f}});
	}

	// Side triangles (apex to base edge)
	for (int i = 0; i < segments; i++) {
		geom.indices.push_back(0);          // Apex
		geom.indices.push_back(2 + i);      // Base edge
		geom.indices.push_back(2 + i + 1);  // Next base edge
	}

	// Base triangles (base center to base edge)
	for (int i = 0; i < segments; i++) {
		geom.indices.push_back(1);          // Base center
		geom.indices.push_back(2 + i + 1);  // Base edge (reversed winding)
		geom.indices.push_back(2 + i);
	}

	return geom;
}

PrimitiveGeometry Primitives::createCylinder(float radius, float height,
                                             int segments) {
	PrimitiveGeometry geom;
	float halfHeight = height * 0.5f;

	// Top center
	geom.vertices.push_back({{0.0f, halfHeight, 0.0f}, {0.5f, 1.0f, 0.5f}});
	// Bottom center
	geom.vertices.push_back({{0.0f, -halfHeight, 0.0f}, {0.5f, 0.0f, 0.5f}});

	// Generate top and bottom rings
	for (int i = 0; i <= segments; i++) {
		float theta = 2.0f * glm::pi<float>() * i / segments;
		float x = radius * cos(theta);
		float z = radius * sin(theta);

		// Top ring
		geom.vertices.push_back({{x, halfHeight, z}, {0.5f, 1.0f, 0.5f}});
		// Bottom ring
		geom.vertices.push_back({{x, -halfHeight, z}, {0.5f, 0.0f, 0.5f}});
	}

	// Top cap
	for (int i = 0; i < segments; i++) {
		geom.indices.push_back(0);
		geom.indices.push_back(2 + i * 2);
		geom.indices.push_back(2 + (i + 1) * 2);
	}

	// Bottom cap
	for (int i = 0; i < segments; i++) {
		geom.indices.push_back(1);
		geom.indices.push_back(3 + (i + 1) * 2);
		geom.indices.push_back(3 + i * 2);
	}

	// Side faces
	for (int i = 0; i < segments; i++) {
		int topCurrent = 2 + i * 2;
		int topNext = 2 + (i + 1) * 2;
		int bottomCurrent = 3 + i * 2;
		int bottomNext = 3 + (i + 1) * 2;

		// Two triangles per side quad
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
	    {{-halfW, 0.0f, -halfH}, {0.8f, 0.8f, 0.8f}},
	    {{halfW, 0.0f, -halfH}, {0.8f, 0.8f, 0.8f}},
	    {{halfW, 0.0f, halfH}, {0.8f, 0.8f, 0.8f}},
	    {{-halfW, 0.0f, halfH}, {0.8f, 0.8f, 0.8f}},
	};

	geom.indices = {0, 1, 2, 0, 2, 3};

	return geom;
}