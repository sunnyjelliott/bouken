#include "camerasystem.h"

#include "camera.h"
#include "transform.h"

void CameraSystem::setActiveCamera(Entity camera) { m_activeCamera = camera; }

glm::mat4 CameraSystem::getViewMatrix(World& world) const {
	if (m_activeCamera == NULL_ENTITY || !world.isEntityAlive(m_activeCamera)) {
		return glm::mat4(1.0f);  // Identity matrix
	}

	if (!world.hasComponent<Transform>(m_activeCamera)) {
		return glm::mat4(1.0f);
	}

	Transform& transform = world.getComponent<Transform>(m_activeCamera);

	// Camera looks down -Z axis by default
	glm::vec3 front = transform.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
	glm::vec3 up = transform.rotation * glm::vec3(0.0f, 1.0f, 0.0f);

	return glm::lookAt(transform.position, transform.position + front, up);
}

glm::mat4 CameraSystem::getProjectionMatrix(World& world,
                                            float aspectRatio) const {
	if (m_activeCamera == NULL_ENTITY || !world.isEntityAlive(m_activeCamera)) {
		return glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
	}

	if (!world.hasComponent<Camera>(m_activeCamera)) {
		return glm::perspective(glm::radians(45.0f), aspectRatio, 0.1f, 100.0f);
	}

	Camera& camera = world.getComponent<Camera>(m_activeCamera);
	return camera.getProjectionMatrix(aspectRatio);
}

void CameraSystem::updateFreeFly(World& world, float deltaTime,
                                 bool isForwardPressed, bool isBackPressed,
                                 bool isLeftPressed, bool isRightPressed,
                                 float mouseDeltaX, float mouseDeltaY) {
	if (m_activeCamera == NULL_ENTITY || !world.isEntityAlive(m_activeCamera)) {
		return;
	}

	if (!world.hasComponent<Transform>(m_activeCamera)) {
		return;
	}

	Transform& transform = world.getComponent<Transform>(m_activeCamera);

	// Update rotation from mouse
	m_yaw += mouseDeltaX * m_lookSpeed;
	m_pitch += mouseDeltaY * m_lookSpeed;

	// Clamp pitch to avoid gimbal lock
	if (m_pitch > 89.0f) m_pitch = 89.0f;
	if (m_pitch < -89.0f) m_pitch = -89.0f;

	// Update rotation quaternion from yaw/pitch
	glm::quat yawQuat =
	    glm::angleAxis(glm::radians(m_yaw), glm::vec3(0.0f, 1.0f, 0.0f));
	glm::quat pitchQuat =
	    glm::angleAxis(glm::radians(m_pitch), glm::vec3(1.0f, 0.0f, 0.0f));

	transform.rotation = yawQuat * pitchQuat;

	// Calculate front vector from yaw/pitch
	glm::vec3 front = transform.rotation * glm::vec3(0.0f, 0.0f, -1.0f);
	glm::vec3 right =
	    glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));

	float velocity = m_moveSpeed * deltaTime;

	if (isForwardPressed) transform.position += front * velocity;
	if (isBackPressed) transform.position -= front * velocity;
	if (isRightPressed) transform.position += right * velocity;
	if (isLeftPressed) transform.position -= right * velocity;
}
