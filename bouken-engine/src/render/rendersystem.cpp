#include "render/rendersystem.h"

#include <vk_mem_alloc.h>

#include "camerasystem.h"
#include "materialmanager.h"
#include "primitives.h"
#include "world.h"

static uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                               uint32_t typeFilter,
                               VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) &&
		    (memProperties.memoryTypes[i].propertyFlags & properties) ==
		        properties) {
			return i;
		}
	}

	throw std::runtime_error("Failed to find suitable memory type!");
}

void RenderSystem::initialize(VulkanContext& context, SwapChain& swapChain,
                              LightSystem& lightSystem) {
	m_context = &context;
	m_swapChain = &swapChain;
	m_swapChainFormat = swapChain.getImageFormat();

	createDepthResources();
	createGBufferTargets();
	createHDRTarget();
	createSamplers();
	createDescriptorSetLayouts();
	createDescriptorPool();
	createFrameUBOs();
	createFrameDescriptorSets();
	createLightingDescriptorSet(lightSystem);
	createTonemapDescriptorSet();
	createDepthPrepass();
	createGeometryPass();
	createGBufferFramebuffer();
	createLightingPass();
	createTonemapPass();
	swapChain.createFramebuffers(m_tonemap.renderPass);
	createMeshBuffers();
	createCommandBuffer();
	createSyncObjects();
}

void RenderSystem::cleanup() {
	// Sync objects
	vkDestroySemaphore(m_context->getDevice(), m_imageAvailableSemaphore,
	                   nullptr);
	vkDestroySemaphore(m_context->getDevice(), m_renderFinishedSemaphore,
	                   nullptr);
	vkDestroyFence(m_context->getDevice(), m_inFlightFence, nullptr);

	// Mesh buffers
	vmaDestroyBuffer(m_context->getAllocator(), m_indexBuffer,
	                 m_indexBufferAllocation);
	vmaDestroyBuffer(m_context->getAllocator(), m_vertexBuffer,
	                 m_vertexBufferAllocation);

	// Material UBOs
	for (size_t i = 0; i < m_materialUBOs.size(); i++) {
		vmaDestroyBuffer(m_context->getAllocator(), m_materialUBOs[i],
		                 m_materialUBOAllocations[i]);
	}

	// Frame UBOs
	for (size_t i = 0; i < m_frameUBOs.size(); i++) {
		vmaDestroyBuffer(m_context->getAllocator(), m_frameUBOs[i],
		                 m_frameUBOAllocations[i]);
	}

	// Descriptor pool (implicitly frees all sets)
	vkDestroyDescriptorPool(m_context->getDevice(), m_descriptorPool, nullptr);

	// Samplers
	vkDestroySampler(m_context->getDevice(), m_gbufferSampler, nullptr);

	// Pipelines and layouts
	vkDestroyPipeline(m_context->getDevice(), m_depthPrepass.pipeline, nullptr);
	vkDestroyPipelineLayout(m_context->getDevice(), m_depthPrepass.layout,
	                        nullptr);
	vkDestroyRenderPass(m_context->getDevice(), m_depthPrepass.renderPass,
	                    nullptr);
	vkDestroyFramebuffer(m_context->getDevice(), m_depthPrepass.framebuffer,
	                     nullptr);

	vkDestroyPipeline(m_context->getDevice(), m_geometry.pipeline, nullptr);
	vkDestroyPipelineLayout(m_context->getDevice(), m_geometry.layout, nullptr);
	vkDestroyRenderPass(m_context->getDevice(), m_geometry.renderPass, nullptr);
	vkDestroyFramebuffer(m_context->getDevice(), m_gbuffer.framebuffer,
	                     nullptr);

	vkDestroyPipeline(m_context->getDevice(), m_lighting.pipeline, nullptr);
	vkDestroyPipelineLayout(m_context->getDevice(), m_lighting.layout, nullptr);
	vkDestroyRenderPass(m_context->getDevice(), m_lighting.renderPass, nullptr);
	vkDestroyFramebuffer(m_context->getDevice(), m_hdr.framebuffer, nullptr);

	vkDestroyPipeline(m_context->getDevice(), m_tonemap.pipeline, nullptr);
	vkDestroyPipelineLayout(m_context->getDevice(), m_tonemap.layout, nullptr);
	vkDestroyRenderPass(m_context->getDevice(), m_tonemap.renderPass, nullptr);

	// Descriptor set layouts
	vkDestroyDescriptorSetLayout(m_context->getDevice(), m_frameSetLayout,
	                             nullptr);
	vkDestroyDescriptorSetLayout(m_context->getDevice(), m_lightingSetLayout,
	                             nullptr);
	vkDestroyDescriptorSetLayout(m_context->getDevice(), m_tonemapSetLayout,
	                             nullptr);
	vkDestroyDescriptorSetLayout(m_context->getDevice(), m_materialSetLayout,
	                             nullptr);
	vkDestroyDescriptorSetLayout(m_context->getDevice(), m_objectSetLayout,
	                             nullptr);

	// Render targets
	m_depth.target.destroy(m_context->getDevice(), m_context->getAllocator());
	m_gbuffer.baseColorMetallic.destroy(m_context->getDevice(),
	                                    m_context->getAllocator());
	m_gbuffer.normals.destroy(m_context->getDevice(),
	                          m_context->getAllocator());
	m_gbuffer.roughnessAOSpecID.destroy(m_context->getDevice(),
	                                    m_context->getAllocator());
	m_gbuffer.emissiveFlags.destroy(m_context->getDevice(),
	                                m_context->getAllocator());
	m_hdr.target.destroy(m_context->getDevice(), m_context->getAllocator());
}

