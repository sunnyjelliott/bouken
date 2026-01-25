#include "app.h"

#include <vk_mem_alloc.h>

#include "input.h"

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

	m_inputBackend = new GLFWInputBackend(m_window);

	Input::initialize(m_inputBackend);
	Input::setMouseCaptured(true);
}

void Application::initVulkan() {
	m_context.init(m_window);
	m_swapChain.init(m_context, m_window, WIDTH, HEIGHT);
	m_renderSystem.initialize(m_context, m_swapChain);
}

void Application::initScene() {
	// Create camera entity
	m_activeCamera = m_world.createEntity();
	m_world.addComponent(m_activeCamera,
	                     Transform{.position = glm::vec3(0.0f, 2.0f, 8.0f)});
	m_world.addComponent(
	    m_activeCamera,
	    Camera{.fov = 45.0f, .nearPlane = 0.1f, .farPlane = 100.0f});
	m_cameraSystem.setActiveCamera(m_activeCamera);

	// Load a mesh from file (try this after creating the file)
	uint32_t loadedMeshID =
	    m_renderSystem.loadMesh("assets/models/viking_room.obj");

	// Parent cube (built-in mesh 0)
	Entity parent = m_world.createEntity();
	m_world.addComponent(parent,
	                     Transform{.position = glm::vec3(0.0f, 0.0f, 0.0f),
	                               .scale = glm::vec3(1.5f)});
	m_world.addComponent(parent, MeshRenderer{.meshID = 0, .visible = true});

	// Child using loaded mesh
	Entity loadedEntity = m_world.createEntity();
	m_world.addComponent(loadedEntity,
	                     Transform{.position = glm::vec3(3.0f, 0.0f, 0.0f),
	                               .scale = glm::vec3(2.0f)});
	m_world.addComponent(
	    loadedEntity,
	    MeshRenderer{.meshID = loadedMeshID,  // Use the loaded mesh
	                 .visible = true});

	std::cout << "Created " << m_world.getEntityCount() << " entities\n";
}

void Application::mainLoop() {
	Entity parent = 0;  // First entity created
	float rotation = 0.0f;

	while (!glfwWindowShouldClose(m_window)) {
		float currentTime = static_cast<float>(glfwGetTime());
		float deltaTime = currentTime - m_lastFrameTime;
		m_lastFrameTime = currentTime;

		glfwPollEvents();

		if (Input::isActionPressed(InputAction::ToggleMouseCapture)) {
			Input::setMouseCaptured(!Input::isMouseCaptured());
		}

		m_cameraSystem.updateFreeFly(
		    m_world, deltaTime, Input::isActionHeld(InputAction::MoveForward),
		    Input::isActionHeld(InputAction::MoveBackward),
		    Input::isActionHeld(InputAction::MoveLeft),
		    Input::isActionHeld(InputAction::MoveRight),
		    Input::getActionValue(InputAction::LookHorizontal),
		    Input::getActionValue(InputAction::LookVertical));

		Input::update();

		// Update transforms
		m_transformSystem.update(m_world);

		// Render
		m_renderSystem.drawFrame(m_swapChain, m_world, m_cameraSystem);
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