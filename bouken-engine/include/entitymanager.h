#pragma once
#include "entity.h"
#include "pch.h"

class EntityManager {
   public:
	Entity createEntity();
	void destroyEntity(Entity entity);
	bool isAlive(Entity entity) const;

	size_t getAliveCount() const { return m_aliveCount; }
	size_t getCapacity() const { return m_versions.size(); }

   private:
	std::vector<uint16_t> m_versions;

	std::queue<uint16_t> m_freeIndices;

	size_t m_aliveCount = 0;
};