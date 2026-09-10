#include "shadows/shadowsystem.h"

#include "boundingbox.h"
#include "light.h"
#include "lighting/lightsystem.h"
#include "render/rendersystem.h"
#include "transform.h"
#include "vulkancontext.h"
#include "world.h"

// -------------------------------------------------------
// ShadowSystem public interface.
//
// The private helpers live alongside the path they serve:
//   shadowsystem_resources.cpp   render passes, sampler, blur descriptors
//   shadowsystem_pipelines.cpp   moments and blur pipelines
//   shadowsystem_atlas.cpp       spot-light atlas
//   shadowsystem_cascades.cpp    directional-light cascades
// -------------------------------------------------------

// -------------------------------------------------------
// Lifecycle
// -------------------------------------------------------

void ShadowSystem::initialize(VulkanContext& context) {
	m_context = &context;

	computeTierLayout();

	createShadowTarget();
	createBlurTargets();
	createCascadeTargets();
	createCascadeUBO();
	createSampler();
	createBlurDescriptorResources();
	createCascadeBlurDescriptorSets();
	createRenderPass();
	createPipeline();
	createFramebuffer();
	createCascadeFramebuffers();

	createBlurRenderPass();
	createBlurFramebuffers();
	createCascadeBlurFramebuffers();
	createBlurPipeline();
	prepareBlurTargetLayouts();
}

void ShadowSystem::cleanup() {
	vkDestroyDescriptorPool(m_context->getDevice(), m_descriptorPool, nullptr);

	vkDestroyPipeline(m_context->getDevice(), m_pipeline, nullptr);
	vkDestroyPipelineLayout(m_context->getDevice(), m_pipelineLayout, nullptr);
	vkDestroyFramebuffer(m_context->getDevice(), m_framebuffer, nullptr);
	vkDestroyRenderPass(m_context->getDevice(), m_renderPass, nullptr);
	vkDestroySampler(m_context->getDevice(), m_sampler, nullptr);
	m_shadowMoments.destroy(m_context->getDevice(), m_context->getAllocator());
	m_shadowMomentsDepth.destroy(m_context->getDevice(),
	                             m_context->getAllocator());

	vkDestroyPipeline(m_context->getDevice(), m_blurPipeline, nullptr);
	vkDestroyPipelineLayout(m_context->getDevice(), m_blurPipelineLayout,
	                        nullptr);
	vkDestroyFramebuffer(m_context->getDevice(), m_blurScratchFramebuffer,
	                     nullptr);
	vkDestroyFramebuffer(m_context->getDevice(), m_blurResolvedFramebuffer,
	                     nullptr);
	vkDestroyRenderPass(m_context->getDevice(), m_blurRenderPass, nullptr);
	vkDestroyDescriptorSetLayout(m_context->getDevice(), m_blurSetLayout,
	                             nullptr);
	m_shadowBlurScratch.destroy(m_context->getDevice(),
	                            m_context->getAllocator());
	m_shadowResolved.destroy(m_context->getDevice(), m_context->getAllocator());

	for (uint32_t i = 0; i < CASCADE_COUNT; i++) {
		vkDestroyFramebuffer(m_context->getDevice(), m_cascades[i].framebuffer,
		                     nullptr);
		vkDestroyFramebuffer(m_context->getDevice(),
		                     m_cascades[i].blurScratchFramebuffer, nullptr);
		vkDestroyFramebuffer(m_context->getDevice(),
		                     m_cascades[i].blurResolvedFramebuffer, nullptr);

		m_cascades[i].moments.destroy(m_context->getDevice(),
		                              m_context->getAllocator());
		m_cascades[i].depth.destroy(m_context->getDevice(),
		                            m_context->getAllocator());
		m_cascades[i].blurScratch.destroy(m_context->getDevice(),
		                                  m_context->getAllocator());
		m_cascades[i].resolved.destroy(m_context->getDevice(),
		                               m_context->getAllocator());
	}

	vmaDestroyBuffer(m_context->getAllocator(), m_cascadeUBO,
	                 m_cascadeUBOAllocation);
}

// -------------------------------------------------------
// Per-frame
//
// Both entry points currently do the spot-atlas work inline; the cascade
// path (updateCascades, and the matching record step) hooks in here.
// -------------------------------------------------------

