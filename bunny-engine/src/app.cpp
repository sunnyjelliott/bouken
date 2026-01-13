#include "app.h"

void Application::run() {
	initWindow();
	initVulkan();
	initScene();
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

void Application::initScene() {
	// Setup camera
	m_camera.position = glm::vec3(0.0f, 2.0f, 5.0f);
	m_camera.front = glm::vec3(0.0f, -0.3f, -1.0f);

	// Create cube entities
	Entity cube1 = m_world.createEntity();
	m_world.addComponent(
	    cube1, Transform{.position = glm::vec3(-2.0f, 0.0f, 0.0f),
	                     .rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
	                     .scale = glm::vec3(1.0f)});
	m_world.addComponent(cube1, MeshRenderer{.meshID = 0, .visible = true});

	Entity cube2 = m_world.createEntity();
	m_world.addComponent(
	    cube2, Transform{.position = glm::vec3(2.0f, 0.0f, 0.0f),
	                     .rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
	                     .scale = glm::vec3(0.8f)});
	m_world.addComponent(cube2, MeshRenderer{.meshID = 0, .visible = true});

	// Create pyramid entities
	Entity pyramid1 = m_world.createEntity();
	m_world.addComponent(
	    pyramid1, Transform{.position = glm::vec3(0.0f, 1.5f, -2.0f),
	                        .rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
	                        .scale = glm::vec3(1.0f)});
	m_world.addComponent(pyramid1, MeshRenderer{.meshID = 1, .visible = true});

	std::cout << "Created " << m_world.getEntityCount() << " entities\n";
}

void Application::mainLoop() {
	while (!glfwWindowShouldClose(m_window)) {
		glfwPollEvents();
		m_renderSystem.drawFrame(m_swapChain, m_world, m_camera);
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