#include "shadows/shadowsystem.h"

#include "light.h"
#include "lighting/lightsystem.h"
#include "render/rendersystem.h"
#include "render/shaderutils.h"
#include "transform.h"
#include "vertex.h"
#include "vulkancontext.h"
#include "world.h"

void ShadowSystem::initialize(VulkanContext& context) {
	m_context = &context;

	createShadowTarget();
	createSampler();
	createRenderPass();
	createPipeline();
	createFramebuffer();
}

void ShadowSystem::cleanup() {
	vkDestroyPipeline(m_context->getDevice(), m_pipeline, nullptr);
	vkDestroyPipelineLayout(m_context->getDevice(), m_pipelineLayout, nullptr);
	vkDestroyFramebuffer(m_context->getDevice(), m_framebuffer, nullptr);
	vkDestroyRenderPass(m_context->getDevice(), m_renderPass, nullptr);
	vkDestroySampler(m_context->getDevice(), m_sampler, nullptr);
	m_shadowTarget.destroy(m_context->getDevice(), m_context->getAllocator());
}

void ShadowSystem::createShadowTarget() {
	// D32_SFLOAT is universally supported for depth-attachment + sampled
	// usage on desktop hardware - no query needed the way the main depth
	// target does (that one also considers stencil formats it doesn't need).
	RenderTargetDesc desc{};
	desc.width = SHADOW_MAP_SIZE;
	desc.height = SHADOW_MAP_SIZE;
	desc.format = VK_FORMAT_D32_SFLOAT;
	desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
	             VK_IMAGE_USAGE_SAMPLED_BIT;
	desc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
	desc.debugName = "shadow_map_spot_tile0";

	m_shadowTarget.create(*m_context, m_context->getAllocator(), desc);
}

void ShadowSystem::createRenderPass() {
	VkAttachmentDescription depthAttachment{};
	depthAttachment.format = VK_FORMAT_D32_SFLOAT;
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentReference depthRef{};
	depthRef.attachment = 0;
	depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 0;
	subpass.pDepthStencilAttachment = &depthRef;

	// Shadow pass -> lighting pass sample, not another depth test - so this
	// synchronizes the write against a shader read, not the next depth stage.
	VkSubpassDependency dependency{};
	dependency.srcSubpass = 0;
	dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
	dependency.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &depthAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	if (vkCreateRenderPass(m_context->getDevice(), &renderPassInfo, nullptr,
	                       &m_renderPass) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create shadow render pass!");
	}
}

