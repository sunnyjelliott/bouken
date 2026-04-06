#include "render/rendersystem.h"

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

void RenderSystem::createDepthPrepass() {
	VkAttachmentDescription depthAttachment{};
	depthAttachment.format = m_depth.format;
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout =
	    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthRef{};
	depthRef.attachment = 0;
	depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 0;
	subpass.pDepthStencilAttachment = &depthRef;

	// Depth prepass -> geometry pass
	VkSubpassDependency dependency{};
	dependency.srcSubpass = 0;
	dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
	dependency.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
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
	                       &m_depthPrepass.renderPass) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create depth prepass render pass!");
	}

	VkImageView depthView = m_depth.target.getImageView();

	VkFramebufferCreateInfo framebufferInfo{};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = m_depthPrepass.renderPass;
	framebufferInfo.attachmentCount = 1;
	framebufferInfo.pAttachments = &depthView;
	framebufferInfo.width = m_swapChain->getExtent().width;
	framebufferInfo.height = m_swapChain->getExtent().height;
	framebufferInfo.layers = 1;

	if (vkCreateFramebuffer(m_context->getDevice(), &framebufferInfo, nullptr,
	                        &m_depthPrepass.framebuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create depth prepass framebuffer!");
	}

	auto vertShaderCode = readFile("shaders/depth_vert.spv");
	VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);

	VkPipelineShaderStageCreateInfo vertStage{};
	vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertStage.module = vertShaderModule;
	vertStage.pName = "main";

	auto bindingDescription = Vertex::getBindingDescription();
	auto attributeDescriptions = Vertex::getAttributeDescriptions();

	// vertex sliced in shader, fine to pass full vertex data here.
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
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
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

	// No color blend state - no color attachments
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
	pushConstantRange.size = sizeof(glm::mat4) * 3;

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 0;  // no descriptor sets needed
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	if (vkCreatePipelineLayout(m_context->getDevice(), &pipelineLayoutInfo,
	                           nullptr, &m_depthPrepass.layout) != VK_SUCCESS) {
		throw std::runtime_error(
		    "Failed to create depth prepass pipeline layout!");
	}

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 1;  // vertex only
	pipelineInfo.pStages = &vertStage;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = m_depthPrepass.layout;
	pipelineInfo.renderPass = m_depthPrepass.renderPass;
	pipelineInfo.subpass = 0;

	if (vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1,
	                              &pipelineInfo, nullptr,
	                              &m_depthPrepass.pipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create depth prepass pipeline!");
	}

	vkDestroyShaderModule(m_context->getDevice(), vertShaderModule, nullptr);
}

