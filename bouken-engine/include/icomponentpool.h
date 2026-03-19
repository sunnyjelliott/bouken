#pragma once
#include "entity.h"
#include "pch.h"

// Type-erased interface for component pools
// Allows World to store different component types in a single container
class IComponentPool {
   public:
	virtual ~IComponentPool() = default;

	// Remove component for an entity
	virtual void remove(Entity entity) = 0;

	// Check if entity has a component
	virtual bool has(Entity entity) const = 0;

	// Get number of components in pool
	virtual size_t size() const = 0;
};