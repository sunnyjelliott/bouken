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

	MaterialData data;
	data.material = defaultMat;
	data.descriptorSet = VK_NULL_HANDLE;  // Will be created by RenderSystem

	m_materials[0] = data;
	std::cout << "Created default material (ID: 0)" << std::endl;
}

uint32_t MaterialManager::createMaterial(const Material& material) {
	uint32_t materialID = m_nextID++;

	MaterialData data;
	data.material = material;
	data.descriptorSet = VK_NULL_HANDLE;  // Will be created later

	m_materials[materialID] = data;

	std::cout << "Created material ID: " << materialID << std::endl;
	std::cout << "  baseColor: (" << material.baseColor.r << ", "
	          << material.baseColor.g << ", " << material.baseColor.b << ")"
	          << std::endl;

	return materialID;
}

void MaterialManager::createDescriptorSet(uint32_t materialID,
                                          VkDescriptorSet descriptorSet) {
	auto it = m_materials.find(materialID);
	if (it != m_materials.end()) {
		it->second.descriptorSet = descriptorSet;
	}
}

const Material& MaterialManager::getMaterial(uint32_t materialID) const {
	auto it = m_materials.find(materialID);
	if (it == m_materials.end()) {
		// Return default material if not found
		static const Material defaultMat = m_materials.at(0).material;
		return defaultMat;
	}
	return it->second.material;
}

Material& MaterialManager::getMaterial(uint32_t materialID) {
	auto it = m_materials.find(materialID);
	if (it == m_materials.end()) {
		throw std::runtime_error("Material not found: " +
		                         std::to_string(materialID));
	}
	return it->second.material;
}

VkDescriptorSet MaterialManager::getDescriptorSet(uint32_t materialID) const {
	auto it = m_materials.find(materialID);
	if (it == m_materials.end() || it->second.descriptorSet == VK_NULL_HANDLE) {
		// Return default material's descriptor set
		auto defaultIt = m_materials.find(0);
		if (defaultIt != m_materials.end()) {
			return defaultIt->second.descriptorSet;
		}
		return VK_NULL_HANDLE;
	}
	return it->second.descriptorSet;
}

bool MaterialManager::hasMaterial(uint32_t materialID) const {
	return m_materials.find(materialID) != m_materials.end();
}