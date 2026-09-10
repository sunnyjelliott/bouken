#include "shadows/shadowsystem.h"

#include "camera.h"
#include "vulkancontext.h"

// -------------------------------------------------------
// Cascade resources
//
// Reuses the shared objects from shadowsystem_resources.cpp: the moments
// render pass, the blur render pass, and the descriptor pool the blur sets
// below are allocated from.
// -------------------------------------------------------

void ShadowSystem::createCascadeUBO() {
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = sizeof(CascadeUBOData);
	bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
	allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo resultInfo{};
	if (vmaCreateBuffer(m_context->getAllocator(), &bufferInfo, &allocInfo,
	                    &m_cascadeUBO, &m_cascadeUBOAllocation,
	                    &resultInfo) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create cascade UBO!");
	}
	m_cascadeUBOMapped = resultInfo.pMappedData;
}

void ShadowSystem::createCascadeTargets() {
	RenderTargetDesc momentsDesc{};
	momentsDesc.width = CASCADE_RESOLUTION;
	momentsDesc.height = CASCADE_RESOLUTION;
	momentsDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	momentsDesc.usage =
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	momentsDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;

	RenderTargetDesc depthDesc{};
	depthDesc.width = CASCADE_RESOLUTION;
	depthDesc.height = CASCADE_RESOLUTION;
	depthDesc.format = VK_FORMAT_D32_SFLOAT;
	depthDesc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	depthDesc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;

	for (uint32_t i = 0; i < CASCADE_COUNT; i++) {
		momentsDesc.debugName = "shadow_moments_cascade" + std::to_string(i);
		m_cascades[i].moments.create(*m_context, m_context->getAllocator(),
		                             momentsDesc);

		depthDesc.debugName =
		    "shadow_moments_depth_cascade" + std::to_string(i);
		m_cascades[i].depth.create(*m_context, m_context->getAllocator(),
		                           depthDesc);

		momentsDesc.debugName =
		    "shadow_blur_scratch_cascade" + std::to_string(i);
		m_cascades[i].blurScratch.create(*m_context, m_context->getAllocator(),
		                                 momentsDesc);

		momentsDesc.debugName = "shadow_resolved_cascade" + std::to_string(i);
		m_cascades[i].resolved.create(*m_context, m_context->getAllocator(),
		                              momentsDesc);
	}
}

void ShadowSystem::createCascadeFramebuffers() {
	for (uint32_t i = 0; i < CASCADE_COUNT; i++) {
		std::array<VkImageView, 2> attachments = {
		    m_cascades[i].moments.getImageView(),
		    m_cascades[i].depth.getImageView()};

		VkFramebufferCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		info.renderPass = m_renderPass;
		info.attachmentCount = static_cast<uint32_t>(attachments.size());
		info.pAttachments = attachments.data();
		info.width = CASCADE_RESOLUTION;
		info.height = CASCADE_RESOLUTION;
		info.layers = 1;

		if (vkCreateFramebuffer(m_context->getDevice(), &info, nullptr,
		                        &m_cascades[i].framebuffer) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create cascade framebuffer!");
		}
	}
}

void ShadowSystem::createCascadeBlurFramebuffers() {
	auto makeFramebuffer = [&](VkImageView view) {
		VkFramebufferCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		info.renderPass = m_blurRenderPass;
		info.attachmentCount = 1;
		info.pAttachments = &view;
		info.width = CASCADE_RESOLUTION;
		info.height = CASCADE_RESOLUTION;
		info.layers = 1;

		VkFramebuffer fb;
		if (vkCreateFramebuffer(m_context->getDevice(), &info, nullptr, &fb) !=
		    VK_SUCCESS) {
			throw std::runtime_error(
			    "Failed to create cascade blur framebuffer!");
		}
		return fb;
	};

	for (uint32_t i = 0; i < CASCADE_COUNT; i++) {
		m_cascades[i].blurScratchFramebuffer =
		    makeFramebuffer(m_cascades[i].blurScratch.getImageView());
		m_cascades[i].blurResolvedFramebuffer =
		    makeFramebuffer(m_cascades[i].resolved.getImageView());
	}
}

