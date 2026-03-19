#pragma once

#include "pch.h"
#include "vertex.h"

struct LoadedMesh {
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
};

class MeshLoader {
   public:
	static LoadedMesh loadOBJ(const std::string& filepath);
};