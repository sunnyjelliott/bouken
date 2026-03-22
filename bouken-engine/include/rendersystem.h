#pragma once
#include "camera.h"
#include "entity.h"
#include "pch.h"
#include "swapchain.h"
#include "vertex.h"
#include "vulkancontext.h"

typedef struct VmaAllocation_T* VmaAllocation;

class World;
class CameraSystem;
class MaterialManager;
class TextureManager;

class RenderSystem {
   public:
	void initialize(VulkanContext& context, SwapChain& swapChain);
	void cleanup();

	uint32_t loadMesh(const std::string& filepath);

	void drawFrame(SwapChain& swapChain, World& world,
	               const CameraSystem& cameraSystem,
	               MaterialManager& materialManager);

	uint32_t uploadMesh(const std::vector<Vertex>& vertices,
	                    const std::vector<uint32_t>& indices);

	void createMaterialDescriptorSets(MaterialManager& materialManager,
	                                  TextureManager& textureManager);

   private:
	void createRenderPass();
	void createGraphicsPipeline();
	void createMeshBuffers();
	void createCommandBuffer();
	void createSyncObjects();
	void createDescriptorSetLayout();
	void createDescriptorPool();

	void uploadMeshData(const std::vector<Vertex>& vertices,
	                    const std::vector<uint32_t>& indices);

	void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex,
	                         SwapChain& swapChain, World& world,
	                         const CameraSystem& cameraSystem,
	                         MaterialManager& materialManager);

	VkShaderModule createShaderModule(const std::vector<char>& code);

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
		uint32_t firstIndex;
		uint32_t indexCount;
	};
	std::unordered_map<uint32_t, MeshInfo> m_meshes;
	uint32_t m_nextMeshID = 0;

	std::vector<Vertex> m_allVertices;
	std::vector<uint32_t> m_allIndices;

	VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
	VmaAllocation m_vertexBufferAllocation = VK_NULL_HANDLE;
	VkBuffer m_indexBuffer = VK_NULL_HANDLE;
	VmaAllocation m_indexBufferAllocation = VK_NULL_HANDLE;

	// Command buffers
	VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
	// TODO: Multiple command buffers for frames in flight

	// Synchronization
	VkSemaphore m_imageAvailableSemaphore = VK_NULL_HANDLE;
	VkSemaphore m_renderFinishedSemaphore = VK_NULL_HANDLE;
	VkFence m_inFlightFence = VK_NULL_HANDLE;
	// TODO: Multiple frames in flight (2-3 sets of sync objects)

	// Descriptor Sets (Textures)
	VkDescriptorSetLayout m_materialDescriptorSetLayout = VK_NULL_HANDLE;
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
};