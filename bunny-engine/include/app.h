#pragma once
#include "camerasystem.h"
#include "glfwinputbackend.h"
#include "pch.h"
#include "rendersystem.h"
#include "swapchain.h"
#include "transformsystem.h"
#include "vulkancontext.h"
#include "world.h"

class Application {
   public:
	void run();

   private:
	void initWindow();
	void initVulkan();
	void initScene();
	void mainLoop();
	void cleanup();

	GLFWwindow* m_window = nullptr;
	const uint32_t WIDTH = 1200;
	const uint32_t HEIGHT = 800;

	VulkanContext m_context;
	SwapChain m_swapChain;
	RenderSystem m_renderSystem;
	TransformSystem m_transformSystem;
	World m_world;
	CameraSystem m_cameraSystem;
	Entity m_activeCamera;
	GLFWInputBackend* m_inputBackend = nullptr;

	float m_lastFrameTime = 0.0f;
};