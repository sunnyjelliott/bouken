#include "rendersystem.h"

#include <vk_mem_alloc.h>

#include "camerasystem.h"
#include "meshloader.h"
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

std::vector<Vertex> createCubeVertices() {
	return {
	    // 8 unique vertices
	    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 1.0f}},  // 0: back-bottom-left
	    {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},   // 1: back-bottom-right
	    {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}},    // 2: back-top-right
	    {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}},   // 3: back-top-left
	    {{-0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}},   // 4: front-bottom-left
	    {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}},    // 5: front-bottom-right
	    {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 1.0f}},     // 6: front-top-right
	    {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}},    // 7: front-top-left
	};
}

std::vector<uint32_t> createCubeIndices() {
	return {
	    // Front face
	    4,
	    5,
	    6,
	    4,
	    6,
	    7,
	    // Back face
	    1,
	    0,
	    3,
	    1,
	    3,
	    2,
	    // Top face
	    7,
	    6,
	    2,
	    7,
	    2,
	    3,
	    // Bottom face
	    0,
	    1,
	    5,
	    0,
	    5,
	    4,
	    // Right face
	    5,
	    1,
	    2,
	    5,
	    2,
	    6,
	    // Left face
	    0,
	    4,
	    7,
	    0,
	    7,
	    3,
	};
}

std::vector<Vertex> createPyramidVertices() {
	return {
	    // 5 unique vertices
	    {{-0.5f, 0.0f, -0.5f}, {1.0f, 1.0f, 1.0f}},  // 0: base back-left
	    {{0.5f, 0.0f, -0.5f}, {1.0f, 1.0f, 1.0f}},   // 1: base back-right
	    {{0.5f, 0.0f, 0.5f}, {1.0f, 1.0f, 1.0f}},    // 2: base front-right
	    {{-0.5f, 0.0f, 0.5f}, {1.0f, 1.0f, 1.0f}},   // 3: base front-left
	    {{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}},    // 4: apex
	};
}

std::vector<uint32_t> createPyramidIndices() {
	return {
	    // Base
	    0,
	    1,
	    2,
	    0,
	    2,
	    3,
	    // Front
	    3,
	    2,
	    4,
	    // Right
	    2,
	    1,
	    4,
	    // Back
	    1,
	    0,
	    4,
	    // Left
	    0,
	    3,
	    4,
	};
}

