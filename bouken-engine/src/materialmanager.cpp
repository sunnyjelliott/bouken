#include "materialmanager.h"

void MaterialManager::initialize() {
	createDefaultMaterial();
	std::cout << "MaterialManager initialized" << std::endl;
}

void MaterialManager::cleanup() {
	m_materials.clear();
	m_nextID = 1;
	std::cout << "MaterialManager cleaned up" << std::endl;
}

void MaterialManager::createDefaultMaterial() {
	// Create default material at ID 0
	Material defaultMat;
	defaultMat.baseColor = glm::vec3(0.8f, 0.8f, 0.8f);  // Light gray
	defaultMat.albedoTextureID = 0;
	defaultMat.normalTextureID = 0;

	m_materials[0] = defaultMat;
	std::cout << "Created default material (ID: 0)" << std::endl;
}

uint32_t MaterialManager::createMaterial(const Material& material) {
	uint32_t materialID = m_nextID++;
	m_materials[materialID] = material;

	std::cout << "Created material ID: " << materialID << std::endl;
	std::cout << "  baseColor: (" << material.baseColor.r << ", "
	          << material.baseColor.g << ", " << material.baseColor.b << ")"
	          << std::endl;

	if (material.albedoTextureID != 0) {
		std::cout << "  albedoTexture: ID " << material.albedoTextureID
		          << std::endl;
	} else {
		std::cout << "  albedoTexture: none (using baseColor)" << std::endl;
	}

	if (material.normalTextureID != 0) {
		std::cout << "  normalTexture: ID " << material.normalTextureID
		          << std::endl;
	}

	return materialID;
}

const Material& MaterialManager::getMaterial(uint32_t materialID) const {
	auto it = m_materials.find(materialID);
	if (it == m_materials.end()) {
		// Return default material if not found
		static const Material defaultMat = m_materials.at(0);
		return defaultMat;
	}
	return it->second;
}

Material& MaterialManager::getMaterial(uint32_t materialID) {
	auto it = m_materials.find(materialID);
	if (it == m_materials.end()) {
		throw std::runtime_error("Material not found: " +
		                         std::to_string(materialID));
	}
	return it->second;
}

bool MaterialManager::hasMaterial(uint32_t materialID) const {
	return m_materials.find(materialID) != m_materials.end();
}