#include "shadows/shadowsystem.h"

#include "light.h"
#include "transform.h"
#include "vulkancontext.h"

// -------------------------------------------------------
// Tile layout
//
// One 4096x4096 atlas split into resolution tiers; a caster's assigned
// (tier, slot) fixes both its pixel rect and its UV region.
// -------------------------------------------------------

void ShadowSystem::computeTierLayout() {
	uint32_t yOffset = 0;
	for (uint32_t i = 0; i < TIER_COUNT; i++) {
		m_tiers[i].resolution = TIER_RESOLUTIONS[i];
		m_tiers[i].slotCount = TIER_SLOT_COUNTS[i];
		m_tiers[i].yOffset = yOffset;
		yOffset += TIER_RESOLUTIONS[i];
	}
	// yOffset now sums to 3840 - within the 4096 atlas, 256px margin unused
}

glm::vec4 ShadowSystem::computeAtlasRegion(uint32_t tier, uint32_t slot) const {
	const ShadowTier& t = m_tiers[tier];
	float tileX = static_cast<float>(slot * t.resolution);
	float tileY = static_cast<float>(t.yOffset);
	float size = static_cast<float>(t.resolution);
	float atlas = static_cast<float>(ATLAS_SIZE);

	return glm::vec4(tileX / atlas, tileY / atlas, size / atlas, size / atlas);
}

// -------------------------------------------------------
// Targets and framebuffers
//
// Created in the order initialize() needs them: moments target, then the
// two blur targets, then the framebuffers, then the one-time layout
// transition that makes the resolved target safe to sample.
// -------------------------------------------------------

void ShadowSystem::createShadowTarget() {
	RenderTargetDesc momentsDesc{};
	momentsDesc.width = ATLAS_SIZE;
	momentsDesc.height = ATLAS_SIZE;
	momentsDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	momentsDesc.usage =
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	momentsDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	momentsDesc.debugName = "shadow_moments_spot_tile0";
	m_shadowMoments.create(*m_context, m_context->getAllocator(), momentsDesc);

	RenderTargetDesc depthDesc{};
	depthDesc.width = ATLAS_SIZE;
	depthDesc.height = ATLAS_SIZE;
	depthDesc.format = VK_FORMAT_D32_SFLOAT;
	depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	depthDesc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
	depthDesc.debugName = "shadow_moments_depth_spot_tile0";
	m_shadowMomentsDepth.create(*m_context, m_context->getAllocator(),
	                            depthDesc);
}

void ShadowSystem::createFramebuffer() {
	std::array<VkImageView, 2> attachments = {
	    m_shadowMoments.getImageView(), m_shadowMomentsDepth.getImageView()};

	VkFramebufferCreateInfo framebufferInfo{};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = m_renderPass;
	framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	framebufferInfo.pAttachments = attachments.data();
	framebufferInfo.width = ATLAS_SIZE;
	framebufferInfo.height = ATLAS_SIZE;
	framebufferInfo.layers = 1;

	if (vkCreateFramebuffer(m_context->getDevice(), &framebufferInfo, nullptr,
	                        &m_framebuffer) != VK_SUCCESS) {
		throw std::runtime_error(
		    "Failed to create shadow moments framebuffer!");
	}
}

void ShadowSystem::createBlurTargets() {
	RenderTargetDesc desc{};
	desc.width = ATLAS_SIZE;
	desc.height = ATLAS_SIZE;
	desc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	desc.usage =
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	desc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;

	desc.debugName = "shadow_blur_scratch_spot_tile0";
	m_shadowBlurScratch.create(*m_context, m_context->getAllocator(), desc);

	desc.debugName = "shadow_resolved_spot_tile0";
	m_shadowResolved.create(*m_context, m_context->getAllocator(), desc);
}

void ShadowSystem::createBlurFramebuffers() {
	auto makeFramebuffer = [&](VkImageView view) {
		VkFramebufferCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		info.renderPass = m_blurRenderPass;
		info.attachmentCount = 1;
		info.pAttachments = &view;
		info.width = ATLAS_SIZE;
		info.height = ATLAS_SIZE;
		info.layers = 1;

		VkFramebuffer fb;
		if (vkCreateFramebuffer(m_context->getDevice(), &info, nullptr, &fb) !=
		    VK_SUCCESS) {
			throw std::runtime_error(
			    "Failed to create shadow blur framebuffer!");
		}
		return fb;
	};

	m_blurScratchFramebuffer =
	    makeFramebuffer(m_shadowBlurScratch.getImageView());
	m_blurResolvedFramebuffer =
	    makeFramebuffer(m_shadowResolved.getImageView());
}