void RenderSystem::createGeometryPass() {
	auto makeColorAttachment = [](VkFormat format) {
		VkAttachmentDescription a{};
		a.format = format;
		a.samples = VK_SAMPLE_COUNT_1_BIT;
		a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		a.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		return a;
	};

	std::array<VkAttachmentDescription, 5> attachments = {
	    makeColorAttachment(VK_FORMAT_R8G8B8A8_UNORM),  // 0 baseColorMetallic
	    makeColorAttachment(VK_FORMAT_R16G16_SNORM),    // 1 normals
	    makeColorAttachment(VK_FORMAT_R8G8B8A8_UNORM),  // 2 roughnessAOSpecID
	    makeColorAttachment(VK_FORMAT_R16G16B16A16_SFLOAT),  // 3 emissiveFlags
	    // depth
	    [&]() {
		    VkAttachmentDescription a{};
		    a.format = m_depth.format;
		    a.samples = VK_SAMPLE_COUNT_1_BIT;
		    a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		    a.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		    a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		    a.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		    a.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		    return a;
	    }()};

	std::array<VkAttachmentReference, 4> colorRefs = {{
	    {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
	    {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
	    {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
	    {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
	}};

	VkAttachmentReference depthRef{};
	depthRef.attachment = 4;
	depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
	subpass.pColorAttachments = colorRefs.data();
	subpass.pDepthStencilAttachment = &depthRef;

	// Two dependencies:
	// 1. Geometry pass waits for depth prepass to finish writing depth
	// 2. Lighting pass waits for geometry pass to finish writing G-buffer
	std::array<VkSubpassDependency, 2> dependencies;

	// Depth prepass → geometry pass
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependencies[0].srcAccessMask =
	    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	// Geometry pass → lighting pass
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask =
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	if (vkCreateRenderPass(m_context->getDevice(), &renderPassInfo, nullptr,
	                       &m_geometry.renderPass) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create geometry render pass!");
	}

	// -------------------------------------------------------
	// Pipeline
	// -------------------------------------------------------
	auto vertShaderCode = readFile("shaders/geometry_vert.spv");
	auto fragShaderCode = readFile("shaders/geometry_frag.spv");

	VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
	VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

	VkPipelineShaderStageCreateInfo shaderStages[] = {
	    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
	     VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule, "main", nullptr},
	    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
	     VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule, "main", nullptr},
	};

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
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_FALSE;  // prepass already wrote depth
	depthStencil.depthCompareOp =
	    VK_COMPARE_OP_EQUAL;  // only draw what the prepass accepted

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// One blend attachment per color attachment - all passthrough
	VkPipelineColorBlendAttachmentState blendPassthrough{};
	blendPassthrough.colorWriteMask =
	    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendPassthrough.blendEnable = VK_FALSE;

	std::array<VkPipelineColorBlendAttachmentState, 4> blendAttachments = {
	    blendPassthrough, blendPassthrough, blendPassthrough, blendPassthrough};

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount =
	    static_cast<uint32_t>(blendAttachments.size());
	colorBlending.pAttachments = blendAttachments.data();

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
	pushConstantRange.size = sizeof(glm::mat4) * 3;  // model, view, projection

	std::array<VkDescriptorSetLayout, 3> setLayouts = {
	    m_frameSetLayout,     // set 0: frame data (view, proj, camera pos)
	    m_objectSetLayout,    // set 1: stub - keeps slot aligned with other
	                          // passes
	    m_materialSetLayout,  // set 2: material textures + scalars
	};

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount =
	    static_cast<uint32_t>(setLayouts.size());
	pipelineLayoutInfo.pSetLayouts = setLayouts.data();
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	if (vkCreatePipelineLayout(m_context->getDevice(), &pipelineLayoutInfo,
	                           nullptr, &m_geometry.layout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create geometry pipeline layout!");
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
	pipelineInfo.layout = m_geometry.layout;
	pipelineInfo.renderPass = m_geometry.renderPass;
	pipelineInfo.subpass = 0;

	if (vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1,
	                              &pipelineInfo, nullptr,
	                              &m_geometry.pipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create geometry pipeline!");
	}

	vkDestroyShaderModule(m_context->getDevice(), fragShaderModule, nullptr);
	vkDestroyShaderModule(m_context->getDevice(), vertShaderModule, nullptr);
}

void RenderSystem::createLightingPass() {
	// -------------------------------------------------------
	// Render pass - one HDR color attachment, no depth
	// -------------------------------------------------------
	VkAttachmentDescription colorAttachment{};
	colorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentReference colorRef{};
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;

	// G-buffer reads must be complete before lighting samples them,
	// and HDR write must be complete before tonemap samples it
	std::array<VkSubpassDependency, 2> dependencies{};

	// Geometry pass → lighting pass
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask =
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	// Lighting pass → tonemap pass
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask =
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	if (vkCreateRenderPass(m_context->getDevice(), &renderPassInfo, nullptr,
	                       &m_lighting.renderPass) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create lighting render pass!");
	}

	// -------------------------------------------------------
	// Framebuffer - HDR target only
	// -------------------------------------------------------
	VkImageView hdrView = m_hdr.target.getImageView();

	VkFramebufferCreateInfo framebufferInfo{};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = m_lighting.renderPass;
	framebufferInfo.attachmentCount = 1;
	framebufferInfo.pAttachments = &hdrView;
	framebufferInfo.width = m_swapChain->getExtent().width;
	framebufferInfo.height = m_swapChain->getExtent().height;
	framebufferInfo.layers = 1;

	if (vkCreateFramebuffer(m_context->getDevice(), &framebufferInfo, nullptr,
	                        &m_hdr.framebuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create lighting framebuffer!");
	}

	// -------------------------------------------------------
	// Pipeline - fullscreen triangle, no vertex input
	// -------------------------------------------------------
	auto vertCode = readFile("shaders/fullscreen_vert.spv");
	auto fragCode = readFile("shaders/lighting_frag.spv");

	VkShaderModule vertModule = createShaderModule(vertCode);
	VkShaderModule fragModule = createShaderModule(fragCode);

	VkPipelineShaderStageCreateInfo shaderStages[] = {
	    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
	     VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main", nullptr},
	    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
	     VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main", nullptr},
	};

	// No vertex input - fullscreen triangle generated in vertex shader
	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
	rasterizer.cullMode = VK_CULL_MODE_NONE;  // fullscreen triangle, no culling
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_FALSE;  // no depth involvement
	depthStencil.depthWriteEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blendAttachment{};
	blendAttachment.colorWriteMask =
	    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendAttachment.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &blendAttachment;

	std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
	                                             VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount =
	    static_cast<uint32_t>(dynamicStates.size());
	dynamicState.pDynamicStates = dynamicStates.data();

	// Set 0: frame data, Set 1: G-buffer textures
	std::array<VkDescriptorSetLayout, 2> setLayouts = {m_frameSetLayout,
	                                                   m_lightingSetLayout};

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount =
	    static_cast<uint32_t>(setLayouts.size());
	pipelineLayoutInfo.pSetLayouts = setLayouts.data();

	if (vkCreatePipelineLayout(m_context->getDevice(), &pipelineLayoutInfo,
	                           nullptr, &m_lighting.layout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create lighting pipeline layout!");
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
	pipelineInfo.layout = m_lighting.layout;
	pipelineInfo.renderPass = m_lighting.renderPass;
	pipelineInfo.subpass = 0;

	if (vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1,
	                              &pipelineInfo, nullptr,
	                              &m_lighting.pipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create lighting pipeline!");
	}

	vkDestroyShaderModule(m_context->getDevice(), fragModule, nullptr);
	vkDestroyShaderModule(m_context->getDevice(), vertModule, nullptr);
}

void RenderSystem::createTonemapPass() {
	// -------------------------------------------------------
	// Render pass - swapchain image, no depth
	// -------------------------------------------------------
	VkAttachmentDescription colorAttachment{};
	colorAttachment.format = m_swapChainFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp =
	    VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // we overwrite every pixel
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorRef{};
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;

	// HDR target must be readable before tonemap samples it
	VkSubpassDependency dependency{};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	if (vkCreateRenderPass(m_context->getDevice(), &renderPassInfo, nullptr,
	                       &m_tonemap.renderPass) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create tonemap render pass!");
	}

	// Swapchain framebuffers are created by SwapChain::createFramebuffers,
	// called with m_tonemap.renderPass - handled in initialize()

	// -------------------------------------------------------
	// Pipeline - fullscreen triangle, reuses fullscreen_vert.spv
	// -------------------------------------------------------
	auto vertCode = readFile("shaders/fullscreen_vert.spv");
	auto fragCode = readFile("shaders/tonemap_frag.spv");

	VkShaderModule vertModule = createShaderModule(vertCode);
	VkShaderModule fragModule = createShaderModule(fragCode);

	VkPipelineShaderStageCreateInfo shaderStages[] = {
	    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
	     VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main", nullptr},
	    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
	     VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main", nullptr},
	};

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_FALSE;
	depthStencil.depthWriteEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blendAttachment{};
	blendAttachment.colorWriteMask =
	    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendAttachment.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &blendAttachment;

	std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
	                                             VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount =
	    static_cast<uint32_t>(dynamicStates.size());
	dynamicState.pDynamicStates = dynamicStates.data();

	// Set 1 only: HDR sampler - no frame data needed for tonemap
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &m_tonemapSetLayout;

	if (vkCreatePipelineLayout(m_context->getDevice(), &pipelineLayoutInfo,
	                           nullptr, &m_tonemap.layout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create tonemap pipeline layout!");
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
	pipelineInfo.layout = m_tonemap.layout;
	pipelineInfo.renderPass = m_tonemap.renderPass;
	pipelineInfo.subpass = 0;

	if (vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1,
	                              &pipelineInfo, nullptr,
	                              &m_tonemap.pipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create tonemap pipeline!");
	}

	vkDestroyShaderModule(m_context->getDevice(), fragModule, nullptr);
	vkDestroyShaderModule(m_context->getDevice(), vertModule, nullptr);
}

void RenderSystem::createGBufferFramebuffer() {
	std::array<VkImageView, 5> attachments = {
	    m_gbuffer.baseColorMetallic.getImageView(),  // 0
	    m_gbuffer.normals.getImageView(),            // 1
	    m_gbuffer.roughnessAOSpecID.getImageView(),  // 2
	    m_gbuffer.emissiveFlags.getImageView(),      // 3
	    m_depth.target.getImageView(),               // 4
	};

	VkFramebufferCreateInfo framebufferInfo{};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = m_geometry.renderPass;
	framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	framebufferInfo.pAttachments = attachments.data();
	framebufferInfo.width = m_swapChain->getExtent().width;
	framebufferInfo.height = m_swapChain->getExtent().height;
	framebufferInfo.layers = 1;

	if (vkCreateFramebuffer(m_context->getDevice(), &framebufferInfo, nullptr,
	                        &m_gbuffer.framebuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create G-buffer framebuffer!");
	}
}

void RenderSystem::createDepthResources() {
	// Find a supported depth format
	const std::vector<VkFormat> candidates = {VK_FORMAT_D32_SFLOAT,
	                                          VK_FORMAT_D32_SFLOAT_S8_UINT,
	                                          VK_FORMAT_D24_UNORM_S8_UINT};

	m_depth.format = VK_FORMAT_UNDEFINED;
	for (VkFormat format : candidates) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(m_context->getPhysicalDevice(),
		                                    format, &props);
		if (props.optimalTilingFeatures &
		    VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
			m_depth.format = format;
			break;
		}
	}

	if (m_depth.format == VK_FORMAT_UNDEFINED) {
		throw std::runtime_error("Failed to find supported depth format!");
	}

	RenderTargetDesc desc{};
	desc.width = m_swapChain->getExtent().width;
	desc.height = m_swapChain->getExtent().height;
	desc.format = m_depth.format;
	desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
	             VK_IMAGE_USAGE_SAMPLED_BIT;  // lighting pass will sample depth
	desc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
	desc.debugName = "depth";

	m_depth.target.create(*m_context, m_context->getAllocator(), desc);
}

void RenderSystem::createGBufferTargets() {
	const uint32_t w = m_swapChain->getExtent().width;
	const uint32_t h = m_swapChain->getExtent().height;

	constexpr VkImageUsageFlags gbufferUsage =
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	RenderTargetDesc baseColorDesc{};
	baseColorDesc.width = w;
	baseColorDesc.height = h;
	baseColorDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
	baseColorDesc.usage = gbufferUsage;
	baseColorDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	baseColorDesc.debugName = "gbuffer_basecolor_metallic";
	m_gbuffer.baseColorMetallic.create(*m_context, m_context->getAllocator(),
	                                   baseColorDesc);

	RenderTargetDesc normalsDesc{};
	normalsDesc.width = w;
	normalsDesc.height = h;
	normalsDesc.format = VK_FORMAT_R16G16_SNORM;
	normalsDesc.usage = gbufferUsage;
	normalsDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	normalsDesc.debugName = "gbuffer_normals";
	m_gbuffer.normals.create(*m_context, m_context->getAllocator(),
	                         normalsDesc);

	RenderTargetDesc roughnessDesc{};
	roughnessDesc.width = w;
	roughnessDesc.height = h;
	roughnessDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
	roughnessDesc.usage = gbufferUsage;
	roughnessDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	roughnessDesc.debugName = "gbuffer_roughness_ao_specular_id";
	m_gbuffer.roughnessAOSpecID.create(*m_context, m_context->getAllocator(),
	                                   roughnessDesc);

	RenderTargetDesc emissiveDesc{};
	emissiveDesc.width = w;
	emissiveDesc.height = h;
	emissiveDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	emissiveDesc.usage = gbufferUsage;
	emissiveDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	emissiveDesc.debugName = "gbuffer_emissive_flags";
	m_gbuffer.emissiveFlags.create(*m_context, m_context->getAllocator(),
	                               emissiveDesc);
}

void RenderSystem::createHDRTarget() {
	RenderTargetDesc hdrDesc{};
	hdrDesc.width = m_swapChain->getExtent().width;
	hdrDesc.height = m_swapChain->getExtent().height;
	hdrDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	hdrDesc.usage =
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	hdrDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	hdrDesc.debugName = "hdr_target";
	m_hdr.target.create(*m_context, m_context->getAllocator(), hdrDesc);
}

void RenderSystem::createSamplers() {
	// -------------------------------------------------------
	// G-buffer sampler
	// -------------------------------------------------------
	VkSamplerCreateInfo gbufferSamplerInfo{};
	gbufferSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	gbufferSamplerInfo.magFilter = VK_FILTER_NEAREST;
	gbufferSamplerInfo.minFilter = VK_FILTER_NEAREST;
	gbufferSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	gbufferSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	gbufferSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	gbufferSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	gbufferSamplerInfo.anisotropyEnable = VK_FALSE;
	gbufferSamplerInfo.maxAnisotropy = 1.0f;
	gbufferSamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
	gbufferSamplerInfo.unnormalizedCoordinates = VK_FALSE;
	gbufferSamplerInfo.compareEnable = VK_FALSE;
	gbufferSamplerInfo.mipLodBias = 0.0f;
	gbufferSamplerInfo.minLod = 0.0f;
	gbufferSamplerInfo.maxLod = 0.0f;

	if (vkCreateSampler(m_context->getDevice(), &gbufferSamplerInfo, nullptr,
	                    &m_gbufferSampler) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create G-buffer sampler!");
	}

	VkDebugUtilsObjectNameInfoEXT nameInfo{};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = VK_OBJECT_TYPE_SAMPLER;
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_gbufferSampler);
	nameInfo.pObjectName = "gbuffer_sampler";
	m_context->setDebugName(nameInfo);
}