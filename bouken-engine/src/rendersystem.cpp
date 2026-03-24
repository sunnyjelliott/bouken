#include "rendersystem.h"

#include <vk_mem_alloc.h>

#include "camerasystem.h"
#include "materialmanager.h"
#include "meshloader.h"
#include "primitives.h"
#include "texturemanager.h"
#include "vulkantexturebackend.h"
#include "world.h"

static std::vector<char> readFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("Failed to open file: " + filename);
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	return buffer;
}

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

void RenderSystem::initialize(VulkanContext& context, SwapChain& swapChain) {
	m_context = &context;
	m_swapChainFormat = swapChain.getImageFormat();

	createRenderPass();
	createDescriptorSetLayout();
	createGraphicsPipeline();
	swapChain.createFramebuffers(m_renderPass);
	createMeshBuffers();
	createCommandBuffer();
	createSyncObjects();
	createDescriptorPool();
}

void RenderSystem::cleanup() {
	vkDestroySemaphore(m_context->getDevice(), m_imageAvailableSemaphore,
	                   nullptr);
	vkDestroySemaphore(m_context->getDevice(), m_renderFinishedSemaphore,
	                   nullptr);
	vkDestroyFence(m_context->getDevice(), m_inFlightFence, nullptr);

	vmaDestroyBuffer(m_context->getAllocator(), m_indexBuffer,
	                 m_indexBufferAllocation);
	vmaDestroyBuffer(m_context->getAllocator(), m_vertexBuffer,
	                 m_vertexBufferAllocation);

	vkDestroyDescriptorPool(m_context->getDevice(), m_descriptorPool, nullptr);
	vkDestroyPipeline(m_context->getDevice(), m_graphicsPipeline, nullptr);
	vkDestroyPipelineLayout(m_context->getDevice(), m_pipelineLayout, nullptr);
	vkDestroyDescriptorSetLayout(m_context->getDevice(),
	                             m_materialDescriptorSetLayout, nullptr);
	vkDestroyRenderPass(m_context->getDevice(), m_renderPass, nullptr);
}