void ShadowSystem::prepareBlurTargetLayouts() {
	VkCommandBuffer cmd = m_context->beginSingleTimeCommands();

	auto makeBarrier = [](VkImage image) {
		VkImageMemoryBarrier barrier{};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		barrier.srcAccessMask = 0;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		return barrier;
	};

	// Contents stay undefined - nothing samples the map on a frame with no
	// casters, this only establishes the layout the descriptor set promises.
	//
	// The cascade blur targets need the same treatment for the same reason:
	// renderCascadeBlur() early-returns when there is no directional caster,
	// which would leave them UNDEFINED while their blur descriptor sets
	// already declare SHADER_READ_ONLY_OPTIMAL.
	std::vector<VkImageMemoryBarrier> barriers = {
	    makeBarrier(m_shadowBlurScratch.getImage()),
	    makeBarrier(m_shadowResolved.getImage())};

	for (uint32_t i = 0; i < CASCADE_COUNT; i++) {
		barriers.push_back(makeBarrier(m_cascades[i].blurScratch.getImage()));
		barriers.push_back(makeBarrier(m_cascades[i].resolved.getImage()));
	}

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
	                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
	                     0, nullptr, static_cast<uint32_t>(barriers.size()),
	                     barriers.data());

	m_context->endSingleTimeCommands(cmd);
}

// -------------------------------------------------------
// Light-space matrices
// -------------------------------------------------------

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

// -------------------------------------------------------
// Per-frame recording
//
// The moments pass itself is recorded by ShadowSystem::render(); this is
// the blur that follows it.
// -------------------------------------------------------

void ShadowSystem::renderBlur(VkCommandBuffer cmd) {
	struct BlurPushConstants {
		glm::vec2 texelDirection;
		glm::vec2 tileUVMin;
		glm::vec2 tileUVMax;
		float atlasSizeInv;
	};

	auto recordDirection = [&](VkFramebuffer framebuffer,
	                           VkDescriptorSet source,
	                           glm::vec2 texelDirection) {
		VkRenderPassBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		beginInfo.renderPass = m_blurRenderPass;
		beginInfo.framebuffer = framebuffer;
		beginInfo.renderArea.offset = {0, 0};
		beginInfo.renderArea.extent = {ATLAS_SIZE, ATLAS_SIZE};
		beginInfo.clearValueCount = 0;

		vkCmdBeginRenderPass(cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_blurPipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		                        m_blurPipelineLayout, 0, 1, &source, 0,
		                        nullptr);

		// Per-tile now, not one fullscreen draw - viewport/scissor confine
		// rasterization to this caster's tile, and the UV clamp confines
		// sampling to the same tile, so the blur can no longer read across
		// into a neighbouring light's region.
		for (const ShadowSlotAssignment& assignment : m_activeCasters) {
			const ShadowTier& tier = m_tiers[assignment.tier];
			uint32_t tileX = assignment.slot * tier.resolution;
			uint32_t tileY = tier.yOffset;

			VkViewport viewport{};
			viewport.x = static_cast<float>(tileX);
			viewport.y = static_cast<float>(tileY);
			viewport.width = static_cast<float>(tier.resolution);
			viewport.height = static_cast<float>(tier.resolution);
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(cmd, 0, 1, &viewport);

			VkRect2D scissor{
			    {static_cast<int32_t>(tileX), static_cast<int32_t>(tileY)},
			    {tier.resolution, tier.resolution}};
			vkCmdSetScissor(cmd, 0, 1, &scissor);

			BlurPushConstants push{};
			push.texelDirection = texelDirection;
			push.tileUVMin =
			    glm::vec2(assignment.atlasRegion.x, assignment.atlasRegion.y);
			push.tileUVMax =
			    push.tileUVMin +
			    glm::vec2(assignment.atlasRegion.z, assignment.atlasRegion.w);
			push.atlasSizeInv = 1.0f / static_cast<float>(ATLAS_SIZE);

			vkCmdPushConstants(cmd, m_blurPipelineLayout,
			                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push),
			                   &push);

			vkCmdDraw(cmd, 3, 1, 0, 0);
		}

		vkCmdEndRenderPass(cmd);
	};

	constexpr float texelSize = 1.0f / static_cast<float>(ATLAS_SIZE);

	recordDirection(m_blurScratchFramebuffer, m_blurSetMoments,
	                glm::vec2(texelSize, 0.0f));
	recordDirection(m_blurResolvedFramebuffer, m_blurSetScratch,
	                glm::vec2(0.0f, texelSize));
}