void ShadowSystem::collectCasters(World& world,
                                  const RenderSystem& renderSystem) {
	m_casters.clear();
	m_sceneBounds = AABB{};

	// Iterating the pool's own entity array, not world.view<>(): View scans
	// the entire 0..MAX_ENTITIES handle range with a type_index hash lookup
	// plus a per-pool hash lookup for every component at every step, so its
	// cost is fixed at ~65k * N lookups no matter how small the scene is.
	// Every shadow pass used to pay that separately - three cascade fits,
	// three cascade record loops, and one record loop per spot tile.
	// TransformSystem::update sets the precedent for going pool-first.
	const ComponentPool<MeshRenderer>* pool =
	    world.getComponentPool<MeshRenderer>();
	if (!pool) return;

	const std::vector<Entity>& entities = pool->getEntities();
	const std::vector<MeshRenderer>& renderers = pool->getComponents();

	for (size_t i = 0; i < entities.size(); i++) {
		const Entity entity = entities[i];
		const MeshRenderer& meshRenderer = renderers[i];

		if (!meshRenderer.visible) continue;
		if (!world.hasComponent<Transform>(entity)) continue;
		if (!world.hasComponent<BoundingBox>(entity)) continue;

		const AABB& bounds = world.getComponent<BoundingBox>(entity).aabb;
		if (!bounds.isValid()) continue;

		const glm::mat4& worldMatrix =
		    world.getComponent<Transform>(entity).worldMatrix;

		for (uint32_t meshID : meshRenderer.getMeshIDs()) {
			const RenderSystem::MeshInfo* mesh = renderSystem.getMeshInfo(meshID);
			if (!mesh) continue;

			m_casters.push_back({worldMatrix, bounds, mesh->indexCount,
			                     mesh->firstIndex, mesh->firstVertex});
		}

		m_sceneBounds = AABB::merge(m_sceneBounds, bounds);
	}
}

void ShadowSystem::update(World& world, LightSystem& lightSystem,
                          const CameraSystem& cameraSystem, float aspectRatio,
                          const RenderSystem& renderSystem) {
	collectCasters(world, renderSystem);

	m_activeCasters.clear();

	const glm::vec3 cameraPos =
	    glm::vec3(glm::inverse(cameraSystem.getViewMatrix(world))[3]);

	struct ScoredLight {
		Entity entity;
		float score;
	};
	std::vector<ScoredLight> candidates;

	for (Entity entity : world.view<Transform, Light>()) {
		const Light& light = world.getComponent<Light>(entity);
		if (!light.castsShadow) continue;

		// Step 3 scope: only spot lights have a light-space matrix function
		// today. Directional/point casters arrive in steps 4/5 - guarding
		// here rather than letting an untyped light silently misbehave.
		if (light.type != LightType::Spot) continue;

		const Transform& transform = world.getComponent<Transform>(entity);
		glm::vec3 lightPos = glm::vec3(transform.worldMatrix[3]);
		glm::vec3 diff = cameraPos - lightPos;
		float distSq =
		    glm::max(glm::dot(diff, diff), 0.01f);  // guards div-by-zero
		                                            // if a light sits at
		                                            // the camera position

		candidates.push_back({entity, light.intensity / distSq});
	}

	std::sort(candidates.begin(), candidates.end(),
	          [](const ScoredLight& a, const ScoredLight& b) {
		          return a.score > b.score;
	          });

	// Greedy assignment with overflow borrowing: each candidate, in score
	// order, takes the highest tier with a free slot.
	std::array<uint32_t, TIER_COUNT> tierNextSlot{};

	for (const ScoredLight& candidate : candidates) {
		for (uint32_t tier = 0; tier < TIER_COUNT; tier++) {
			if (tierNextSlot[tier] < m_tiers[tier].slotCount) {
				uint32_t slot = tierNextSlot[tier]++;
				m_activeCasters.push_back({candidate.entity, tier, slot,
				                           computeAtlasRegion(tier, slot)});
				break;
			}
			// tier full - falls through to try the next (lower) tier
		}
		// If every tier is full, this light gets no shadow this frame - an
		// accepted budget consequence for now (round-robin scheduling,
		// already roadmapped, is what actually addresses this later).
	}

	GPULight* lights = lightSystem.getLightsForWrite();
	uint32_t lightCount = lightSystem.getLightCount();

	for (ShadowSlotAssignment& assignment : m_activeCasters) {
		uint32_t index = lightSystem.getLightIndex(assignment.entity);
		if (index == UINT32_MAX || index >= lightCount) continue;

		const Transform& transform =
		    world.getComponent<Transform>(assignment.entity);
		const Light& light = world.getComponent<Light>(assignment.entity);
		assignment.matrices = computeSpotLightSpaceMatrix(transform, light);
		assignment.frustum =
		    Frustum::fromViewProjection(assignment.matrices.viewProjection);

		lights[index].castsShadow = 1;
		lights[index].lightSpaceMatrix = assignment.matrices.viewProjection;
		lights[index].shadowAtlasRegion = assignment.atlasRegion;
	}

	updateCascades(world, cameraSystem, aspectRatio);
}

