#pragma once
#include "pch.h"

struct Material {
	glm::vec3 baseColor = glm::vec3(1.0f, 1.0f, 1.0f);

	uint32_t albedoTextureID = 0;
	uint32_t normalTextureID = 0;
};

struct MaterialBinding {
	uint32_t materialID = 0;
};