#pragma once
#include "pch.h"

enum class LightType : uint32_t {
	Directional = 0,
	Point = 1,
	Spot = 2,
};

struct Light {
	LightType type = LightType::Point;
	glm::vec3 color = glm::vec3(1.0f);
	float intensity = 1.0f;

	// Point + Spot
	float radius = 10.0f;

	// Spot
	float innerAngle = 15.0f;  // degrees
	float outerAngle = 30.0f;  // degrees
};