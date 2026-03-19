#pragma once

#include "pch.h"

struct Transform {
	glm::vec3 position = glm::vec3(0.0f);
	glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
	glm::vec3 scale = glm::vec3(1.0f);

	glm::mat4 worldMatrix = glm::mat4(1.0f);

	glm::mat4 getLocalMatrix() const {
		glm::mat4 mat = glm::mat4(1.0f);
		mat = glm::translate(mat, position);
		mat = mat * glm::mat4_cast(rotation);
		mat = glm::scale(mat, scale);
		return mat;
	}
};