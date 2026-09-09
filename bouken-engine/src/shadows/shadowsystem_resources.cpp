#include "shadows/shadowsystem.h"

#include "vulkancontext.h"

// -------------------------------------------------------
// Moments render pass
//
// Shared by both shadow paths: the spot atlas framebuffer and every
// cascade framebuffer are created against this one VkRenderPass.
// -------------------------------------------------------

void ShadowSystem::createRenderPass() {
	VkAttachmentDescription colorAttachment{};
	colorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout =
	    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // blur pass samples this

	VkAttachmentDescription depthAttachment{};
	depthAttachment.format = VK_FORMAT_D32_SFLOAT;
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp =
	    VK_ATTACHMENT_STORE_OP_DONT_CARE;  // nothing downstream reads it
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout =
	    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	std::array<VkAttachmentDescription, 2> attachments = {colorAttachment,
	                                                      depthAttachment};

	VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkAttachmentReference depthRef{
	    1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;
	subpass.pDepthStencilAttachment = &depthRef;

	// Moments write -> blur pass samples the color output. (Not a depth-read
	// dependency this time - the depth attachment here is private to this
	// pass, never sampled, so only the color write needs to be visible.)
	VkSubpassDependency dependency{};
	dependency.srcSubpass = 0;
	dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	// Not BY_REGION: the consumer is the separable blur, which samples
	// neighbouring texels. By-region would promise a locality this read
	// doesn't have.
	dependency.dependencyFlags = 0;

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
		throw std::runtime_error(
		    "Failed to create shadow moments render pass!");
	}
}

// -------------------------------------------------------
// Blur render pass
//
// One render pass serves both directions and both shadow paths; only the
// framebuffer, source descriptor set and push constants differ between
// invocations.
// -------------------------------------------------------

void ShadowSystem::createBlurRenderPass() {
	VkAttachmentDescription colorAttachment{};
	colorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp =
	    VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // fullscreen overwrite
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;

	std::array<VkSubpassDependency, 2> dependencies{};

	// Wait for whichever pass wrote this pass's source texture (moments
	// pass for horizontal, horizontal blur for vertical).
	//
	// The destination scope also has to cover the color attachment's
	// UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL transition, which this dependency
	// performs - without COLOR_ATTACHMENT_OUTPUT/WRITE here that transition is
	// unordered against the pass's own writes (a WAW hazard).
	//
	// Neither dependency is BY_REGION: a blur samples neighbouring texels, so
	// the read is not confined to the matching framebuffer region.
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask =
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[0].dstStageMask =
	    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].dstAccessMask =
	    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[0].dependencyFlags = 0;

	// Signal for whichever pass reads this pass's output next (vertical
	// blur for horizontal, lighting pass for vertical)
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask =
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	dependencies[1].dependencyFlags = 0;

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	if (vkCreateRenderPass(m_context->getDevice(), &renderPassInfo, nullptr,
	                       &m_blurRenderPass) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create shadow blur render pass!");
	}
}

// -------------------------------------------------------
// Blur descriptors
//
// Owns the descriptor set layout and the descriptor pool that the cascades
// also allocate from (see createCascadeBlurDescriptorSets). Also allocates
// and writes the atlas's own two sets - the one place this shared file
// touches atlas-specific state.
// -------------------------------------------------------

void ShadowSystem::createBlurDescriptorResources() {
	VkDescriptorSetLayoutBinding binding{};
	binding.binding = 0;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	binding.descriptorCount = 1;
	binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = 1;
	layoutInfo.pBindings = &binding;

	if (vkCreateDescriptorSetLayout(m_context->getDevice(), &layoutInfo,
	                                nullptr, &m_blurSetLayout) != VK_SUCCESS) {
		throw std::runtime_error(
		    "Failed to create shadow blur descriptor set layout!");
	}

	VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	                              2 + CASCADE_COUNT * 2};
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = 1;
	poolInfo.pPoolSizes = &poolSize;
	poolInfo.maxSets = 2 + CASCADE_COUNT * 2;

	if (vkCreateDescriptorPool(m_context->getDevice(), &poolInfo, nullptr,
	                           &m_descriptorPool) != VK_SUCCESS) {
		throw std::runtime_error(
		    "Failed to create shadow blur descriptor pool!");
	}

	VkDescriptorSetLayout layouts[2] = {m_blurSetLayout, m_blurSetLayout};
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_descriptorPool;
	allocInfo.descriptorSetCount = 2;
	allocInfo.pSetLayouts = layouts;

	VkDescriptorSet sets[2];
	if (vkAllocateDescriptorSets(m_context->getDevice(), &allocInfo, sets) !=
	    VK_SUCCESS) {
		throw std::runtime_error(
		    "Failed to allocate shadow blur descriptor sets!");
	}
	m_blurSetMoments = sets[0];
	m_blurSetScratch = sets[1];

	// Targets never resize (no window-resize support yet), so these writes
	// are one-time - no per-frame descriptor churn.
	VkDescriptorImageInfo momentsInfo{m_sampler, m_shadowMoments.getImageView(),
	                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	VkDescriptorImageInfo scratchInfo{m_sampler,
	                                  m_shadowBlurScratch.getImageView(),
	                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

	std::array<VkWriteDescriptorSet, 2> writes{};
	writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	             nullptr,
	             m_blurSetMoments,
	             0,
	             0,
	             1,
	             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	             &momentsInfo,
	             nullptr,
	             nullptr};
	writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
	             nullptr,
	             m_blurSetScratch,
	             0,
	             0,
	             1,
	             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
	             &scratchInfo,
	             nullptr,
	             nullptr};

	vkUpdateDescriptorSets(m_context->getDevice(), 2, writes.data(), 0,
	                       nullptr);
}

// -------------------------------------------------------
// Sampler
//
// One sampler for every shadow read: the lighting pass, the atlas blur
// sets, and the cascade blur sets all bind this handle.
// -------------------------------------------------------

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
