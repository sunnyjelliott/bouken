#pragma once

#include "pch.h"

struct Camera {
	glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f);
	glm::vec3 front = glm::vec3(0.0f, 0.0f, -1.0f);
	glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);

	float fov = 45.0f;
	float nearPlane = 0.1f;
	float farPlane = 100.0f;

	glm::mat4 getViewMatrix() const {
		return glm::lookAt(position, position + front, up);
	}

	glm::mat4 getProjectionMatrix(float aspectRatio) const {
		return glm::perspective(glm::radians(fov), aspectRatio, nearPlane,
		                        farPlane);
	}
};