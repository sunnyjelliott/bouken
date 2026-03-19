#include "world.h"

#include "hierarchy.h"

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

void World::setParent(Entity child, Entity parent) {
	if (!isEntityAlive(child) || !isEntityAlive(parent)) {
		throw std::runtime_error("Cannot set parent: entity is dead!");
	}

	if (child == parent) {
		throw std::runtime_error("Entity cannot be its own parent!");
	}

	// Remove from old parent if exists
	if (hasComponent<Parent>(child)) {
		Entity oldParent = getComponent<Parent>(child).parent;
		if (isEntityAlive(oldParent) && hasComponent<Children>(oldParent)) {
			Children& oldParentChildren = getComponent<Children>(oldParent);
			auto& vec = oldParentChildren.children;
			vec.erase(std::remove(vec.begin(), vec.end(), child), vec.end());
		}
	}

	// Set new parent
	if (!hasComponent<Parent>(child)) {
		addComponent(child, Parent{parent});
	} else {
		getComponent<Parent>(child).parent = parent;
	}

	// Add to parent's children
	if (!hasComponent<Children>(parent)) {
		addComponent(parent, Children{});
	}
	getComponent<Children>(parent).children.push_back(child);
}

void World::removeParent(Entity child) {
	if (!hasComponent<Parent>(child)) return;

	Entity parent = getComponent<Parent>(child).parent;

	// Remove from parent's children
	if (isEntityAlive(parent) && hasComponent<Children>(parent)) {
		Children& parentChildren = getComponent<Children>(parent);
		auto& vec = parentChildren.children;
		vec.erase(std::remove(vec.begin(), vec.end(), child), vec.end());
	}

	// Remove parent component
	removeComponent<Parent>(child);
}

Entity World::getParent(Entity child) const {
	if (!hasComponent<Parent>(child)) {
		return NULL_ENTITY;
	}
	return getComponent<Parent>(child).parent;
}

const std::vector<Entity>& World::getChildren(Entity parent) const {
	static std::vector<Entity> empty;
	if (!hasComponent<Children>(parent)) {
		return empty;
	}
	return getComponent<Children>(parent).children;
}