void RenderSystem::drawFrame(SwapChain& swapChain, World& world,
                             const CameraSystem& cameraSystem,
                             MaterialManager& materialManager) {
	vkWaitForFences(m_context->getDevice(), 1, &m_inFlightFence, VK_TRUE,
	                UINT64_MAX);
	vkResetFences(m_context->getDevice(), 1, &m_inFlightFence);

	uint32_t imageIndex;
	vkAcquireNextImageKHR(m_context->getDevice(), swapChain.getSwapChain(),
	                      UINT64_MAX, m_imageAvailableSemaphore, VK_NULL_HANDLE,
	                      &imageIndex);

	m_currentImageIndex = imageIndex;

	// Gather and sort render items once per frame
	gatherRenderItems(world, cameraSystem, materialManager);

	vkResetCommandBuffer(m_commandBuffer, 0);
	recordCommandBuffer(m_commandBuffer, imageIndex, swapChain, world,
	                    cameraSystem, materialManager);

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphore};
	VkPipelineStageFlags waitStages[] = {
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &m_commandBuffer;

	VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphore};
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;

	if (vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo,
	                  m_inFlightFence) != VK_SUCCESS) {
		throw std::runtime_error("Failed to submit draw command buffer!");
	}

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;

	VkSwapchainKHR swapChains[] = {swapChain.getSwapChain()};
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;

	vkQueuePresentKHR(m_context->getPresentQueue(), &presentInfo);
}

