#pragma once
#include "entity.h"
#include "icomponentpool.h"
#include "pch.h"

// Struct-of-Arrays component storage
// All components of type T are stored contiguously for cache performance
template <typename T>
class ComponentPool : public IComponentPool {
   public:
	// Add component to entity
	void add(Entity entity, T&& component);

	// Remove component from entity
	void remove(Entity entity) override;

	// Check if entity has component
	bool has(Entity entity) const override;

	// Get component for entity
	T& get(Entity entity);
	const T& get(Entity entity) const;

	// Get number of components
	size_t size() const override { return m_components.size(); }

	// Iteration support (for systems)
	std::vector<T>& getComponents() { return m_components; }
	const std::vector<T>& getComponents() const { return m_components; }

	std::vector<Entity>& getEntities() { return m_entities; }
	const std::vector<Entity>& getEntities() const { return m_entities; }

   private:
	// SoA: All components packed together
	std::vector<T> m_components;

	// Parallel array: which entity owns each component
	std::vector<Entity> m_entities;

	// Mapping: entity -> component index
	std::unordered_map<Entity, size_t> m_entityToIndex;
};

template <typename T>
void ComponentPool<T>::add(Entity entity, T&& component) {
	if (has(entity)) {
		throw std::runtime_error("Entity already has this component type!");
	}

	// Add component to end of array
	size_t index = m_components.size();
	m_components.push_back(std::forward<T>(component));
	m_entities.push_back(entity);
	m_entityToIndex[entity] = index;
}

template <typename T>
void ComponentPool<T>::remove(Entity entity) {
	if (!has(entity)) {
		throw std::runtime_error("Entity does not have this component type!");
	}

	// Swap-and-pop for O(1) removal
	size_t indexToRemove = m_entityToIndex[entity];
	size_t lastIndex = m_components.size() - 1;

	if (indexToRemove != lastIndex) {
		// Swap with last element
		std::swap(m_components[indexToRemove], m_components[lastIndex]);
		std::swap(m_entities[indexToRemove], m_entities[lastIndex]);

		// Update mapping for the swapped entity
		Entity swappedEntity = m_entities[indexToRemove];
		m_entityToIndex[swappedEntity] = indexToRemove;
	}

	// Remove last element
	m_components.pop_back();
	m_entities.pop_back();
	m_entityToIndex.erase(entity);
}

template <typename T>
bool ComponentPool<T>::has(Entity entity) const {
	return m_entityToIndex.find(entity) != m_entityToIndex.end();
}

template <typename T>
T& ComponentPool<T>::get(Entity entity) {
	auto it = m_entityToIndex.find(entity);
	if (it == m_entityToIndex.end()) {
		throw std::runtime_error("Entity does not have this component type!");
	}
	return m_components[it->second];
}

template <typename T>
const T& ComponentPool<T>::get(Entity entity) const {
	auto it = m_entityToIndex.find(entity);
	if (it == m_entityToIndex.end()) {
		throw std::runtime_error("Entity does not have this component type!");
	}
	return m_components[it->second];
}