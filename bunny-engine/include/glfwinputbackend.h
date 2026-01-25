#pragma once

#include "iinputbackend.h"
#include "pch.h"

class GLFWInputBackend : public IInputBackend {
   public:
	GLFWInputBackend(GLFWwindow* window);
	~GLFWInputBackend() override = default;

	bool isKeyDown(int key) const override;
	bool isMouseButtonDown(int button) const override;

	float getMouseDeltaX() const override;
	float getMouseDeltaY() const override;
	void setMouseCaptured(bool captured) override;
	bool isMouseCaptured() const override;

	void update() override;

   private:
	static void mouseCallback(GLFWwindow* window, double xpos, double ypos);

	GLFWwindow* m_window;

	bool m_firstMouse = true;
	double m_lastMouseX = 0.0;
	double m_lastMouseY = 0.0;
	float m_mouseDeltaX = 0.0f;
	float m_mouseDeltaY = 0.0f;
	bool m_mouseCaptured = false;

	static GLFWInputBackend* s_instance;
};