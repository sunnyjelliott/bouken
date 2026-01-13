#pragma once
#include "camera.h"
#include "entity.h"
#include "pch.h"
#include "swapchain.h"
#include "vertex.h"
#include "vulkancontext.h"

class World;

class RenderSystem {
   public:
	void initialize(VulkanContext& context, SwapChain& swapChain);
	void cleanup();

	void drawFrame(SwapChain& swapChain, World& world, const Camera& camera);

   private:
	void createRenderPass();
	void createGraphicsPipeline();
	void createVertexBuffer();
	void createCommandBuffer();
	void createSyncObjects();

	void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex,
	                         SwapChain& swapChain, World& world,
	                         const Camera& camera);

	VkShaderModule createShaderModule(const std::vector<char>& code);

	std::vector<Vertex> createCubeVertices();
	std::vector<Vertex> createPyramidVertices();

	VulkanContext* m_context = nullptr;

	// Rendering pipeline
	VkRenderPass m_renderPass = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_graphicsPipeline = VK_NULL_HANDLE;
	VkFormat m_swapChainFormat;
	// TODO: Add multiple pipelines (wireframe, transparent, etc.)
	// TODO: Add depth/stencil state when implementing depth buffer

	struct MeshInfo {
		uint32_t firstVertex;
		uint32_t vertexCount;
	};
	std::unordered_map<uint32_t, MeshInfo> m_meshes;

	VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
	VkDeviceMemory m_vertexBufferMemory = VK_NULL_HANDLE;

	// Command buffers
	VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
	// TODO: Multiple command buffers for frames in flight

	// Synchronization
	VkSemaphore m_imageAvailableSemaphore = VK_NULL_HANDLE;
	VkSemaphore m_renderFinishedSemaphore = VK_NULL_HANDLE;
	VkFence m_inFlightFence = VK_NULL_HANDLE;
	// TODO: Multiple frames in flight (2-3 sets of sync objects)
};