// Shared by both record paths: push the caster's matrices and emit its draw.
static void recordCaster(VkCommandBuffer cmd, VkPipelineLayout layout,
                         const ShadowCaster& caster, const glm::mat4& view,
                         const glm::mat4& projection) {
	struct {
		glm::mat4 model;
		glm::mat4 view;
		glm::mat4 projection;
	} push;
	push.model = caster.worldMatrix;
	push.view = view;
	push.projection = projection;

	vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push),
	                   &push);
	vkCmdDrawIndexed(cmd, caster.indexCount, 1, caster.firstIndex,
	                 caster.firstVertex, 0);
}

void ShadowSystem::render(VkCommandBuffer commandBuffer,
                          RenderSystem& renderSystem) {
	// Both paths are independently optional: a scene can have spot casters,
	// a directional caster, both, or neither.
	if (m_activeCasters.empty() && m_directionalCaster == NULL_ENTITY) return;

	// Encoding of z = 1.0 (fully far) under the MOMENT_ENCODE basis in
	// msm_moments.frag - MOMENT_ENCODE * vec4(1) with the bias on .x. This is
	// the only valid "nothing here" value for a moment texture: a zero clear
	// decodes to a non-PSD moment vector and breaks the Cholesky solve in
	// momentShadow4MSM(). Covers the atlas gutter, unassigned tiles, and any
	// texel the caster's geometry never rasterized into - and the cascade
	// passes below, which clear on the same basis.
	std::array<VkClearValue, 2> clearValues{};
	clearValues[0].color = {1.0f, 0.99756f, 0.89344f, 0.0f};
	clearValues[1].depthStencil = {1.0f, 0};

	// Bound once for every pass below - vertex and index bindings are not
	// invalidated by render pass boundaries, and every shadow draw sources
	// the same two buffers.
	VkBuffer vertexBuffers[] = {renderSystem.getVertexBuffer()};
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
	vkCmdBindIndexBuffer(commandBuffer, renderSystem.getIndexBuffer(), 0,
	                     VK_INDEX_TYPE_UINT32);

	if (!m_activeCasters.empty()) {
		VkRenderPassBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		beginInfo.renderPass = m_renderPass;
		beginInfo.framebuffer = m_framebuffer;
		beginInfo.renderArea.extent = {ATLAS_SIZE, ATLAS_SIZE};
		beginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
		beginInfo.pClearValues = clearValues.data();

		vkCmdBeginRenderPass(commandBuffer, &beginInfo,
		                     VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		                  m_pipeline);

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
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			VkRect2D scissor{
			    {static_cast<int32_t>(tileX), static_cast<int32_t>(tileY)},
			    {tier.resolution, tier.resolution}};
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			for (const ShadowCaster& caster : m_casters) {
				if (!assignment.frustum.intersects(caster.bounds)) continue;

				recordCaster(commandBuffer, m_pipelineLayout, caster,
				             assignment.matrices.view,
				             assignment.matrices.projection);
			}
		}

		vkCmdEndRenderPass(commandBuffer);
	}

	if (m_directionalCaster != NULL_ENTITY) {
		for (uint32_t i = 0; i < CASCADE_COUNT; i++) {
			VkRenderPassBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			beginInfo.renderPass = m_renderPass;
			beginInfo.framebuffer = m_cascades[i].framebuffer;
			beginInfo.renderArea.extent = {CASCADE_RESOLUTION,
			                               CASCADE_RESOLUTION};
			beginInfo.clearValueCount =
			    static_cast<uint32_t>(clearValues.size());
			beginInfo.pClearValues = clearValues.data();  // reuses the atlas's
			                                              // encoded-far clear

			vkCmdBeginRenderPass(commandBuffer, &beginInfo,
			                     VK_SUBPASS_CONTENTS_INLINE);
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			                  m_pipeline);

			VkViewport viewport{};
			viewport.width = static_cast<float>(CASCADE_RESOLUTION);
			viewport.height = static_cast<float>(CASCADE_RESOLUTION);
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

			VkRect2D scissor{{0, 0}, {CASCADE_RESOLUTION, CASCADE_RESOLUTION}};
			vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

			// The per-cascade cull that makes the three maps actually differ.
			// m_cascadeFrusta[i] is the cascade's own ortho volume, already
			// extended toward the light, so this rejects both geometry
			// outside the slice and geometry that can't reach it.
			for (const ShadowCaster& caster : m_casters) {
				if (!m_cascadeFrusta[i].intersects(caster.bounds)) continue;

				recordCaster(commandBuffer, m_pipelineLayout, caster,
				             m_cascadeMatrices[i].view,
				             m_cascadeMatrices[i].projection);
			}

			vkCmdEndRenderPass(commandBuffer);
		}
	}

	// Each blur guards its own path: renderBlur() has no caster check of its
	// own (it used to rely on the early return above), renderCascadeBlur()
	// already tests m_directionalCaster.
	if (!m_activeCasters.empty()) renderBlur(commandBuffer);

	renderCascadeBlur(commandBuffer);
}
