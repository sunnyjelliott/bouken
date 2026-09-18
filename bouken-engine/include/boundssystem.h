#pragma once

#include "pch.h"
#include "world.h"

// Derives BoundingBox::world from BoundingBox::local and Transform::worldMatrix.
//
// Must run AFTER TransformSystem::update (it reads worldMatrix) and BEFORE
// RenderSystem::drawFrame - both ShadowSystem::collectCasters and
// RenderSystem::gatherRenderItems cull against BoundingBox::world.
class BoundsSystem {
   public:
	void update(World& world);
};