uint32_t RenderSystem::loadMesh(const std::string& filepath) {
	LoadedMesh loadedMesh = MeshLoader::loadOBJ(filepath);

	uint32_t meshID = m_nextMeshID++;

	m_meshes[meshID] = {
	    .firstVertex = static_cast<uint32_t>(m_allVertices.size()),
	    .vertexCount = static_cast<uint32_t>(loadedMesh.vertices.size()),
	    .firstIndex = static_cast<uint32_t>(m_allIndices.size()),
	    .indexCount = static_cast<uint32_t>(loadedMesh.indices.size())};

	m_allVertices.insert(m_allVertices.end(), loadedMesh.vertices.begin(),
	                     loadedMesh.vertices.end());
	m_allIndices.insert(m_allIndices.end(), loadedMesh.indices.begin(),
	                    loadedMesh.indices.end());

	// Re-upload all mesh data
	uploadMeshData(m_allVertices, m_allIndices);

	return meshID;
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

uint32_t RenderSystem::uploadMesh(const std::vector<Vertex>& vertices,
                                  const std::vector<uint32_t>& indices) {
	if (vertices.empty() || indices.empty()) {
		std::cerr << "Cannot upload empty mesh" << std::endl;
		return 0;  // Return default cube
	}

	uint32_t meshID = m_nextMeshID++;

	// Store mesh metadata
	m_meshes[meshID] = {
	    .firstVertex = static_cast<uint32_t>(m_allVertices.size()),
	    .vertexCount = static_cast<uint32_t>(vertices.size()),
	    .firstIndex = static_cast<uint32_t>(m_allIndices.size()),
	    .indexCount = static_cast<uint32_t>(indices.size())};

	// Append to global vertex/index arrays
	m_allVertices.insert(m_allVertices.end(), vertices.begin(), vertices.end());
	m_allIndices.insert(m_allIndices.end(), indices.begin(), indices.end());

	return meshID;
}

void RenderSystem::flushMeshUploads() {
	if (!m_allVertices.empty() && !m_allIndices.empty()) {
		uploadMeshData(m_allVertices, m_allIndices);
	}
}

void RenderSystem::createMaterialDescriptorSets(
    MaterialManager& materialManager, TextureManager& textureManager) {
	std::cout << "Creating descriptor sets for "
	          << materialManager.getMaterialCount() << " materials..."
	          << std::endl;

	uint32_t defaultWhiteID = textureManager.getDefaultWhiteTextureID();
	uint32_t defaultNormalID = textureManager.getDefaultNormalTextureID();

	// Iterate through all materials
	for (uint32_t matID = 0; matID <= 200; ++matID) {
		if (!materialManager.hasMaterial(matID)) {
			continue;
		}

		const Material& material = materialManager.getMaterial(matID);

		// Allocate descriptor set
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_materialDescriptorSetLayout;

		VkDescriptorSet descriptorSet;
		if (vkAllocateDescriptorSets(m_context->getDevice(), &allocInfo,
		                             &descriptorSet) != VK_SUCCESS) {
			throw std::runtime_error("Failed to allocate descriptor set!");
		}

		// Use material textures if available, otherwise use defaults
		uint32_t albedoID = (material.albedoTextureID != 0)
		                        ? material.albedoTextureID
		                        : defaultWhiteID;
		uint32_t normalID = (material.normalTextureID != 0)
		                        ? material.normalTextureID
		                        : defaultNormalID;

		// Get texture binding data
		void* albedoBindingData = textureManager.getBindingData(albedoID);
		void* normalBindingData = textureManager.getBindingData(normalID);

		VulkanTextureBindingData* albedoData =
		    static_cast<VulkanTextureBindingData*>(albedoBindingData);
		VulkanTextureBindingData* normalData =
		    static_cast<VulkanTextureBindingData*>(normalBindingData);

		if (!albedoData || !normalData) {
			std::cerr << "Failed to get texture binding data for material "
			          << matID << std::endl;
			continue;
		}

		std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

		// Binding 0: Albedo texture
		VkDescriptorImageInfo albedoImageInfo{};
		albedoImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		albedoImageInfo.imageView = albedoData->imageView;
		albedoImageInfo.sampler = albedoData->sampler;

		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[0].dstSet = descriptorSet;
		descriptorWrites[0].dstBinding = 0;
		descriptorWrites[0].dstArrayElement = 0;
		descriptorWrites[0].descriptorType =
		    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrites[0].descriptorCount = 1;
		descriptorWrites[0].pImageInfo = &albedoImageInfo;

		// Binding 1: Normal texture
		VkDescriptorImageInfo normalImageInfo{};
		normalImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		normalImageInfo.imageView = normalData->imageView;
		normalImageInfo.sampler = normalData->sampler;

		descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[1].dstSet = descriptorSet;
		descriptorWrites[1].dstBinding = 1;
		descriptorWrites[1].dstArrayElement = 0;
		descriptorWrites[1].descriptorType =
		    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrites[1].descriptorCount = 1;
		descriptorWrites[1].pImageInfo = &normalImageInfo;

		// Update both descriptors
		vkUpdateDescriptorSets(m_context->getDevice(), 2,
		                       descriptorWrites.data(), 0, nullptr);

		// Store descriptor set in material
		materialManager.createDescriptorSet(matID, descriptorSet);
	}

	std::cout << "Created descriptor sets for materials" << std::endl;
}

void RenderSystem::createRenderPass() {
	// Color attachment
	VkAttachmentDescription colorAttachment{};
	colorAttachment.format = m_swapChainFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorAttachmentRef{};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	// Depth attachment
	VkAttachmentDescription depthAttachment{};
	depthAttachment.format = VK_FORMAT_D32_SFLOAT;
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout =
	    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthAttachmentRef{};
	depthAttachmentRef.attachment = 1;
	depthAttachmentRef.layout =
	    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	// Subpass
	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;
	subpass.pDepthStencilAttachment = &depthAttachmentRef;

	// Subpass dependency
	VkSubpassDependency dependency{};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
	                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
	                          VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
	                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

	// Create render pass with both attachments
	std::array<VkAttachmentDescription, 2> attachments = {colorAttachment,
	                                                      depthAttachment};
	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	if (vkCreateRenderPass(m_context->getDevice(), &renderPassInfo, nullptr,
	                       &m_renderPass) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create render pass!");
	}
}

void RenderSystem::createGraphicsPipeline() {
	auto vertShaderCode = readFile("shaders/shader_vert.spv");
	auto fragShaderCode = readFile("shaders/shader_frag.spv");

	VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
	VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

	VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
	vertShaderStageInfo.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = vertShaderModule;
	vertShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
	fragShaderStageInfo.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = fragShaderModule;
	fragShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo,
	                                                  fragShaderStageInfo};

	auto bindingDescription = Vertex::getBindingDescription();
	auto attributeDescriptions = Vertex::getAttributeDescriptions();

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
	vertexInputInfo.vertexAttributeDescriptionCount =
	    static_cast<uint32_t>(attributeDescriptions.size());
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	// TODO: Add wireframe pipeline variant
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.stencilTestEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	// TODO: MSAA support

	VkPipelineColorBlendAttachmentState colorBlendAttachment{};
	colorBlendAttachment.colorWriteMask =
	    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = VK_FALSE;
	// TODO: Add transparent pipeline with alpha blending

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;

	std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
	                                             VK_DYNAMIC_STATE_SCISSOR};

	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount =
	    static_cast<uint32_t>(dynamicStates.size());
	dynamicState.pDynamicStates = dynamicStates.data();

	VkPushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pushConstantRange.offset = 0;
	pushConstantRange.size =
	    sizeof(glm::mat4) + sizeof(glm::mat4) + sizeof(glm::mat4);

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &m_materialDescriptorSetLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	if (vkCreatePipelineLayout(m_context->getDevice(), &pipelineLayoutInfo,
	                           nullptr, &m_pipelineLayout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create pipeline layout!");
	}

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = m_pipelineLayout;
	pipelineInfo.renderPass = m_renderPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

	if (vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1,
	                              &pipelineInfo, nullptr,
	                              &m_graphicsPipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create graphics pipeline!");
	}

	vkDestroyShaderModule(m_context->getDevice(), fragShaderModule, nullptr);
	vkDestroyShaderModule(m_context->getDevice(), vertShaderModule, nullptr);
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

void RenderSystem::createDescriptorSetLayout() {
	// Binding 0: Albedo texture
	VkDescriptorSetLayoutBinding albedoBinding{};
	albedoBinding.binding = 0;
	albedoBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	albedoBinding.descriptorCount = 1;
	albedoBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	albedoBinding.pImmutableSamplers = nullptr;

	// Binding 1: Normal texture
	VkDescriptorSetLayoutBinding normalBinding{};
	normalBinding.binding = 1;
	normalBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	normalBinding.descriptorCount = 1;
	normalBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	normalBinding.pImmutableSamplers = nullptr;

	std::array<VkDescriptorSetLayoutBinding, 2> bindings = {albedoBinding,
	                                                        normalBinding};

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	layoutInfo.pBindings = bindings.data();

	if (vkCreateDescriptorSetLayout(m_context->getDevice(), &layoutInfo,
	                                nullptr, &m_materialDescriptorSetLayout) !=
	    VK_SUCCESS) {
		throw std::runtime_error("Failed to create descriptor set layout!");
	}

	std::cout << "Created descriptor set layout for materials" << std::endl;
}

void RenderSystem::createDescriptorPool() {
	// Pool size for combined image samplers
	// Allocate enough for all materials (estimate ~100 materials max for now)
	VkDescriptorPoolSize poolSize{};
	poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSize.descriptorCount = 200;  // 2 textures * 100 materials

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.maxSets = 100;  // Max 100 materials

	if (vkCreateDescriptorPool(m_context->getDevice(), &poolInfo, nullptr,
	                           &m_descriptorPool) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create descriptor pool!");
	}

	std::cout << "Created descriptor pool (max 100 material sets)" << std::endl;
}

void RenderSystem::uploadMeshData(const std::vector<Vertex>& vertices,
                                  const std::vector<uint32_t>& indices) {
	if (m_vertexBuffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(m_context->getAllocator(), m_vertexBuffer,
		                 m_vertexBufferAllocation);
	}
	if (m_indexBuffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(m_context->getAllocator(), m_indexBuffer,
		                 m_indexBufferAllocation);
	}

	VkDeviceSize vertexBufferSize = sizeof(Vertex) * vertices.size();
	VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();

	// === VERTEX BUFFER ===

	// Create staging buffer (CPU-visible)
	VkBuffer vertexStagingBuffer;
	VmaAllocation vertexStagingAllocation;

	VkBufferCreateInfo vertexStagingInfo{};
	vertexStagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexStagingInfo.size = vertexBufferSize;
	vertexStagingInfo.usage =
	    VK_BUFFER_USAGE_TRANSFER_SRC_BIT;  // Source for transfer
	vertexStagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo vertexStagingAllocInfo{};
	vertexStagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	vertexStagingAllocInfo.flags =
	    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
	    VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo vertexStagingAllocResult;
	if (vmaCreateBuffer(m_context->getAllocator(), &vertexStagingInfo,
	                    &vertexStagingAllocInfo, &vertexStagingBuffer,
	                    &vertexStagingAllocation,
	                    &vertexStagingAllocResult) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create vertex staging buffer!");
	}

	// Copy data to staging buffer
	memcpy(vertexStagingAllocResult.pMappedData, vertices.data(),
	       (size_t)vertexBufferSize);

	// Create device-local buffer (GPU-only, fast)
	VkBufferCreateInfo vertexBufferInfo{};
	vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferInfo.size = vertexBufferSize;
	vertexBufferInfo.usage =
	    VK_BUFFER_USAGE_TRANSFER_DST_BIT |  // Destination for transfer
	    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;  // Will be used as vertex buffer
	vertexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo vertexAllocInfo{};
	vertexAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	vertexAllocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

	if (vmaCreateBuffer(m_context->getAllocator(), &vertexBufferInfo,
	                    &vertexAllocInfo, &m_vertexBuffer,
	                    &m_vertexBufferAllocation, nullptr) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create vertex buffer!");
	}

	// === INDEX BUFFER ===

	// Create staging buffer
	VkBuffer indexStagingBuffer;
	VmaAllocation indexStagingAllocation;

	VkBufferCreateInfo indexStagingInfo{};
	indexStagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	indexStagingInfo.size = indexBufferSize;
	indexStagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	indexStagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo indexStagingAllocInfo{};
	indexStagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	indexStagingAllocInfo.flags =
	    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
	    VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo indexStagingAllocResult;
	if (vmaCreateBuffer(m_context->getAllocator(), &indexStagingInfo,
	                    &indexStagingAllocInfo, &indexStagingBuffer,
	                    &indexStagingAllocation,
	                    &indexStagingAllocResult) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create index staging buffer!");
	}

	// Copy data to staging buffer
	memcpy(indexStagingAllocResult.pMappedData, indices.data(),
	       (size_t)indexBufferSize);

	// Create device-local buffer
	VkBufferCreateInfo indexBufferInfo{};
	indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	indexBufferInfo.size = indexBufferSize;
	indexBufferInfo.usage =
	    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	indexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo indexAllocInfo{};
	indexAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	indexAllocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

	if (vmaCreateBuffer(m_context->getAllocator(), &indexBufferInfo,
	                    &indexAllocInfo, &m_indexBuffer,
	                    &m_indexBufferAllocation, nullptr) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create index buffer!");
	}

	// Copy both buffers in a single command submission
	VkCommandBuffer commandBuffer = m_context->beginSingleTimeCommands();

	VkBufferCopy vertexCopyRegion{};
	vertexCopyRegion.size = vertexBufferSize;
	vkCmdCopyBuffer(commandBuffer, vertexStagingBuffer, m_vertexBuffer, 1,
	                &vertexCopyRegion);

	VkBufferCopy indexCopyRegion{};
	indexCopyRegion.size = indexBufferSize;
	vkCmdCopyBuffer(commandBuffer, indexStagingBuffer, m_indexBuffer, 1,
	                &indexCopyRegion);

	m_context->endSingleTimeCommands(commandBuffer);

	vmaDestroyBuffer(m_context->getAllocator(), vertexStagingBuffer,
	                 vertexStagingAllocation);
	vmaDestroyBuffer(m_context->getAllocator(), indexStagingBuffer,
	                 indexStagingAllocation);
}

void RenderSystem::recordCommandBuffer(VkCommandBuffer commandBuffer,
                                       uint32_t imageIndex,
                                       SwapChain& swapChain, World& world,
                                       const CameraSystem& cameraSystem,
                                       MaterialManager& materialManager) {
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
		throw std::runtime_error("Failed to begin recording command buffer!");
	}

	VkRenderPassBeginInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = m_renderPass;
	renderPassInfo.framebuffer = swapChain.getFramebuffers()[imageIndex];
	renderPassInfo.renderArea.offset = {0, 0};
	renderPassInfo.renderArea.extent = swapChain.getExtent();

	std::array<VkClearValue, 2> clearValues{};
	clearValues[0].color = {{0.1f, 0.1f, 0.1f, 1.0f}};
	clearValues[1].depthStencil = {1.0f, 0};

	renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	renderPassInfo.pClearValues = clearValues.data();

	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo,
	                     VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                  m_graphicsPipeline);

	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(swapChain.getExtent().width);
	viewport.height = static_cast<float>(swapChain.getExtent().height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.offset = {0, 0};
	scissor.extent = swapChain.getExtent();
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	VkBuffer vertexBuffers[] = {m_vertexBuffer};
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
	vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

	// Calculate view and projection matrices once
	float aspectRatio =
	    swapChain.getExtent().width / (float)swapChain.getExtent().height;
	glm::mat4 view = cameraSystem.getViewMatrix(world);
	glm::mat4 projection = cameraSystem.getProjectionMatrix(world, aspectRatio);

	// Render all entities with Transform + MeshRenderer
	for (Entity entity :
	     world.view<Transform, MeshRenderer, MaterialBinding>()) {
		Transform& transform = world.getComponent<Transform>(entity);
		MeshRenderer& meshRenderer = world.getComponent<MeshRenderer>(entity);
		MaterialBinding& materialBinding =
		    world.getComponent<MaterialBinding>(entity);

		if (!meshRenderer.visible) continue;

		VkDescriptorSet descriptorSet =
		    materialManager.getDescriptorSet(materialBinding.materialID);
		if (descriptorSet != VK_NULL_HANDLE) {
			vkCmdBindDescriptorSets(
			    commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			    m_pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);
		}

		// Get all mesh IDs (handles both single and multiple)
		std::vector<uint32_t> meshIDsToRender = meshRenderer.getMeshIDs();

		// Render each mesh with the same transform
		for (uint32_t currentMeshID : meshIDsToRender) {
			auto it = m_meshes.find(currentMeshID);
			if (it == m_meshes.end()) {
				continue;
			}

			const MeshInfo& meshInfo = it->second;
			glm::mat4 model = transform.worldMatrix;

			// Bind material descriptor set
			// NOTE: We need access to MaterialManager here
			// For now, skip binding if descriptor set is null
			// TODO: Pass MaterialManager to drawFrame

			struct {
				glm::mat4 model;
				glm::mat4 view;
				glm::mat4 projection;
			} pushConstants;

			pushConstants.model = model;
			pushConstants.view = view;
			pushConstants.projection = projection;

			vkCmdPushConstants(commandBuffer, m_pipelineLayout,
			                   VK_SHADER_STAGE_VERTEX_BIT, 0,
			                   sizeof(pushConstants), &pushConstants);

			vkCmdDrawIndexed(commandBuffer, meshInfo.indexCount, 1,
			                 meshInfo.firstIndex, meshInfo.firstVertex, 0);
		}
	}

	vkCmdEndRenderPass(commandBuffer);

	if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to record command buffer!");
	}
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