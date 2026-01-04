#include "world.h"

Entity World::createEntity() { return m_entityManager.createEntity(); }

void World::destroyEntity(Entity entity) {
	if (!m_entityManager.isAlive(entity)) {
		throw std::runtime_error("Cannot destroy dead entity!");
	}

	for (auto& [typeIndex, pool] : m_componentPools) {
		if (pool->has(entity)) {
			pool->remove(entity);
		}
	}

	m_entityManager.destroyEntity(entity);
}

bool World::isEntityAlive(Entity entity) const {
	return m_entityManager.isAlive(entity);
}