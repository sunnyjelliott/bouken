#pragma once

#include "pch.h"
#include "world.h"

class TransformSystem {
   public:
	// Update all world matrices based on hierarchy
	void update(World& world);

   private:
	// Recursively update entity and its children
	void updateEntityRecursive(World& world, Entity entity,
	                           const glm::mat4& parentWorld);
};