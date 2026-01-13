#pragma once
#include "componentpool.h"
#include "entity.h"
#include "entitymanager.h"
#include "icomponentpool.h"
#include "meshrenderer.h"
#include "pch.h"
#include "transform.h"

template <typename... Components>
class View;

class World {
   public:
	// Entity management
	Entity createEntity();
	void destroyEntity(Entity entity);
	bool isEntityAlive(Entity entity) const;

	// Component management
	template <typename T>
	void addComponent(Entity entity, T&& component);

	template <typename T>
	void removeComponent(Entity entity);

	template <typename T>
	bool hasComponent(Entity entity) const;

	template <typename T>
	T& getComponent(Entity entity);

	template <typename T>
	const T& getComponent(Entity entity) const;

	// Get component pool directly (for systems)
	template <typename T>
	ComponentPool<T>* getComponentPool();

	template <typename T>
	const ComponentPool<T>* getComponentPool() const;

	// Query entities with specific components
	template <typename... Components>
	View<Components...> view();

	// Stats
	size_t getEntityCount() const { return m_entityManager.getAliveCount(); }

   private:
	template <typename T>
	ComponentPool<T>* getOrCreatePool();

	EntityManager m_entityManager;
	std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>>
	    m_componentPools;
};

// World template implementations (keep these in World.h)

template <typename T>
void World::addComponent(Entity entity, T&& component) {
	if (!m_entityManager.isAlive(entity)) {
		throw std::runtime_error("Cannot add component to dead entity!");
	}

	ComponentPool<T>* pool = getOrCreatePool<T>();
	pool->add(entity, std::forward<T>(component));
}

template <typename T>
void World::removeComponent(Entity entity) {
	if (!m_entityManager.isAlive(entity)) {
		throw std::runtime_error("Cannot remove component from dead entity!");
	}

	ComponentPool<T>* pool = getComponentPool<T>();
	if (pool) {
		pool->remove(entity);
	}
}

template <typename T>
bool World::hasComponent(Entity entity) const {
	if (!m_entityManager.isAlive(entity)) {
		return false;
	}

	const ComponentPool<T>* pool = getComponentPool<T>();
	return pool ? pool->has(entity) : false;
}

template <typename T>
T& World::getComponent(Entity entity) {
	if (!m_entityManager.isAlive(entity)) {
		throw std::runtime_error("Cannot get component from dead entity!");
	}

	ComponentPool<T>* pool = getComponentPool<T>();
	if (!pool) {
		throw std::runtime_error("Component pool does not exist!");
	}

	return pool->get(entity);
}

template <typename T>
const T& World::getComponent(Entity entity) const {
	if (!m_entityManager.isAlive(entity)) {
		throw std::runtime_error("Cannot get component from dead entity!");
	}

	const ComponentPool<T>* pool = getComponentPool<T>();
	if (!pool) {
		throw std::runtime_error("Component pool does not exist!");
	}

	return pool->get(entity);
}

template <typename T>
ComponentPool<T>* World::getComponentPool() {
	std::type_index typeIndex(typeid(T));

	auto it = m_componentPools.find(typeIndex);
	if (it == m_componentPools.end()) {
		return nullptr;
	}

	return static_cast<ComponentPool<T>*>(it->second.get());
}

template <typename T>
const ComponentPool<T>* World::getComponentPool() const {
	std::type_index typeIndex(typeid(T));

	auto it = m_componentPools.find(typeIndex);
	if (it == m_componentPools.end()) {
		return nullptr;
	}

	return static_cast<const ComponentPool<T>*>(it->second.get());
}

template <typename T>
ComponentPool<T>* World::getOrCreatePool() {
	std::type_index typeIndex(typeid(T));

	auto it = m_componentPools.find(typeIndex);
	if (it == m_componentPools.end()) {
		auto pool = std::make_unique<ComponentPool<T>>();
		ComponentPool<T>* poolPtr = pool.get();
		m_componentPools[typeIndex] = std::move(pool);
		return poolPtr;
	}

	return static_cast<ComponentPool<T>*>(it->second.get());
}

// View implementation
template <typename... Components>
View<Components...> World::view() {
	return View<Components...>(this);
}

#include "view.h"