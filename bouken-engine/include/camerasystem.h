#pragma once

#include "entity.h"
#include "pch.h"
#include "world.h"

class CameraSystem {
   public:
	void setActiveCamera(Entity camera);
	Entity getActiveCamera() const { return m_activeCamera; }

	glm::mat4 getViewMatrix(World& world) const;

	glm::mat4 getProjectionMatrix(World& world, float aspectRatio) const;

	void updateFreeFly(World& world, float deltaTime, bool moveForward,
	                   bool moveBackward, bool moveLeft, bool moveRight,
	                   float lookHorizontal, float lookVertical,
	                   bool sprint = false);

   private:
	Entity m_activeCamera = NULL_ENTITY;

	float m_yaw = 0.0f;
	float m_pitch = 0.0f;
	float m_moveSpeed = 100.0f;
	float m_lookSpeed = 0.1f;
};