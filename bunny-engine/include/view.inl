#pragma once

#include "view.h"
#include "world.h"

// Iterator implementation
template <typename... Components>
View<Components...>::Iterator::Iterator(World* world, Entity current,
                                        Entity end)
    : m_world(world), m_current(current), m_end(end) {
	skipInvalid();
}

template <typename... Components>
Entity View<Components...>::Iterator::operator*() const {
	return m_current;
}

template <typename... Components>
typename View<Components...>::Iterator&
View<Components...>::Iterator::operator++() {
	m_current++;
	skipInvalid();
	return *this;
}

template <typename... Components>
bool View<Components...>::Iterator::operator!=(const Iterator& other) const {
	return m_current != other.m_current;
}

template <typename... Components>
void View<Components...>::Iterator::skipInvalid() {
	while (m_current < m_end && !hasAllComponents()) {
		m_current++;
	}
}

template <typename... Components>
bool View<Components...>::Iterator::hasAllComponents() const {
	if (!m_world->isEntityAlive(m_current)) {
		return false;
	}

	// Fold expression: check all components
	return (m_world->hasComponent<Components>(m_current) && ...);
}

// View implementation
template <typename... Components>
View<Components...>::View(World* world) : m_world(world) {}

template <typename... Components>
typename View<Components...>::Iterator View<Components...>::begin() {
	return Iterator(m_world, 0, EntityUtil::MAX_ENTITIES);
}

template <typename... Components>
typename View<Components...>::Iterator View<Components...>::end() {
	return Iterator(m_world, EntityUtil::MAX_ENTITIES,
	                EntityUtil::MAX_ENTITIES);
}