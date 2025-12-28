#pragma once
#include "pch.h"
#include "swapchain.h"
#include "vulkancontext.h"

class RenderSystem {
   public:
	void initialize(VulkanContext& context, SwapChain& swapChain);
	void cleanup();

	void drawFrame(SwapChain& swapChain);

   private:
	void createRenderPass();
	void createGraphicsPipeline();
	void createCommandBuffer();
	void createSyncObjects();

	void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex,
	                         SwapChain& swapChain);

	VkShaderModule createShaderModule(const std::vector<char>& code);

	// Reference to context (doesn't own it)
	VulkanContext* m_context = nullptr;

	// Rendering pipeline
	VkRenderPass m_renderPass = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;
	// TODO: Add multiple pipelines (wireframe, transparent, etc.)
	// TODO: Add depth/stencil state when implementing depth buffer

	// Command buffers
	VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
	// TODO: Multiple command buffers for frames in flight

	// Synchronization
	VkSemaphore m_imageAvailableSemaphore = VK_NULL_HANDLE;
	VkSemaphore m_renderFinishedSemaphore = VK_NULL_HANDLE;
	VkFence m_inFlightFence = VK_NULL_HANDLE;
	// TODO: Multiple frames in flight (2-3 sets of sync objects)
};