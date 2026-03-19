#pragma once
#include "material.h"
#include "pch.h"

class MaterialManager {
   public:
	void initialize();
	void cleanup();

	uint32_t createMaterial(const Material& material);

	const Material& getMaterial(uint32_t materialID) const;
	Material& getMaterial(uint32_t materialID);

	bool hasMaterial(uint32_t materialID) const;

	uint32_t getDefaultMaterialID() const { return 0; }

	size_t getMaterialCount() const { return m_materials.size(); }

   private:
	std::unordered_map<uint32_t, Material> m_materials;
	uint32_t m_nextID = 1;

	void createDefaultMaterial();
};