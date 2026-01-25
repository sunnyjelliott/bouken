#include "glfwinputbackend.h"

GLFWInputBackend* GLFWInputBackend::s_instance = nullptr;

GLFWInputBackend::GLFWInputBackend(GLFWwindow* window) : m_window(window) {
	s_instance = this;
	glfwSetCursorPosCallback(window, mouseCallback);
}

bool GLFWInputBackend::isKeyDown(int key) const {
	return glfwGetKey(m_window, key) == GLFW_PRESS;
}

bool GLFWInputBackend::isMouseButtonDown(int button) const {
	return glfwGetMouseButton(m_window, button) == GLFW_PRESS;
}

float GLFWInputBackend::getMouseDeltaX() const { return m_mouseDeltaX; }

float GLFWInputBackend::getMouseDeltaY() const { return m_mouseDeltaY; }

void GLFWInputBackend::setMouseCaptured(bool captured) {
	m_mouseCaptured = captured;
	if (captured) {
		glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
		m_firstMouse = true;
	} else {
		glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
	}
}

bool GLFWInputBackend::isMouseCaptured() const { return m_mouseCaptured; }

void GLFWInputBackend::update() {
	m_mouseDeltaX = 0.0f;
	m_mouseDeltaY = 0.0f;
}

void GLFWInputBackend::mouseCallback(GLFWwindow* window, double xpos,
                                     double ypos) {
	if (!s_instance || !s_instance->m_mouseCaptured) return;

	if (s_instance->m_firstMouse) {
		s_instance->m_lastMouseX = xpos;
		s_instance->m_lastMouseY = ypos;
		s_instance->m_firstMouse = false;
	}

	s_instance->m_mouseDeltaX =
	    static_cast<float>(s_instance->m_lastMouseX - xpos);
	s_instance->m_mouseDeltaY =
	    static_cast<float>(s_instance->m_lastMouseY - ypos);

	s_instance->m_lastMouseX = xpos;
	s_instance->m_lastMouseY = ypos;
}
