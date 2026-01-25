#include "entitymanager.h"

Entity EntityManager::createEntity() {
	uint16_t index;

	if (m_freeIndices.empty()) {
		// No recycled indices available, allocate new index
		index = static_cast<uint16_t>(m_versions.size());

		if (index >= EntityUtil::MAX_ENTITIES) {
			throw std::runtime_error("Maximum entity count reached!");
		}

		// Initialize version to 0 for new index
		m_versions.push_back(0);
	} else {
		// Reuse a recycled index
		index = m_freeIndices.front();
		m_freeIndices.pop();
	}

	uint16_t version = m_versions[index];
	m_aliveCount++;

	return EntityUtil::createHandle(index, version);
}

void EntityManager::destroyEntity(Entity entity) {
	uint16_t index = EntityUtil::getIndex(entity);
	uint16_t version = EntityUtil::getVersion(entity);

	// Validate entity
	if (index >= m_versions.size()) {
		throw std::runtime_error("Invalid entity index!");
	}

	if (m_versions[index] != version) {
		throw std::runtime_error("Stale entity handle!");
	}

	// Increment version to invalidate old handles
	m_versions[index]++;

	// Add index to free list for reuse
	m_freeIndices.push(index);

	m_aliveCount--;
}

bool EntityManager::isAlive(Entity entity) const {
	uint16_t index = EntityUtil::getIndex(entity);
	uint16_t version = EntityUtil::getVersion(entity);

	// Check if index is valid
	if (index >= m_versions.size()) {
		return false;
	}

	// Check if version matches (not stale)
	return m_versions[index] == version;
}
