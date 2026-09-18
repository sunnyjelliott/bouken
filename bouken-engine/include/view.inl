#pragma once

#include "view.h"
#include "world.h"

// Iterator implementation
template <typename... Components>
View<Components...>::Iterator::Iterator(World* world, uint16_t currentIndex,
                                        uint16_t endIndex)
    : m_world(world), m_currentIndex(currentIndex), m_endIndex(endIndex) {
	skipInvalid();
}

template <typename... Components>
Entity View<Components...>::Iterator::operator*() const {
	return m_world->getHandleAtIndex(m_currentIndex);
}

template <typename... Components>
typename View<Components...>::Iterator&
View<Components...>::Iterator::operator++() {
	m_currentIndex++;
	skipInvalid();
	return *this;
}

template <typename... Components>
bool View<Components...>::Iterator::operator!=(const Iterator& other) const {
	return m_currentIndex != other.m_currentIndex;
}

template <typename... Components>
void View<Components...>::Iterator::skipInvalid() {
	while (m_currentIndex < m_endIndex && !hasAllComponents()) {
		m_currentIndex++;
	}
}

template <typename... Components>
bool View<Components...>::Iterator::hasAllComponents() const {
	if (!m_world->isIndexAlive(m_currentIndex)) {
		return false;
	}

	Entity handle = m_world->getHandleAtIndex(m_currentIndex);
	return (m_world->hasComponent<Components>(handle) && ...);
}

// View implementation
template <typename... Components>
View<Components...>::View(World* world) : m_world(world) {}

template <typename... Components>
typename View<Components...>::Iterator View<Components...>::begin() {
	return Iterator(m_world, 0,
	                static_cast<uint16_t>(m_world->getEntityCapacity()));
}

template <typename... Components>
typename View<Components...>::Iterator View<Components...>::end() {
	uint16_t capacity = static_cast<uint16_t>(m_world->getEntityCapacity());
	return Iterator(m_world, capacity, capacity);
}