#pragma once

#include "entity.h"
#include "pch.h"

struct Parent {
	Entity parent = NULL_ENTITY;
};

struct Children {
	std::vector<Entity> children;
};