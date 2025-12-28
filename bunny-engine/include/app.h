#pragma once
#include "pch.h"
#include "rendersystem.h"
#include "swapchain.h"
#include "vulkancontext.h"

class Application {
   public:
	void run();

   private:
	void initWindow();
	void initVulkan();
	void mainLoop();
	void cleanup();

	GLFWwindow* m_window = nullptr;
	const uint32_t WIDTH = 1200;
	const uint32_t HEIGHT = 800;

	VulkanContext m_context;
	SwapChain m_swapChain;
	RenderSystem m_renderSystem;
};