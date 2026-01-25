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

	void updateFreeFly(World& world, float deltaTime, bool isForwardPressed,
	                   bool isBackPressed, bool isLeftPressed,
	                   bool isRightPressed, float mouseDeltaX,
	                   float mouseDeltaY);

   private:
	Entity m_activeCamera = NULL_ENTITY;

	float m_yaw = 0.0f;
	float m_pitch = 0.0f;
	float m_moveSpeed = 2.0f;
	float m_lookSpeed = 0.1f;
};