#pragma once
#include "entity.h"
#include "pch.h"

class World;

template <typename... Components>
class View {
   public:
	class Iterator {
	   public:
		Iterator(World* world, uint16_t currentIndex, uint16_t endIndex);

		Entity operator*() const;
		Iterator& operator++();
		bool operator!=(const Iterator& other) const;

	   private:
		void skipInvalid();
		bool hasAllComponents() const;

		World* m_world;
		uint16_t m_currentIndex;
		uint16_t m_endIndex;
	};

	View(World* world);

	Iterator begin();
	Iterator end();

   private:
	World* m_world;
};

#include "view.inl"