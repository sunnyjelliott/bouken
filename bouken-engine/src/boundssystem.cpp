#include "boundssystem.h"

#include "boundingbox.h"
#include "transform.h"

void BoundsSystem::update(World& world) {
	// Iterating the pools directly, not world.view<>(): View scans the entire
	// 0..MAX_ENTITIES handle range with a type_index hash lookup plus a
	// per-pool lookup at every step, so its cost is fixed no matter how small
	// the scene is. Same reasoning as ShadowSystem::collectCasters and
	// TransformSystem::update.
	//
	// Driving off the BoundingBox pool rather than the Transform pool is also
	// strictly less work: every Xform prim, light and camera has a Transform,
	// but only geometry has bounds.
	ComponentPool<BoundingBox>* boxes = world.getComponentPool<BoundingBox>();
	if (!boxes) return;

	const ComponentPool<Transform>* transforms =
	    world.getComponentPool<Transform>();
	if (!transforms) return;

	const std::vector<Entity>& entities = boxes->getEntities();
	std::vector<BoundingBox>& components = boxes->getComponents();

	// Unconditional, matching TransformSystem::update. A bounds-only dirty
	// flag would be decorative while transforms rebuild every frame anyway,
	// and a flag someone forgets to set reproduces exactly the silent
	// stale-bounds bug this system exists to fix. When Transform gains a
	// dirty/version stamp, read that here - do not invent a second one.
	for (size_t i = 0; i < entities.size(); i++) {
		BoundingBox& bb = components[i];

		if (!transforms->has(entities[i])) {
			bb.world = bb.local;
			continue;
		}

		// transformed() is total: an invalid local stays invalid, so every
		// consumer's isValid() guard keeps working.
		bb.world =
		    bb.local.transformed(transforms->get(entities[i]).worldMatrix);
	}
}
