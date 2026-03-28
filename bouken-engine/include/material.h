#pragma once
#include "pch.h"

constexpr uint32_t MAT_FLAG_HAS_ALBEDO = 1 << 0;
constexpr uint32_t MAT_FLAG_HAS_NORMAL = 1 << 1;
constexpr uint32_t MAT_FLAG_HAS_METALLIC = 1 << 2;
constexpr uint32_t MAT_FLAG_HAS_ROUGHNESS = 1 << 3;
constexpr uint32_t MAT_FLAG_HAS_AO = 1 << 4;
constexpr uint32_t MAT_FLAG_HAS_EMISSIVE = 1 << 5;

struct MaterialConstants {
	glm::vec4 baseColor;      // rgb + w unused
	glm::vec4 emissiveColor;  // rgb + w unused
	float metallic;
	float roughness;
	float occlusion;
	float opacity;
	uint32_t textureFlags;
	uint32_t _pad[3];
};

struct Material {
	// fallback constants
	glm::vec3 baseColor = glm::vec3(1.0f);
	float metallic = 0.0f;
	float roughness = 0.5f;
	float occlusion = 1.0f;
	glm::vec3 emissiveColor = glm::vec3(0.0f);
	float opacity = 1.0f;

	// Texture overrides
	uint32_t albedoTextureID = 0;
	uint32_t normalTextureID = 0;
	uint32_t metallicTextureID = 0;
	uint32_t roughnessTextureID = 0;
	uint32_t aoTextureID = 0;
	uint32_t emissiveTextureID = 0;

	MaterialConstants toConstants() const {
		MaterialConstants mc{};
		mc.baseColor = glm::vec4(baseColor, 1.0f);
		mc.metallic = metallic;
		mc.roughness = roughness;
		mc.occlusion = occlusion;
		mc.emissiveColor = glm::vec4(emissiveColor, 1.0f);
		mc.opacity = opacity;

		if (albedoTextureID != 0) mc.textureFlags |= MAT_FLAG_HAS_ALBEDO;
		if (normalTextureID != 0) mc.textureFlags |= MAT_FLAG_HAS_NORMAL;
		if (metallicTextureID != 0) mc.textureFlags |= MAT_FLAG_HAS_METALLIC;
		if (roughnessTextureID != 0) mc.textureFlags |= MAT_FLAG_HAS_ROUGHNESS;
		if (aoTextureID != 0) mc.textureFlags |= MAT_FLAG_HAS_AO;
		if (emissiveTextureID != 0) mc.textureFlags |= MAT_FLAG_HAS_EMISSIVE;

		return mc;
	}
};

struct MaterialBinding {
	uint32_t materialID = 0;
};