void ShadowSystem::createPipeline() {
	auto vertCode = ShaderUtils::readFile("shaders/depth_vert.spv");
	VkShaderModule vertModule =
	    ShaderUtils::createShaderModule(*m_context, vertCode);

	VkPipelineShaderStageCreateInfo vertStage{};
	vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertStage.module = vertModule;
	vertStage.pName = "main";

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

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode =
	    VK_CULL_MODE_BACK_BIT;  // matches depth prepass convention;
	                            // front-face culling to fight peter-panning
	                            // is a tunable worth revisiting once shadows
	                            // are actually on screen
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 0;
	colorBlending.pAttachments = nullptr;

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
	    sizeof(glm::mat4) * 3;  // model, view, projection - same
	                            // contract as depth_vert.spv expects

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 0;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	if (vkCreatePipelineLayout(m_context->getDevice(), &pipelineLayoutInfo,
	                           nullptr, &m_pipelineLayout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create shadow pipeline layout!");
	}

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 1;
	pipelineInfo.pStages = &vertStage;
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

	if (vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1,
	                              &pipelineInfo, nullptr,
	                              &m_pipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create shadow pipeline!");
	}

	vkDestroyShaderModule(m_context->getDevice(), vertModule, nullptr);
}

void ShadowSystem::createFramebuffer() {
	VkImageView depthView = m_shadowTarget.getImageView();

	VkFramebufferCreateInfo framebufferInfo{};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = m_renderPass;
	framebufferInfo.attachmentCount = 1;
	framebufferInfo.pAttachments = &depthView;
	framebufferInfo.width = SHADOW_MAP_SIZE;
	framebufferInfo.height = SHADOW_MAP_SIZE;
	framebufferInfo.layers = 1;

	if (vkCreateFramebuffer(m_context->getDevice(), &framebufferInfo, nullptr,
	                        &m_framebuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create shadow framebuffer!");
	}
}

void ShadowSystem::createSampler() {
	// LINEAR + manual compare in sampleShadow() (lighting.frag), not a
	// hardware comparison sampler - the depth test happens explicitly in
	// the shader, so this is a plain sampler. LINEAR still buys a cheap
	// softening of the hard depth compare via bilinear-interpolated depth
	// values, at no extra cost over NEAREST.
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	samplerInfo.anisotropyEnable = VK_FALSE;
	samplerInfo.maxAnisotropy = 1.0f;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = 0.0f;

	if (vkCreateSampler(m_context->getDevice(), &samplerInfo, nullptr,
	                    &m_sampler) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create shadow map sampler!");
	}

	VkDebugUtilsObjectNameInfoEXT nameInfo{};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = VK_OBJECT_TYPE_SAMPLER;
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_sampler);
	nameInfo.pObjectName = "shadow_map_sampler";
	m_context->setDebugName(nameInfo);
}

LightSpaceMatrices ShadowSystem::computeSpotLightSpaceMatrix(
    const Transform& transform, const Light& light) const {
	const glm::vec3 position = glm::vec3(transform.worldMatrix[3]);
	glm::vec3 forward = glm::normalize(
	    glm::vec3(transform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

	// Guard against a near-vertical forward vector, which makes the default
	// up (0,1,0) collinear with forward and produces a degenerate lookAt.
	glm::vec3 up = (glm::abs(forward.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f)
	                                             : glm::vec3(0.0f, 1.0f, 0.0f);

	glm::mat4 view = glm::lookAt(position, position + forward, up);

	// outerAngle is the cone's half-angle; full vertical FOV is 2x that,
	// with a small pad so the depth map fully covers the lit cone rather
	// than clipping exactly at the falloff edge.
	constexpr float FOV_PADDING = 1.05f;
	float fovRadians = glm::radians(light.outerAngle * 2.0f * FOV_PADDING);

	glm::mat4 proj =
	    glm::perspective(fovRadians, 1.0f, SHADOW_NEAR, SHADOW_FAR);
	proj[1][1] *= -1.0f;  // Vulkan NDC Y-flip

	return {view, proj, proj * view};
}

void ShadowSystem::update(World& world, LightSystem& lightSystem) {
	m_shadowCaster = NULL_ENTITY;

	for (Entity entity : world.view<Transform, Light>()) {
		if (world.getComponent<Light>(entity).castsShadow) {
			m_shadowCaster = entity;
			break;  // step 1: single caster, first match wins
		}
	}

	if (m_shadowCaster == NULL_ENTITY) return;

	const Transform& transform = world.getComponent<Transform>(m_shadowCaster);
	const Light& light = world.getComponent<Light>(m_shadowCaster);

	LightSpaceMatrices lightSpaceMatrices =
	    computeSpotLightSpaceMatrix(transform, light);

	uint32_t index = lightSystem.getLightIndex(m_shadowCaster);
	if (index != UINT32_MAX) {
		lightSystem.getLightsForWrite()[index].lightSpaceMatrix =
		    lightSpaceMatrices.viewProjection;
	}
}

void ShadowSystem::render(VkCommandBuffer commandBuffer, World& world,
                          RenderSystem& renderSystem) {
	if (m_shadowCaster == NULL_ENTITY) return;  // nothing to cast this frame

	VkRenderPassBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	beginInfo.renderPass = m_renderPass;
	beginInfo.framebuffer = m_framebuffer;
	beginInfo.renderArea.offset = {0, 0};
	beginInfo.renderArea.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE};

	VkClearValue depthClear{};
	depthClear.depthStencil = {1.0f, 0};
	beginInfo.clearValueCount = 1;
	beginInfo.pClearValues = &depthClear;

	vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                  m_pipeline);

	VkViewport viewport{};
	viewport.width = static_cast<float>(SHADOW_MAP_SIZE);
	viewport.height = static_cast<float>(SHADOW_MAP_SIZE);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor{{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}};
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	VkBuffer vertexBuffers[] = {renderSystem.getVertexBuffer()};
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
	vkCmdBindIndexBuffer(commandBuffer, renderSystem.getIndexBuffer(), 0,
	                     VK_INDEX_TYPE_UINT32);

	const Transform& casterTransform =
	    world.getComponent<Transform>(m_shadowCaster);
	const Light& casterLight = world.getComponent<Light>(m_shadowCaster);
	LightSpaceMatrices matrices =
	    computeSpotLightSpaceMatrix(casterTransform, casterLight);

	// Step 1: no light-frustum culling and no opacity filtering - every
	// mesh-bearing entity draws, giving an honest worst-case baseline before
	// culling narrows it down later. Known simplification: cutout/transparent
	// materials cast full opaque shadows for now, same tier as round-robin
	// scheduling and the DPSM seam fix - roadmap it if it looks wrong on
	// screen.
	for (Entity entity : world.view<Transform, MeshRenderer>()) {
		const Transform& transform = world.getComponent<Transform>(entity);
		const MeshRenderer& meshRenderer =
		    world.getComponent<MeshRenderer>(entity);
		if (!meshRenderer.visible) continue;

		for (uint32_t meshID : meshRenderer.getMeshIDs()) {
			const RenderSystem::MeshInfo* mesh =
			    renderSystem.getMeshInfo(meshID);
			if (!mesh) continue;

			struct {
				glm::mat4 model;
				glm::mat4 view;
				glm::mat4 projection;
			} push;
			push.model = transform.worldMatrix;
			push.view = matrices.view;
			push.projection = matrices.projection;

			vkCmdPushConstants(commandBuffer, m_pipelineLayout,
			                   VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push),
			                   &push);

			vkCmdDrawIndexed(commandBuffer, mesh->indexCount, 1,
			                 mesh->firstIndex, mesh->firstVertex, 0);
		}
	}

	vkCmdEndRenderPass(commandBuffer);
}