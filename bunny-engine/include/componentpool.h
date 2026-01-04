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