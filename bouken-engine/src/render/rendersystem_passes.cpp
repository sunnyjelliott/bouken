#include "render/rendersystem.h"
#include "render/shaderutils.h"

namespace {

void setObjectName(VulkanContext& context, VkObjectType type, uint64_t handle,
                   const std::string& name) {
	if (name.empty()) return;

	VkDebugUtilsObjectNameInfoEXT nameInfo{};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = type;
	nameInfo.objectHandle = handle;
	nameInfo.pObjectName = name.c_str();
	context.setDebugName(nameInfo);
}

// -------------------------------------------------------
// Render pass
//
// Deliberately thin. VkRenderPass, VkFramebuffer and subpass dependencies have
// no equivalent under dynamic rendering or a non-Vulkan backend, so this holds
// data and does nothing else - no dependency graph, no derived barriers. That
// keeps it cheap to delete once it stops being the right concept.
// -------------------------------------------------------

// Every attachment in this renderer is single-sampled and ignores stencil.
VkAttachmentDescription colorAttachment(VkFormat format,
                                        VkAttachmentLoadOp loadOp,
                                        VkImageLayout finalLayout) {
	VkAttachmentDescription a{};
	a.format = format;
	a.samples = VK_SAMPLE_COUNT_1_BIT;
	a.loadOp = loadOp;
	a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	a.finalLayout = finalLayout;
	return a;
}

VkAttachmentDescription depthAttachment(VkFormat format,
                                        VkAttachmentLoadOp loadOp,
                                        VkImageLayout initialLayout,
                                        VkImageLayout finalLayout) {
	VkAttachmentDescription a{};
	a.format = format;
	a.samples = VK_SAMPLE_COUNT_1_BIT;
	a.loadOp = loadOp;
	// Always STORE: the lighting pass samples depth to reconstruct world
	// position. DONT_CARE leaves the contents undefined and surfaces as
	// tile-shaped garbage.
	a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	a.initialLayout = initialLayout;
	a.finalLayout = finalLayout;
	return a;
}

// Every dependency in this renderer is by-region. The three named forms below
// cover all of them; they are literal constructors, not inferred barriers.
VkSubpassDependency dependency(uint32_t srcSubpass, uint32_t dstSubpass,
                               VkPipelineStageFlags srcStage,
                               VkAccessFlags srcAccess,
                               VkPipelineStageFlags dstStage,
                               VkAccessFlags dstAccess) {
	VkSubpassDependency d{};
	d.srcSubpass = srcSubpass;
	d.dstSubpass = dstSubpass;
	d.srcStageMask = srcStage;
	d.srcAccessMask = srcAccess;
	d.dstStageMask = dstStage;
	d.dstAccessMask = dstAccess;
	d.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	return d;
}

// Depth written as an attachment -> depth tested as an attachment.
VkSubpassDependency depthWriteToDepthTest(uint32_t srcSubpass,
                                          uint32_t dstSubpass) {
	return dependency(srcSubpass, dstSubpass,
	                  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
	                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
	                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
	                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
}

// Color written as an attachment -> sampled in a fragment shader.
VkSubpassDependency colorWriteToShaderRead(uint32_t srcSubpass,
                                           uint32_t dstSubpass) {
	return dependency(
	    srcSubpass, dstSubpass, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
}

// Depth written as an attachment -> sampled as a texture in a fragment shader.
VkSubpassDependency depthWriteToShaderRead(uint32_t srcSubpass,
                                           uint32_t dstSubpass) {
	return dependency(
	    srcSubpass, dstSubpass, VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
	    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
	    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
}

struct RenderPassDesc {
	std::vector<VkAttachmentDescription>
	    attachments;  // color first, depth last
	bool hasDepthAttachment = false;
	std::vector<VkSubpassDependency> dependencies;
	std::string debugName;
};

VkRenderPass buildRenderPass(VulkanContext& context,
                             const RenderPassDesc& desc) {
	// Convention across every pass here: color attachments come first, the
	// depth attachment (if any) is last. The refs follow from that, so no pass
	// has to spell them out.
	const uint32_t colorCount = static_cast<uint32_t>(desc.attachments.size()) -
	                            (desc.hasDepthAttachment ? 1u : 0u);

	std::vector<VkAttachmentReference> colorRefs(colorCount);
	for (uint32_t i = 0; i < colorCount; i++) {
		colorRefs[i].attachment = i;
		colorRefs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}

	VkAttachmentReference depthRef{};
	depthRef.attachment = colorCount;
	depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = colorCount;
	subpass.pColorAttachments = colorCount > 0 ? colorRefs.data() : nullptr;
	subpass.pDepthStencilAttachment =
	    desc.hasDepthAttachment ? &depthRef : nullptr;

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount =
	    static_cast<uint32_t>(desc.attachments.size());
	renderPassInfo.pAttachments = desc.attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount =
	    static_cast<uint32_t>(desc.dependencies.size());
	renderPassInfo.pDependencies = desc.dependencies.data();

	VkRenderPass renderPass = VK_NULL_HANDLE;
	if (vkCreateRenderPass(context.getDevice(), &renderPassInfo, nullptr,
	                       &renderPass) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create render pass: " +
		                         desc.debugName);
	}

	setObjectName(context, VK_OBJECT_TYPE_RENDER_PASS,
	              reinterpret_cast<uint64_t>(renderPass), desc.debugName);

	return renderPass;
}

VkFramebuffer buildFramebuffer(VulkanContext& context, VkRenderPass renderPass,
                               const std::vector<VkImageView>& attachments,
                               VkExtent2D extent,
                               const std::string& debugName) {
	VkFramebufferCreateInfo framebufferInfo{};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = renderPass;
	framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	framebufferInfo.pAttachments = attachments.data();
	framebufferInfo.width = extent.width;
	framebufferInfo.height = extent.height;
	framebufferInfo.layers = 1;

	VkFramebuffer framebuffer = VK_NULL_HANDLE;
	if (vkCreateFramebuffer(context.getDevice(), &framebufferInfo, nullptr,
	                        &framebuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create framebuffer: " + debugName);
	}

	setObjectName(context, VK_OBJECT_TYPE_FRAMEBUFFER,
	              reinterpret_cast<uint64_t>(framebuffer), debugName);

	return framebuffer;
}

// -------------------------------------------------------
// Graphics pipeline
//
// Describes everything that differs between the renderer's graphics pipelines.
// Everything they share - topology, front face, polygon mode, sample count,
// passthrough blending, dynamic viewport/scissor - lives in the builder.
//
// A pass with genuinely exotic needs (tessellation, real blending, multiple
// subpasses) should write Vulkan inline rather than growing this struct.
// -------------------------------------------------------

// Vertex::getBindingDescription/getAttributeDescriptions return by value, so
// the builder calls them itself; storing them in the desc would dangle.
enum class VertexInput {
	None,           // vertices generated in the vertex shader
	StandardVertex  // Vertex struct bound at binding 0
};

struct GraphicsPipelineDesc {
	const char* vertShaderPath = nullptr;
	const char* fragShaderPath = nullptr;  // nullptr => vertex-only pipeline
	// Points at caller-owned storage that must outlive the build call.
	const VkSpecializationInfo* fragSpecialization = nullptr;

	VertexInput vertexInput = VertexInput::None;
	VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;

	VkBool32 depthTestEnable = VK_FALSE;
	VkBool32 depthWriteEnable = VK_FALSE;
	VkCompareOp depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

	uint32_t colorAttachmentCount = 1;  // N identical passthrough blend states
	std::vector<VkDescriptorSetLayout> setLayouts;

	VkShaderStageFlags pushConstantStages = 0;
	uint32_t pushConstantSize = 0;  // 0 => no push constant range

	VkRenderPass renderPass = VK_NULL_HANDLE;
	uint32_t subpass = 0;

	std::string debugName;
};

void buildGraphicsPipeline(VulkanContext& context,
                           const GraphicsPipelineDesc& desc,
                           VkPipelineLayout& outLayout,
                           VkPipeline& outPipeline) {
	// --- Pipeline layout ---
	const bool hasPushConstants = desc.pushConstantSize > 0;

	VkPushConstantRange pushRange{};
	pushRange.stageFlags = desc.pushConstantStages;
	pushRange.offset = 0;
	pushRange.size = desc.pushConstantSize;

	VkPipelineLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = static_cast<uint32_t>(desc.setLayouts.size());
	layoutInfo.pSetLayouts =
	    desc.setLayouts.empty() ? nullptr : desc.setLayouts.data();
	layoutInfo.pushConstantRangeCount = hasPushConstants ? 1 : 0;
	layoutInfo.pPushConstantRanges = hasPushConstants ? &pushRange : nullptr;

	if (vkCreatePipelineLayout(context.getDevice(), &layoutInfo, nullptr,
	                           &outLayout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create pipeline layout: " +
		                         desc.debugName);
	}

	setObjectName(context, VK_OBJECT_TYPE_PIPELINE_LAYOUT,
	              reinterpret_cast<uint64_t>(outLayout),
	              desc.debugName + "_layout");

	// --- Shader stages ---
	VkShaderModule vertModule = ShaderUtils::createShaderModule(
	    context, ShaderUtils::readFile(desc.vertShaderPath));
	VkShaderModule fragModule = VK_NULL_HANDLE;

	std::vector<VkPipelineShaderStageCreateInfo> stages;

	VkPipelineShaderStageCreateInfo vertStage{};
	vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertStage.module = vertModule;
	vertStage.pName = "main";
	stages.push_back(vertStage);

	if (desc.fragShaderPath != nullptr) {
		fragModule = ShaderUtils::createShaderModule(
		    context, ShaderUtils::readFile(desc.fragShaderPath));

		VkPipelineShaderStageCreateInfo fragStage{};
		fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragStage.module = fragModule;
		fragStage.pName = "main";
		fragStage.pSpecializationInfo = desc.fragSpecialization;
		stages.push_back(fragStage);
	}

	// --- Vertex input ---
	auto bindingDescription = Vertex::getBindingDescription();
	auto attributeDescriptions = Vertex::getAttributeDescriptions();

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	if (desc.vertexInput == VertexInput::StandardVertex) {
		vertexInputInfo.vertexBindingDescriptionCount = 1;
		vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
		vertexInputInfo.vertexAttributeDescriptionCount =
		    static_cast<uint32_t>(attributeDescriptions.size());
		vertexInputInfo.pVertexAttributeDescriptions =
		    attributeDescriptions.data();
	}

	// --- Fixed state ---
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
	rasterizer.cullMode = desc.cullMode;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = desc.depthTestEnable;
	depthStencil.depthWriteEnable = desc.depthWriteEnable;
	depthStencil.depthCompareOp = desc.depthCompareOp;

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blendPassthrough{};
	blendPassthrough.colorWriteMask =
	    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendPassthrough.blendEnable = VK_FALSE;

	std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(
	    desc.colorAttachmentCount, blendPassthrough);

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = desc.colorAttachmentCount;
	colorBlending.pAttachments =
	    blendAttachments.empty() ? nullptr : blendAttachments.data();

	std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
	                                             VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount =
	    static_cast<uint32_t>(dynamicStates.size());
	dynamicState.pDynamicStates = dynamicStates.data();

	// --- Pipeline ---
	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
	pipelineInfo.pStages = stages.data();
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = outLayout;
	pipelineInfo.renderPass = desc.renderPass;
	pipelineInfo.subpass = desc.subpass;

	const VkResult result =
	    vkCreateGraphicsPipelines(context.getDevice(), VK_NULL_HANDLE, 1,
	                              &pipelineInfo, nullptr, &outPipeline);

	if (fragModule != VK_NULL_HANDLE)
		vkDestroyShaderModule(context.getDevice(), fragModule, nullptr);
	vkDestroyShaderModule(context.getDevice(), vertModule, nullptr);

	if (result != VK_SUCCESS) {
		throw std::runtime_error("Failed to create pipeline: " +
		                         desc.debugName);
	}

	setObjectName(context, VK_OBJECT_TYPE_PIPELINE,
	              reinterpret_cast<uint64_t>(outPipeline), desc.debugName);
}

void createTarget(VulkanContext& context, RenderTarget& target, uint32_t width,
                  uint32_t height, VkFormat format, VkImageUsageFlags usage,
                  VkImageAspectFlags aspect, std::string_view debugName) {
	RenderTargetDesc desc{};
	desc.width = width;
	desc.height = height;
	desc.format = format;
	desc.usage = usage;
	desc.aspect = aspect;
	desc.debugName = debugName;

	target.create(context, context.getAllocator(), desc);
}

}  // anonymous namespace

void RenderSystem::createDepthPrepass() {
	RenderPassDesc passDesc{};
	passDesc.attachments = {
	    depthAttachment(m_depth.format, VK_ATTACHMENT_LOAD_OP_CLEAR,
	                    VK_IMAGE_LAYOUT_UNDEFINED,
	                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL),
	};
	passDesc.hasDepthAttachment = true;
	passDesc.dependencies = {
	    depthWriteToDepthTest(0, VK_SUBPASS_EXTERNAL),  // -> geometry pass
	};
	passDesc.debugName = "depth_prepass";

	m_depthPrepass.renderPass = buildRenderPass(m_context, passDesc);

	m_depthPrepass.framebuffer = buildFramebuffer(
	    m_context, m_depthPrepass.renderPass, {m_depth.target.getImageView()},
	    m_swapChain.getExtent(), "depth_prepass_framebuffer");

	GraphicsPipelineDesc pipelineDesc{};
	pipelineDesc.vertShaderPath = "shaders/depth_vert.spv";
	// No fragment stage - this pass only writes depth.
	// Vertex is sliced in the shader, so passing full vertex data is fine.
	pipelineDesc.vertexInput = VertexInput::StandardVertex;
	pipelineDesc.cullMode = VK_CULL_MODE_BACK_BIT;
	pipelineDesc.depthTestEnable = VK_TRUE;
	pipelineDesc.depthWriteEnable = VK_TRUE;
	pipelineDesc.depthCompareOp = VK_COMPARE_OP_LESS;
	pipelineDesc.colorAttachmentCount = 0;
	pipelineDesc.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
	pipelineDesc.pushConstantSize = sizeof(glm::mat4) * 3;
	pipelineDesc.renderPass = m_depthPrepass.renderPass;
	pipelineDesc.debugName = "depth_prepass";

	buildGraphicsPipeline(m_context, pipelineDesc, m_depthPrepass.layout,
	                      m_depthPrepass.pipeline);
}

void RenderSystem::createGeometryPass() {
	RenderPassDesc passDesc{};
	passDesc.attachments = {
	    // 0 baseColorMetallic
	    colorAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_ATTACHMENT_LOAD_OP_CLEAR,
	                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
	    // 1 normals
	    colorAttachment(VK_FORMAT_R16G16_SNORM, VK_ATTACHMENT_LOAD_OP_CLEAR,
	                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
	    // 2 roughnessAOSpecID
	    colorAttachment(VK_FORMAT_R8G8B8A8_UNORM, VK_ATTACHMENT_LOAD_OP_CLEAR,
	                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
	    // 3 emissiveFlags
	    colorAttachment(VK_FORMAT_R16G16B16A16_SFLOAT,
	                    VK_ATTACHMENT_LOAD_OP_CLEAR,
	                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
	    // 4 depth - loaded from the prepass, stored for the lighting pass
	    depthAttachment(m_depth.format, VK_ATTACHMENT_LOAD_OP_LOAD,
	                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL),
	};
	passDesc.hasDepthAttachment = true;
	passDesc.dependencies = {
	    depthWriteToDepthTest(VK_SUBPASS_EXTERNAL, 0),  // depth prepass -> here
	    colorWriteToShaderRead(0, VK_SUBPASS_EXTERNAL),  // -> lighting pass
	};
	passDesc.debugName = "geometry";

	m_geometry.renderPass = buildRenderPass(m_context, passDesc);

	GraphicsPipelineDesc pipelineDesc{};
	pipelineDesc.vertShaderPath = "shaders/geometry_vert.spv";
	pipelineDesc.fragShaderPath = "shaders/geometry_frag.spv";
	pipelineDesc.vertexInput = VertexInput::StandardVertex;
	pipelineDesc.cullMode = VK_CULL_MODE_BACK_BIT;
	pipelineDesc.depthTestEnable = VK_TRUE;
	pipelineDesc.depthWriteEnable = VK_FALSE;  // prepass already wrote depth
	// Depth already holds the nearest value, so LEQUAL still rejects every
	// occluded fragment. Preferred over EQUAL, which punches holes in the
	// G-buffer on any last-bit disagreement with the prepass.
	pipelineDesc.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	pipelineDesc.colorAttachmentCount = 4;
	pipelineDesc.setLayouts = {
	    m_frameSetLayout,     // set 0: frame data (view, proj, camera pos)
	    m_objectSetLayout,    // set 1: stub - keeps slot aligned with other
	                          // passes
	    m_materialSetLayout,  // set 2: material textures + scalars
	};
	pipelineDesc.pushConstantStages = VK_SHADER_STAGE_VERTEX_BIT;
	pipelineDesc.pushConstantSize =
	    sizeof(glm::mat4) * 3;  // model, view, projection
	pipelineDesc.renderPass = m_geometry.renderPass;
	pipelineDesc.debugName = "geometry";

	buildGraphicsPipeline(m_context, pipelineDesc, m_geometry.layout,
	                      m_geometry.pipeline);
}

void RenderSystem::createLightingPass() {
	// Depth is deliberately NOT an attachment here. Both lighting.frag and
	// skybox.frag sample it through set 1 binding 4, and sampling an image
	// that is simultaneously bound as an attachment is an attachment feedback
	// loop - undefined without VK_EXT_attachment_feedback_loop_layout, and it
	// reads back tile-shaped garbage on real hardware.
	RenderPassDesc passDesc{};
	passDesc.attachments = {
	    colorAttachment(VK_FORMAT_R16G16B16A16_SFLOAT,
	                    VK_ATTACHMENT_LOAD_OP_CLEAR,
	                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL),
	};
	passDesc.dependencies = {
	    colorWriteToShaderRead(VK_SUBPASS_EXTERNAL, 0),  // geometry -> here
	    colorWriteToShaderRead(0, VK_SUBPASS_EXTERNAL),  // -> tonemap pass
	    // Depth write (prepass + geometry pass) -> lighting/skybox fragment
	    // shaders sampling depth as a texture
	    depthWriteToShaderRead(VK_SUBPASS_EXTERNAL, 0),
	};
	passDesc.debugName = "lighting";

	m_lighting.renderPass = buildRenderPass(m_context, passDesc);

	m_hdr.framebuffer = buildFramebuffer(
	    m_context, m_lighting.renderPass, {m_hdr.target.getImageView()},
	    m_swapChain.getExtent(), "hdr_framebuffer");

	// Specialization constant: bakes IBLSystem's actual prefilteredMipLevels
	// into the shader at pipeline-creation time, replacing the shader's own
	// default (5) so the two can never silently drift apart.
	//
	// These three locals must outlive the buildGraphicsPipeline call below -
	// the desc only holds a pointer to them.
	uint32_t specPrefilteredMips = m_iblSystem.prefilteredMipLevels();

	VkSpecializationMapEntry specMapEntry{};
	specMapEntry.constantID = 0;
	specMapEntry.offset = 0;
	specMapEntry.size = sizeof(uint32_t);

	VkSpecializationInfo specInfo{};
	specInfo.mapEntryCount = 1;
	specInfo.pMapEntries = &specMapEntry;
	specInfo.dataSize = sizeof(uint32_t);
	specInfo.pData = &specPrefilteredMips;

	GraphicsPipelineDesc pipelineDesc{};
	pipelineDesc.vertShaderPath = "shaders/fullscreen_vert.spv";
	pipelineDesc.fragShaderPath = "shaders/lighting_frag.spv";
	pipelineDesc.fragSpecialization = &specInfo;
	pipelineDesc.vertexInput = VertexInput::None;  // fullscreen triangle
	pipelineDesc.cullMode = VK_CULL_MODE_NONE;
	pipelineDesc.depthTestEnable = VK_FALSE;  // no depth involvement
	pipelineDesc.depthWriteEnable = VK_FALSE;
	pipelineDesc.setLayouts = {
	    m_frameSetLayout,    // set 0: frame data
	    m_lightingSetLayout  // set 1: G-buffer textures
	};
	pipelineDesc.renderPass = m_lighting.renderPass;
	pipelineDesc.debugName = "lighting";

	buildGraphicsPipeline(m_context, pipelineDesc, m_lighting.layout,
	                      m_lighting.pipeline);
}

void RenderSystem::createSkyboxPipeline() {
	GraphicsPipelineDesc pipelineDesc{};
	pipelineDesc.vertShaderPath = "shaders/skybox_vert.spv";
	pipelineDesc.fragShaderPath = "shaders/skybox_frag.spv";
	// No vertex input - cube positions generated in the vertex shader
	pipelineDesc.vertexInput = VertexInput::None;
	// Camera sits inside the cube - cull front faces to see the interior
	pipelineDesc.cullMode = VK_CULL_MODE_FRONT_BIT;
	// The lighting render pass has no depth attachment, so there is nothing to
	// test against. skybox.frag instead samples depth (set 1 binding 4) and
	// discards wherever geometry already wrote a closer value - equivalent to
	// the LEQUAL test this replaces, given the xyww far-plane trick in
	// skybox.vert.
	pipelineDesc.depthTestEnable = VK_FALSE;
	pipelineDesc.depthWriteEnable = VK_FALSE;
	// Set 0: frame data, Set 1: lighting/IBL set (env cubemap lives at binding
	// 9)
	pipelineDesc.setLayouts = {m_frameSetLayout, m_lightingSetLayout};
	// Reuses the lighting render pass - same subpass, second pipeline bind
	pipelineDesc.renderPass = m_lighting.renderPass;
	pipelineDesc.debugName = "skybox";

	buildGraphicsPipeline(m_context, pipelineDesc, m_skybox.layout,
	                      m_skybox.pipeline);
}

void RenderSystem::createTonemapPass() {
	RenderPassDesc passDesc{};
	passDesc.attachments = {
	    // We overwrite every pixel, so the previous contents are irrelevant.
	    colorAttachment(m_swapChainFormat, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
	                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),
	};
	passDesc.dependencies = {
	    // HDR target must be readable before tonemap samples it
	    colorWriteToShaderRead(VK_SUBPASS_EXTERNAL, 0),
	};
	passDesc.debugName = "tonemap";

	m_tonemap.renderPass = buildRenderPass(m_context, passDesc);

	// Swapchain framebuffers are created by SwapChain::createFramebuffers,
	// called with m_tonemap.renderPass - handled in initialize()

	GraphicsPipelineDesc pipelineDesc{};
	pipelineDesc.vertShaderPath = "shaders/fullscreen_vert.spv";
	pipelineDesc.fragShaderPath = "shaders/tonemap_frag.spv";
	pipelineDesc.vertexInput = VertexInput::None;  // fullscreen triangle
	pipelineDesc.cullMode = VK_CULL_MODE_NONE;
	pipelineDesc.depthTestEnable = VK_FALSE;
	pipelineDesc.depthWriteEnable = VK_FALSE;
	// Set 1 only: HDR sampler - no frame data needed for tonemap
	pipelineDesc.setLayouts = {m_tonemapSetLayout};
	pipelineDesc.renderPass = m_tonemap.renderPass;
	pipelineDesc.debugName = "tonemap";

	buildGraphicsPipeline(m_context, pipelineDesc, m_tonemap.layout,
	                      m_tonemap.pipeline);
}

void RenderSystem::createGBufferFramebuffer() {
	m_gbuffer.framebuffer =
	    buildFramebuffer(m_context, m_geometry.renderPass,
	                     {
	                         m_gbuffer.baseColorMetallic.getImageView(),  // 0
	                         m_gbuffer.normals.getImageView(),            // 1
	                         m_gbuffer.roughnessAOSpecID.getImageView(),  // 2
	                         m_gbuffer.emissiveFlags.getImageView(),      // 3
	                         m_depth.target.getImageView(),               // 4
	                     },
	                     m_swapChain.getExtent(), "gbuffer_framebuffer");
}

void RenderSystem::createDepthResources() {
	// Find a supported depth format
	const std::vector<VkFormat> candidates = {VK_FORMAT_D32_SFLOAT,
	                                          VK_FORMAT_D32_SFLOAT_S8_UINT,
	                                          VK_FORMAT_D24_UNORM_S8_UINT};

	m_depth.format = VK_FORMAT_UNDEFINED;
	for (VkFormat format : candidates) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(m_context.getPhysicalDevice(),
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

	createTarget(m_context, m_depth.target, m_swapChain.getExtent().width,
	             m_swapChain.getExtent().height, m_depth.format,
	             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
	                 VK_IMAGE_USAGE_SAMPLED_BIT,  // lighting pass samples depth
	             VK_IMAGE_ASPECT_DEPTH_BIT, "depth");
}

void RenderSystem::createGBufferTargets() {
	const uint32_t w = m_swapChain.getExtent().width;
	const uint32_t h = m_swapChain.getExtent().height;

	constexpr VkImageUsageFlags gbufferUsage =
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	createTarget(m_context, m_gbuffer.baseColorMetallic, w, h,
	             VK_FORMAT_R8G8B8A8_UNORM, gbufferUsage,
	             VK_IMAGE_ASPECT_COLOR_BIT, "gbuffer_basecolor_metallic");

	createTarget(m_context, m_gbuffer.normals, w, h, VK_FORMAT_R16G16_SNORM,
	             gbufferUsage, VK_IMAGE_ASPECT_COLOR_BIT, "gbuffer_normals");

	createTarget(m_context, m_gbuffer.roughnessAOSpecID, w, h,
	             VK_FORMAT_R8G8B8A8_UNORM, gbufferUsage,
	             VK_IMAGE_ASPECT_COLOR_BIT, "gbuffer_roughness_ao_specular_id");

	createTarget(m_context, m_gbuffer.emissiveFlags, w, h,
	             VK_FORMAT_R16G16B16A16_SFLOAT, gbufferUsage,
	             VK_IMAGE_ASPECT_COLOR_BIT, "gbuffer_emissive_flags");
}

void RenderSystem::createHDRTarget() {
	createTarget(
	    m_context, m_hdr.target, m_swapChain.getExtent().width,
	    m_swapChain.getExtent().height, VK_FORMAT_R16G16B16A16_SFLOAT,
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
	    VK_IMAGE_ASPECT_COLOR_BIT, "hdr_target");
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

	if (vkCreateSampler(m_context.getDevice(), &gbufferSamplerInfo, nullptr,
	                    &m_gbufferSampler) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create G-buffer sampler!");
	}

	setObjectName(m_context, VK_OBJECT_TYPE_SAMPLER,
	              reinterpret_cast<uint64_t>(m_gbufferSampler),
	              "gbuffer_sampler");
}
