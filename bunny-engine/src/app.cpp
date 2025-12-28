#include "app.h"

void Application::run() {
	initWindow();
	initVulkan();
	mainLoop();
	cleanup();
}

void Application::initWindow() {
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(
	    GLFW_RESIZABLE,
	    GLFW_FALSE);  // TODO: Re-enable when window resizing is handled.

	m_window =
	    glfwCreateWindow(WIDTH, HEIGHT, "Bunny Engine", nullptr, nullptr);
}

void Application::initVulkan() {
	m_context.init(m_window);
	m_swapChain.init(m_context, m_window, WIDTH, HEIGHT);
	m_renderSystem.initialize(m_context, m_swapChain);
}

void Application::mainLoop() {
	while (!glfwWindowShouldClose(m_window)) {
		glfwPollEvents();
		m_renderSystem.drawFrame(m_swapChain);
	}

	vkDeviceWaitIdle(m_context.getDevice());
}

void Application::cleanup() {
	m_renderSystem.cleanup();
	m_swapChain.cleanup();
	m_context.cleanup();

	glfwDestroyWindow(m_window);
	glfwTerminate();
}