void RenderSystem::initialize(VulkanContext& context, SwapChain& swapChain) {
	m_context = &context;
	m_swapChainFormat = swapChain.getImageFormat();

	createRenderPass();
	createGraphicsPipeline();
	swapChain.createFramebuffers(m_renderPass);
	createMeshBuffers();
	createCommandBuffer();
	createSyncObjects();
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

	vkDestroyPipeline(m_context->getDevice(), m_graphicsPipeline, nullptr);
	vkDestroyPipelineLayout(m_context->getDevice(), m_pipelineLayout, nullptr);
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
                             const CameraSystem& cameraSystem) {
	vkWaitForFences(m_context->getDevice(), 1, &m_inFlightFence, VK_TRUE,
	                UINT64_MAX);
	vkResetFences(m_context->getDevice(), 1, &m_inFlightFence);

	uint32_t imageIndex;
	vkAcquireNextImageKHR(m_context->getDevice(), swapChain.getSwapChain(),
	                      UINT64_MAX, m_imageAvailableSemaphore, VK_NULL_HANDLE,
	                      &imageIndex);

	vkResetCommandBuffer(m_commandBuffer, 0);
	recordCommandBuffer(m_commandBuffer, imageIndex, swapChain, world,
	                    cameraSystem);

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

void RenderSystem::createRenderPass() {
	// Get format from swap chain (passed during initialize)
	// We need to temporarily get it - or store reference
	// For now, we'll query it when needed

	VkAttachmentDescription colorAttachment{};
	// Note: We need swap chain format here - this is a bit awkward
	// We'll address this by storing a reference or accepting it as parameter
	colorAttachment.format =
	    VK_FORMAT_B8G8R8A8_SRGB;  // TODO: Get from swap chain
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	// TODO: MSAA - change samples and add resolve attachment
	// TODO: Deferred - multiple color attachments

	VkAttachmentReference colorAttachmentRef{};
	colorAttachmentRef.attachment = 0;
	colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorAttachmentRef;

	VkSubpassDependency dependency{};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = 0;
	dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
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
	pipelineLayoutInfo.setLayoutCount = 0;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
	// TODO: Add descriptor set layouts for uniforms/textures

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

void RenderSystem::uploadMeshData(const std::vector<Vertex>& vertices,
                                  const std::vector<uint32_t>& indices) {
	// Clean up old buffers if they exist
	if (m_vertexBuffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(m_context->getAllocator(), m_vertexBuffer,
		                 m_vertexBufferAllocation);
	}
	if (m_indexBuffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(m_context->getAllocator(), m_indexBuffer,
		                 m_indexBufferAllocation);
	}

	// Create vertex buffer
	VkDeviceSize vertexBufferSize = sizeof(Vertex) * vertices.size();

	VkBufferCreateInfo vertexBufferInfo{};
	vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferInfo.size = vertexBufferSize;
	vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	vertexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo vertexAllocInfo{};
	vertexAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	vertexAllocInfo.flags =
	    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
	    VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo vertexAllocInfoResult;
	if (vmaCreateBuffer(m_context->getAllocator(), &vertexBufferInfo,
	                    &vertexAllocInfo, &m_vertexBuffer,
	                    &m_vertexBufferAllocation,
	                    &vertexAllocInfoResult) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create vertex buffer with VMA!");
	}

	memcpy(vertexAllocInfoResult.pMappedData, vertices.data(),
	       (size_t)vertexBufferSize);

	// Create index buffer
	VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();

	VkBufferCreateInfo indexBufferInfo{};
	indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	indexBufferInfo.size = indexBufferSize;
	indexBufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	indexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo indexAllocInfo{};
	indexAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	indexAllocInfo.flags =
	    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
	    VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo indexAllocInfoResult;
	if (vmaCreateBuffer(m_context->getAllocator(), &indexBufferInfo,
	                    &indexAllocInfo, &m_indexBuffer,
	                    &m_indexBufferAllocation,
	                    &indexAllocInfoResult) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create index buffer with VMA!");
	}

	memcpy(indexAllocInfoResult.pMappedData, indices.data(),
	       (size_t)indexBufferSize);
}

void RenderSystem::recordCommandBuffer(VkCommandBuffer commandBuffer,
                                       uint32_t imageIndex,
                                       SwapChain& swapChain, World& world,
                                       const CameraSystem& cameraSystem) {
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

	VkClearValue clearColor = {{{0.1f, 0.1f, 0.1f, 1.0f}}};
	renderPassInfo.clearValueCount = 1;
	renderPassInfo.pClearValues = &clearColor;

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

	float aspectRatio = (float)swapChain.getExtent().width /
	                    (float)swapChain.getExtent().height;
	glm::mat4 view = cameraSystem.getViewMatrix(world);
	glm::mat4 projection = cameraSystem.getProjectionMatrix(world, aspectRatio);

	for (Entity entity : world.view<Transform, MeshRenderer>()) {
		Transform& transform = world.getComponent<Transform>(entity);
		MeshRenderer& meshRenderer = world.getComponent<MeshRenderer>(entity);

		if (!meshRenderer.visible) continue;

		auto it = m_meshes.find(meshRenderer.meshID);
		if (it == m_meshes.end()) continue;

		const MeshInfo& meshInfo = it->second;

		// Create model matrix from transform
		glm::mat4 model = transform.worldMatrix;

		struct {
			glm::mat4 model;
			glm::mat4 view;
			glm::mat4 projection;
		} pushConstants;

		pushConstants.model = model;
		pushConstants.view = view;
		pushConstants.projection = projection;

		vkCmdPushConstants(commandBuffer, m_pipelineLayout,
		                   VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants),
		                   &pushConstants);

		vkCmdDrawIndexed(commandBuffer, meshInfo.indexCount, 1,
		                 meshInfo.firstIndex, meshInfo.firstVertex, 0);
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
	auto cubeVerts = createCubeVertices();
	auto cubeIndices = createCubeIndices();
	m_meshes[m_nextMeshID++] = {
	    .firstVertex = static_cast<uint32_t>(m_allVertices.size()),
	    .vertexCount = static_cast<uint32_t>(cubeVerts.size()),
	    .firstIndex = static_cast<uint32_t>(m_allIndices.size()),
	    .indexCount = static_cast<uint32_t>(cubeIndices.size())};
	m_allVertices.insert(m_allVertices.end(), cubeVerts.begin(),
	                     cubeVerts.end());
	m_allIndices.insert(m_allIndices.end(), cubeIndices.begin(),
	                    cubeIndices.end());

	// Mesh 1: Pyramid
	auto pyramidVerts = createPyramidVertices();
	auto pyramidIndices = createPyramidIndices();
	m_meshes[m_nextMeshID++] = {
	    .firstVertex = static_cast<uint32_t>(m_allVertices.size()),
	    .vertexCount = static_cast<uint32_t>(pyramidVerts.size()),
	    .firstIndex = static_cast<uint32_t>(m_allIndices.size()),
	    .indexCount = static_cast<uint32_t>(pyramidIndices.size())};
	m_allVertices.insert(m_allVertices.end(), pyramidVerts.begin(),
	                     pyramidVerts.end());
	m_allIndices.insert(m_allIndices.end(), pyramidIndices.begin(),
	                    pyramidIndices.end());

	// Upload to GPU
	uploadMeshData(m_allVertices, m_allIndices);
}