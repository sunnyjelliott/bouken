#pragma once
#include "pch.h"
#include "vertex.h"

struct PrimitiveGeometry {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
};

class Primitives {
   public:
	static PrimitiveGeometry createCube(float size = 1.0f);
	static PrimitiveGeometry createSphere(float radius = 1.0f, int rings = 16,
	                                      int segments = 16);
	static PrimitiveGeometry createCone(float radius = 1.0f,
	                                    float height = 2.0f, int segments = 16);
	static PrimitiveGeometry createCylinder(float radius = 1.0f,
	                                        float height = 2.0f,
	                                        int segments = 16);
	static PrimitiveGeometry createPlane(float width = 1.0f,
	                                     float height = 1.0f);
};