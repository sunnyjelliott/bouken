#include "transformsystem.h"

#include "hierarchy.h"
#include "transform.h"

void TransformSystem::update(World& world) {
	ComponentPool<Transform>* transforms = world.getComponentPool<Transform>();
	if (!transforms) return;

	const auto& entities = transforms->getEntities();

	for (Entity entity : entities) {
		if (!world.hasComponent<Parent>(entity)) {
			glm::mat4 identity = glm::mat4(1.0f);
			updateEntityRecursive(world, entity, identity);
		}
	}
}

void TransformSystem::updateEntityRecursive(World& world, Entity entity,
                                            const glm::mat4& parentWorld) {
	Transform& transform = world.getComponent<Transform>(entity);
	transform.worldMatrix = parentWorld * transform.getLocalMatrix();

	if (world.hasComponent<Children>(entity)) {
		const Children& children = world.getComponent<Children>(entity);

		for (Entity child : children.children) {
			if (world.isEntityAlive(child)) {
				updateEntityRecursive(world, child, transform.worldMatrix);
			}
		}
	}
}
