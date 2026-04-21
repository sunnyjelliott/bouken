#pragma once
#include "camera.h"
#include "entity.h"
#include "gpu/devicebuffer.h"
#include "pch.h"
#include "render/rendertarget.h"
#include "spatial.h"
#include "swapchain.h"
#include "vertex.h"
#include "vulkancontext.h"

typedef struct VmaAllocation_T* VmaAllocation;

class World;
class CameraSystem;
class MaterialManager;
class TextureManager;
class LightSystem;

struct RenderItem {
	Entity entity;
	uint32_t meshID;
	VkDescriptorSet descriptorSet;
	glm::mat4 modelMatrix;
	float depth;
};

class RenderSystem {
   public:
	void initialize(VulkanContext& context, SwapChain& swapChain,
	                LightSystem& lightSystem);
	void cleanup();

	uint32_t loadMesh(const std::string& filepath);

	void drawFrame(SwapChain& swapChain, World& world,
	               const CameraSystem& cameraSystem,
	               MaterialManager& materialManager);

	uint32_t uploadMesh(const std::vector<Vertex>& vertices,
	                    const std::vector<uint32_t>& indices);

	void flushMeshUploads();

	AABB getMeshAABB(uint32_t meshID) const;

	void createMaterialDescriptorSets(MaterialManager& materialManager,
	                                  TextureManager& textureManager);

   private:
	void createDepthResources();
	void createGBufferTargets();
	void createHDRTarget();
	void createSamplers();
	void createDescriptorSetLayouts();
	void createDescriptorPool();
	void createFrameUBOs();
	void createFrameDescriptorSets();
	void createLightingDescriptorSet(LightSystem& lightSystem);
	void createTonemapDescriptorSet();
	void createDepthPrepass();
	void createGeometryPass();
	void createGBufferFramebuffer();
	void createLightingPass();
	void createTonemapPass();
	void createMeshBuffers();
	void createCommandBuffer();
	void createSyncObjects();

	void uploadMeshData(const std::vector<Vertex>& vertices,
	                    const std::vector<uint32_t>& indices);

	void gatherRenderItems(World& world, const CameraSystem& cameraSystem,
	                       MaterialManager& materialManager);

	void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex,
	                         SwapChain& swapChain, World& world,
	                         const CameraSystem& cameraSystem,
	                         MaterialManager& materialManager);

	void updateFrameUBO(uint32_t imageIndex, SwapChain& swapChain,
	                    const glm::mat4& view, const glm::mat4& projection,
	                    const glm::vec3& cameraPos);

	void recordDepthPrepass(VkCommandBuffer commandBuffer,
	                        const glm::mat4& view, const glm::mat4& projection,
	                        VkExtent2D extent);

	void recordGeometryPass(VkCommandBuffer commandBuffer,
	                        const glm::mat4& view, const glm::mat4& projection,
	                        VkExtent2D extent,
	                        MaterialManager& materialManager);

	void recordLightingPass(VkCommandBuffer commandBuffer);
	void recordTonemapPass(VkCommandBuffer commandBuffer, uint32_t imageIndex,
	                       SwapChain& swapChain);

	VkShaderModule createShaderModule(const std::vector<char>& code);

	VulkanContext* m_context = nullptr;
	SwapChain* m_swapChain = nullptr;
	uint32_t m_currentImageIndex = 0;

	struct DepthResources {
		RenderTarget target;
		VkFormat format = VK_FORMAT_UNDEFINED;
	} m_depth;

	struct GBuffer {
		RenderTarget baseColorMetallic;  // RGBA8_UNORM
		RenderTarget normals;            // RG16_SNORM
		RenderTarget roughnessAOSpecID;  // RGBA8_UNORM
		RenderTarget emissiveFlags;      // RGBA16F

		VkFramebuffer framebuffer = VK_NULL_HANDLE;
		VkRenderPass renderPass = VK_NULL_HANDLE;
	} m_gbuffer;

	struct HDRTarget {
		RenderTarget target;  // RGBA16F
		VkFramebuffer framebuffer = VK_NULL_HANDLE;
		VkRenderPass renderPass = VK_NULL_HANDLE;
	} m_hdr;

	struct DepthPrepass {
		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkPipelineLayout layout = VK_NULL_HANDLE;
		VkFramebuffer framebuffer = VK_NULL_HANDLE;
	} m_depthPrepass;

	struct LightingPass {
		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkPipelineLayout layout = VK_NULL_HANDLE;
		VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	} m_lighting;

	struct TonemapPass {
		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkPipelineLayout layout = VK_NULL_HANDLE;
		VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
		VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
	} m_tonemap;

	struct GeometryPass {
		VkRenderPass renderPass = VK_NULL_HANDLE;
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkPipelineLayout layout = VK_NULL_HANDLE;
	} m_geometry;

	std::vector<RenderItem> m_renderItems;

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
	std::unordered_map<uint32_t, AABB> m_meshAABBs;
	bool m_meshBufferDirty = false;
	uint32_t m_uploadedVertexCount = 0;
	uint32_t m_uploadedIndexCount = 0;

	DeviceBuffer m_vertexBuffer;
	DeviceBuffer m_indexBuffer;

	// Initial GPU buffer reservation.
	// TODO: Select based on VkPhysicalDeviceLimits or scene budget estimate.
	static constexpr VkDeviceSize VERTEX_BUFFER_INITIAL_CAPACITY =
	    32 * 1024 * 1024;  // 32 MB
	static constexpr VkDeviceSize INDEX_BUFFER_INITIAL_CAPACITY =
	    16 * 1024 * 1024;  // 16 MB

	VkSampler m_gbufferSampler = VK_NULL_HANDLE;

	// Command buffers
	VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
	// TODO: Multiple command buffers for frames in flight

	// Synchronization
	VkSemaphore m_imageAvailableSemaphore = VK_NULL_HANDLE;
	VkSemaphore m_renderFinishedSemaphore = VK_NULL_HANDLE;
	VkFence m_inFlightFence = VK_NULL_HANDLE;
	// TODO: Multiple frames in flight (2-3 sets of sync objects)

	// Descriptor set layouts
	VkDescriptorSetLayout m_frameSetLayout = VK_NULL_HANDLE;     // set 0
	VkDescriptorSetLayout m_lightingSetLayout = VK_NULL_HANDLE;  // set 1a
	VkDescriptorSetLayout m_tonemapSetLayout = VK_NULL_HANDLE;   // set 1b
	VkDescriptorSetLayout m_materialSetLayout = VK_NULL_HANDLE;  // set 2
	VkDescriptorSetLayout m_objectSetLayout = VK_NULL_HANDLE;    // set 3 stub

	// Descriptor pool
	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

	// Frame sets (one per swapchain image)
	std::vector<VkDescriptorSet> m_frameSets;
	std::vector<VkBuffer> m_frameUBOs;
	std::vector<VmaAllocation> m_frameUBOAllocations;
	std::vector<void*> m_frameUBOMapped;  // persistently mapped

	// Material UBOs
	std::vector<VkBuffer> m_materialUBOs;
	std::vector<VmaAllocation> m_materialUBOAllocations;

	// Pass sets (single instances)
	VkDescriptorSet m_lightingSet = VK_NULL_HANDLE;
	VkDescriptorSet m_tonemapSet = VK_NULL_HANDLE;
};