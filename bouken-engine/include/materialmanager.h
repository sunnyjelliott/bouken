#pragma once
#include "material.h"
#include "pch.h"

// TODO: Abstract Descriptor Sets from VK

class MaterialManager {
   public:
	void initialize();
	void cleanup();

	uint32_t createMaterial(const Material& material);

	void createDescriptorSet(uint32_t materialID,
	                         VkDescriptorSet descriptorSet);

	const Material& getMaterial(uint32_t materialID) const;
	Material& getMaterial(uint32_t materialID);

	VkDescriptorSet getDescriptorSet(uint32_t materialID) const;

	bool hasMaterial(uint32_t materialID) const;

	uint32_t getDefaultMaterialID() const { return 0; }

	size_t getMaterialCount() const { return m_materials.size(); }

   private:
	struct MaterialData {
		Material material;
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	};

	std::unordered_map<uint32_t, MaterialData> m_materials;
	uint32_t m_nextID = 1;

	void createDefaultMaterial();
};