void ShadowSystem::createCascadeBlurDescriptorSets() {
	for (uint32_t i = 0; i < CASCADE_COUNT; i++) {
		VkDescriptorSetLayout layouts[2] = {m_blurSetLayout, m_blurSetLayout};
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_descriptorPool;
		allocInfo.descriptorSetCount = 2;
		allocInfo.pSetLayouts = layouts;

		VkDescriptorSet sets[2];
		if (vkAllocateDescriptorSets(m_context->getDevice(), &allocInfo,
		                             sets) != VK_SUCCESS) {
			throw std::runtime_error(
			    "Failed to allocate cascade blur descriptor sets!");
		}
		m_cascades[i].blurSetMoments = sets[0];
		m_cascades[i].blurSetScratch = sets[1];

		VkDescriptorImageInfo momentsInfo{
		    m_sampler, m_cascades[i].moments.getImageView(),
		    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		VkDescriptorImageInfo scratchInfo{
		    m_sampler, m_cascades[i].blurScratch.getImageView(),
		    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

		std::array<VkWriteDescriptorSet, 2> writes{};
		writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		             nullptr,
		             sets[0],
		             0,
		             0,
		             1,
		             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
		             &momentsInfo,
		             nullptr,
		             nullptr};
		writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		             nullptr,
		             sets[1],
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
}

// -------------------------------------------------------
// Split and light-space math
// -------------------------------------------------------

std::array<float, CASCADE_COUNT + 1> ShadowSystem::computeSplitDepths(
    float nearPlane, float farPlane) const {
	std::array<float, CASCADE_COUNT + 1> splits;
	splits[0] = nearPlane;

	for (uint32_t i = 1; i < CASCADE_COUNT; i++) {
		float p = static_cast<float>(i) / static_cast<float>(CASCADE_COUNT);
		float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);
		float uniformSplit = nearPlane + (farPlane - nearPlane) * p;
		splits[i] = CASCADE_SPLIT_LAMBDA * logSplit +
		            (1.0f - CASCADE_SPLIT_LAMBDA) * uniformSplit;
	}

	splits[CASCADE_COUNT] = farPlane;
	return splits;
}

std::array<glm::vec3, 8> ShadowSystem::computeFrustumCornersWorldSpace(
    const glm::mat4& cameraView, float fovRadians, float aspect,
    float splitNear, float splitFar) const {
	glm::mat4 proj = glm::perspective(fovRadians, aspect, splitNear, splitFar);
	proj[1][1] *= -1.0f;  // same Vulkan NDC convention used everywhere else

	glm::mat4 invViewProj = glm::inverse(proj * cameraView);

	std::array<glm::vec3, 8> corners;
	uint32_t idx = 0;
	for (uint32_t x = 0; x < 2; x++) {
		for (uint32_t y = 0; y < 2; y++) {
			for (uint32_t z = 0; z < 2; z++) {
				glm::vec4 ndc(2.0f * x - 1.0f, 2.0f * y - 1.0f,
				              static_cast<float>(z),
				              1.0f);  // Vulkan NDC z in [0,1]
				glm::vec4 worldPos = invViewProj * ndc;
				corners[idx++] = glm::vec3(worldPos) / worldPos.w;
			}
		}
	}
	return corners;
}

LightSpaceMatrices ShadowSystem::computeCascadeViewProjection(
    const std::array<glm::vec3, 8>& corners, const AABB& sceneBounds,
    const glm::vec3& lightDirection, uint32_t resolution) const {
	glm::vec3 center(0.0f);
	for (const glm::vec3& c : corners) center += c;
	center /= 8.0f;

	// XY extent is the bounding sphere of the slice corners, not a tight
	// AABB: a sphere's radius doesn't change as the camera rotates, so
	// world-units-per-texel is fixed and the snap below actually stabilizes
	// the map. A tight per-frame fit would change size every frame and the
	// snap would quantize against a moving grid - which is what the previous
	// occluder-AABB-union fit did.
	float radius = 0.0f;
	for (const glm::vec3& c : corners) {
		radius = glm::max(radius, glm::length(c - center));
	}
	radius = glm::ceil(radius * 16.0f) / 16.0f;  // coarse rounding, further
	                                             // damps size jitter
	radius = glm::max(radius, 1e-4f);  // a degenerate slice would otherwise
	                                   // divide by zero in the snap below

	glm::vec3 up = (glm::abs(lightDirection.y) > 0.99f)
	                   ? glm::vec3(0.0f, 0.0f, 1.0f)
	                   : glm::vec3(0.0f, 1.0f, 0.0f);
	glm::mat4 lightView =
	    glm::lookAt(center - lightDirection * radius * 2.0f, center, up);

	// Texel snapping: with a fixed radius, world-units-per-texel is fixed
	// too, so quantizing the center to that grid makes the ortho frustum
	// shift in whole-texel steps only.
	float worldUnitsPerTexel = (radius * 2.0f) / static_cast<float>(resolution);
	glm::vec3 centerLS = glm::vec3(lightView * glm::vec4(center, 1.0f));
	centerLS.x =
	    glm::floor(centerLS.x / worldUnitsPerTexel) * worldUnitsPerTexel;
	centerLS.y =
	    glm::floor(centerLS.y / worldUnitsPerTexel) * worldUnitsPerTexel;
	glm::vec3 snappedCenter =
	    glm::vec3(glm::inverse(lightView) * glm::vec4(centerLS, 1.0f));

	lightView = glm::lookAt(snappedCenter - lightDirection * radius * 2.0f,
	                        snappedCenter, up);

	// Depth range. lookAt looks down -Z, so a light-space distance from the
	// eye is -z, and the slice sphere spans [radius, radius * 3].
	//
	// The near plane is then pulled back to whatever the furthest scene
	// geometry toward the light is. This is the fix for the clipped shadows:
	// an occluder up-sun of the slice still shadows into it, and with a near
	// plane sitting at the sphere's own tangent plane it was being clipped
	// away. A negative zNear is fine here - unlike perspective, an ortho
	// volume may extend behind its own eye.
	//
	// Only the near side is extended. Receivers only exist inside the slice,
	// so the far plane stays at the sphere, which keeps the depth range to
	// (scene depth along the light) rather than (whole scene diagonal). At
	// Sponza's scale that costs a few bits of moment precision on cascade 0;
	// if it ever shows up as MSM light bleed, the tighter bound is the
	// light-space depth of only those casters whose XY overlaps this
	// cascade's box, which m_casters is already the right list to compute.
	//
	// It also makes the resulting ortho volume exactly the right thing to
	// cull against: one 6-plane test answers both "is this inside the
	// cascade" and "could this cast into the cascade".
	float zNear = radius;
	float zFar = radius * 3.0f;

	for (uint32_t i = 0; i < 8; i++) {
		glm::vec3 corner((i & 1) ? sceneBounds.max.x : sceneBounds.min.x,
		                 (i & 2) ? sceneBounds.max.y : sceneBounds.min.y,
		                 (i & 4) ? sceneBounds.max.z : sceneBounds.min.z);
		float dist = -(lightView * glm::vec4(corner, 1.0f)).z;
		zNear = glm::min(zNear, dist);
	}

	glm::mat4 proj =
	    glm::ortho(-radius, radius, -radius, radius, zNear, zFar);
	proj[1][1] *= -1.0f;

	return {lightView, proj, proj * lightView};
}

// -------------------------------------------------------
// Per-frame
// -------------------------------------------------------

void ShadowSystem::updateCascades(World& world,
                                  const CameraSystem& cameraSystem,
                                  float aspectRatio) {
	m_directionalCaster = NULL_ENTITY;
	for (Entity entity : world.view<Transform, Light>()) {
		const Light& light = world.getComponent<Light>(entity);
		if (light.type == LightType::Directional && light.castsShadow) {
			m_directionalCaster = entity;
			break;
		}
	}

	// No active camera means no view frustum to fit the cascades to. Clearing
	// the caster keeps the single exit path below - a zeroed upload with
	// hasDirectionalShadow == 0, same as a scene with no directional light.
	if (cameraSystem.getActiveCamera() == NULL_ENTITY) {
		m_directionalCaster = NULL_ENTITY;
	}

	// No geometry at all means nothing to fit the cascades to - m_sceneBounds
	// would be the inverted sentinel AABB and would poison the near plane.
	if (!m_sceneBounds.isValid()) {
		m_directionalCaster = NULL_ENTITY;
	}

	CascadeUBOData uboData{};

	if (m_directionalCaster != NULL_ENTITY) {
		const Transform& transform =
		    world.getComponent<Transform>(m_directionalCaster);
		glm::vec3 lightDirection = glm::normalize(glm::vec3(
		    transform.worldMatrix * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));

		Entity activeCamera = cameraSystem.getActiveCamera();
		const Camera& camera = world.getComponent<Camera>(activeCamera);
		glm::mat4 cameraView = cameraSystem.getViewMatrix(world);
		float fovRadians = glm::radians(camera.fov);

		auto splits =
		    computeSplitDepths(camera.nearPlane, CASCADE_SHADOW_DISTANCE);

		for (uint32_t i = 0; i < CASCADE_COUNT; i++) {
			auto corners = computeFrustumCornersWorldSpace(
			    cameraView, fovRadians, aspectRatio, splits[i], splits[i + 1]);

			m_cascadeMatrices[i] = computeCascadeViewProjection(
			    corners, m_sceneBounds, lightDirection, CASCADE_RESOLUTION);

			// The ortho volume is the cull volume - it already reaches back
			// toward the light far enough to cover off-slice occluders.
			m_cascadeFrusta[i] = Frustum::fromViewProjection(
			    m_cascadeMatrices[i].viewProjection);

			uboData.cascadeMatrices[i] = m_cascadeMatrices[i].viewProjection;
		}

		// splits has CASCADE_COUNT + 1 entries; this packing takes the far
		// distance of each cascade, which only fits a vec4 at three cascades.
		static_assert(CASCADE_COUNT == 3,
		              "splitDepths packing assumes 3 cascades");
		uboData.splitDepths = glm::vec4(splits[1], splits[2], splits[3], 0.0f);
		uboData.hasDirectionalShadow = 1;
	}

	memcpy(m_cascadeUBOMapped, &uboData, sizeof(CascadeUBOData));
}

void ShadowSystem::renderCascadeBlur(VkCommandBuffer cmd) {
	if (m_directionalCaster == NULL_ENTITY) return;

	struct BlurPushConstants {
		glm::vec2 texelDirection;
		glm::vec2 tileUVMin;
		glm::vec2 tileUVMax;
		float atlasSizeInv;
	};

	auto recordPass = [&](VkFramebuffer framebuffer, VkDescriptorSet source,
	                      glm::vec2 texelDirection) {
		VkRenderPassBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		beginInfo.renderPass = m_blurRenderPass;
		beginInfo.framebuffer = framebuffer;
		beginInfo.renderArea.extent = {CASCADE_RESOLUTION, CASCADE_RESOLUTION};
		beginInfo.clearValueCount = 0;

		vkCmdBeginRenderPass(cmd, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_blurPipeline);
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
		                        m_blurPipelineLayout, 0, 1, &source, 0,
		                        nullptr);

		VkViewport viewport{};
		viewport.width = static_cast<float>(CASCADE_RESOLUTION);
		viewport.height = static_cast<float>(CASCADE_RESOLUTION);
		viewport.minDepth = 0.0f;
		viewport.maxDepth = 1.0f;
		vkCmdSetViewport(cmd, 0, 1, &viewport);
		VkRect2D scissor{{0, 0}, {CASCADE_RESOLUTION, CASCADE_RESOLUTION}};
		vkCmdSetScissor(cmd, 0, 1, &scissor);

		BlurPushConstants push{};
		push.texelDirection = texelDirection;
		push.tileUVMin = glm::vec2(0.0f);
		push.tileUVMax = glm::vec2(1.0f);
		push.atlasSizeInv = 1.0f / static_cast<float>(CASCADE_RESOLUTION);
		vkCmdPushConstants(cmd, m_blurPipelineLayout,
		                   VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push),
		                   &push);

		vkCmdDraw(cmd, 3, 1, 0, 0);
		vkCmdEndRenderPass(cmd);
	};

	const float texel = 1.0f / static_cast<float>(CASCADE_RESOLUTION);
	for (uint32_t i = 0; i < CASCADE_COUNT; i++) {
		recordPass(m_cascades[i].blurScratchFramebuffer,
		           m_cascades[i].blurSetMoments, glm::vec2(texel, 0.0f));
		recordPass(m_cascades[i].blurResolvedFramebuffer,
		           m_cascades[i].blurSetScratch, glm::vec2(0.0f, texel));
	}
}