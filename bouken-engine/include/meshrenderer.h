#pragma once

#include "pch.h"

struct MeshRenderer {
	uint32_t meshID = 0;  // Legacy
	std::vector<uint32_t> meshIDs;
	uint32_t materialID = 0;
	bool visible = true;

	std::vector<uint32_t> getMeshIDs() const {
		if (!meshIDs.empty()) {
			return meshIDs;
		}
		return {meshID};
	}

	bool hasMultipleMeshes() const { return !meshIDs.empty(); }
};