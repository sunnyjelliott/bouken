#pragma once
#include "pch.h"

struct FrameUBO {
	glm::mat4 view;
	glm::mat4 projection;
	glm::mat4 viewProjection;  // precomputed
	glm::mat4 invProjection;
	glm::mat4 invView;
	glm::vec4 cameraPosition;  // w unused, vec4 for alignment
	glm::vec2 screenExtent;
	float time;
	float _pad;
};