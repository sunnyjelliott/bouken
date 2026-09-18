#pragma once
#include "spatial.h"

// Per-entity bounds, in two spaces.
//
// local: union of RenderSystem::m_meshAABBs over MeshRenderer::getMeshIDs(),
//        in whatever space that mesh's vertices live in. Written ONCE, by
//        whoever creates the entity. RenderSystem::m_meshAABBs is the single
//        source of truth - never refit this from vertices.
//        INVARIANT: anything that mutates MeshRenderer::meshIDs must rewrite
//        this via RenderSystem::getMeshAABB(meshIDs).
//
// world: local transformed by Transform::worldMatrix. Derived every frame by
//        BoundsSystem, which is its ONLY writer. Every consumer - frustum
//        culling, shadow caster collection, m_sceneBounds - reads this one.
struct BoundingBox {
	AABB local;
	AABB world;
};
