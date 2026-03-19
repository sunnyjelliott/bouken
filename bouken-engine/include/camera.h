#pragma once

#include "pch.h"

struct Camera {
	float fov = 45.0f;
	float nearPlane = 0.1f;
	float farPlane = 100.0f;

	glm::mat4 getProjectionMatrix(float aspectRatio) const {
		glm::mat4 projection = glm::perspective(glm::radians(fov), aspectRatio,
		                                        nearPlane, farPlane);
		projection[1][1] *= -1;
		return projection;
	}
};