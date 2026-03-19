#pragma once
#include "entity.h"
#include "pch.h"

class World;

template <typename... Components>
class View {
   public:
	class Iterator {
	   public:
		Iterator(World* world, Entity current, Entity end);

		Entity operator*() const;
		Iterator& operator++();
		bool operator!=(const Iterator& other) const;

	   private:
		void skipInvalid();
		bool hasAllComponents() const;

		World* m_world;
		Entity m_current;
		Entity m_end;
	};

	View(World* world);

	Iterator begin();
	Iterator end();

   private:
	World* m_world;
};

#include "view.inl"