void RenderSystem::createMeshBuffers() {
	// Load built-in meshes

	// Mesh 0: Cube
	auto cubeGeom = Primitives::createCube(1.0f);
	m_meshes[m_nextMeshID++] = {
	    .firstVertex = static_cast<uint32_t>(m_allVertices.size()),
	    .vertexCount = static_cast<uint32_t>(cubeGeom.vertices.size()),
	    .firstIndex = static_cast<uint32_t>(m_allIndices.size()),
	    .indexCount = static_cast<uint32_t>(cubeGeom.indices.size())};
	m_allVertices.insert(m_allVertices.end(), cubeGeom.vertices.begin(),
	                     cubeGeom.vertices.end());
	m_allIndices.insert(m_allIndices.end(), cubeGeom.indices.begin(),
	                    cubeGeom.indices.end());

	// Mesh 1: Sphere
	auto sphereGeom = Primitives::createSphere(1.0f, 16, 16);
	m_meshes[m_nextMeshID++] = {
	    .firstVertex = static_cast<uint32_t>(m_allVertices.size()),
	    .vertexCount = static_cast<uint32_t>(sphereGeom.vertices.size()),
	    .firstIndex = static_cast<uint32_t>(m_allIndices.size()),
	    .indexCount = static_cast<uint32_t>(sphereGeom.indices.size())};
	m_allVertices.insert(m_allVertices.end(), sphereGeom.vertices.begin(),
	                     sphereGeom.vertices.end());
	m_allIndices.insert(m_allIndices.end(), sphereGeom.indices.begin(),
	                    sphereGeom.indices.end());

	// Mesh 2: Cone
	auto coneGeom = Primitives::createCone(1.0f, 2.0f, 16);
	m_meshes[m_nextMeshID++] = {
	    .firstVertex = static_cast<uint32_t>(m_allVertices.size()),
	    .vertexCount = static_cast<uint32_t>(coneGeom.vertices.size()),
	    .firstIndex = static_cast<uint32_t>(m_allIndices.size()),
	    .indexCount = static_cast<uint32_t>(coneGeom.indices.size())};
	m_allVertices.insert(m_allVertices.end(), coneGeom.vertices.begin(),
	                     coneGeom.vertices.end());
	m_allIndices.insert(m_allIndices.end(), coneGeom.indices.begin(),
	                    coneGeom.indices.end());

	// Upload to GPU
	uploadMeshData(m_allVertices, m_allIndices);
}

void RenderSystem::createCommandBuffer() {
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = m_context->getCommandPool();
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	if (vkAllocateCommandBuffers(m_context->getDevice(), &allocInfo,
	                             &m_commandBuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to allocate command buffer!");
	}
}

void RenderSystem::createSyncObjects() {
	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	if (vkCreateSemaphore(m_context->getDevice(), &semaphoreInfo, nullptr,
	                      &m_imageAvailableSemaphore) != VK_SUCCESS ||
	    vkCreateSemaphore(m_context->getDevice(), &semaphoreInfo, nullptr,
	                      &m_renderFinishedSemaphore) != VK_SUCCESS ||
	    vkCreateFence(m_context->getDevice(), &fenceInfo, nullptr,
	                  &m_inFlightFence) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create synchronization objects!");
	}
}

void RenderSystem::gatherRenderItems(World& world,
                                     const CameraSystem& cameraSystem,
                                     MaterialManager& materialManager) {
	m_renderItems.clear();

	const glm::mat4 view = cameraSystem.getViewMatrix(world);
	const glm::vec3 viewDir = glm::vec3(
	    view[0][2], view[1][2], view[2][2]);  // third row = forward vector

	for (Entity entity :
	     world.view<Transform, MeshRenderer, MaterialBinding>()) {
		const Transform& transform = world.getComponent<Transform>(entity);
		const MeshRenderer& meshRenderer =
		    world.getComponent<MeshRenderer>(entity);
		const MaterialBinding& materialBinding =
		    world.getComponent<MaterialBinding>(entity);

		// Skip cutout materials entirely until transparency is implemented
		const Material& material =
		    materialManager.getMaterial(materialBinding.materialID);
		if (material.opacity < 1.0f) continue;

		if (!meshRenderer.visible) continue;

		VkDescriptorSet descriptorSet =
		    materialManager.getDescriptorSet(materialBinding.materialID);

		for (uint32_t meshID : meshRenderer.getMeshIDs()) {
			auto it = m_meshes.find(meshID);
			if (it == m_meshes.end()) continue;

			// not actual depth, just relative for sorting
			const glm::vec3 worldPos = glm::vec3(transform.worldMatrix[3]);
			const float relativeDepth = glm::dot(viewDir, worldPos);

			m_renderItems.push_back({entity, meshID, descriptorSet,
			                         transform.worldMatrix, relativeDepth});
		}
	}

	// Front-to-back — smallest depth value first
	std::sort(m_renderItems.begin(), m_renderItems.end(),
	          [](const RenderItem& a, const RenderItem& b) {
		          return a.depth < b.depth;
	          });
}

VkShaderModule RenderSystem::createShaderModule(const std::vector<char>& code) {
	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(m_context->getDevice(), &createInfo, nullptr,
	                         &shaderModule) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create shader module!");
	}

	return shaderModule;
}