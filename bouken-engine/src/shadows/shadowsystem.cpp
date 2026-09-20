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

	createShadowAtlasTarget();
	createBlurTargets();
	createCascadeTargets();
	createCascadeUBO();
	createSampler();
	createBlurDescriptorResources();
	createCascadeBlurDescriptorSets();
	createRenderPass();
	createPipeline();
	createDPSMPipeline();
	createFramebuffer();
	createCascadeFramebuffers();

	createBlurRenderPass();
	createBlurFramebuffers();
	createCascadeBlurFramebuffers();
	createBlurPipeline();
	prepareBlurTargetLayouts();
}

void ShadowSystem::cleanup() {
	// Every handle is nulled after destruction so a stale one can never be
	// handed back to the driver a second time.
	vkDestroyDescriptorPool(m_context->getDevice(), m_descriptorPool, nullptr);
	m_descriptorPool = VK_NULL_HANDLE;

	vkDestroyPipeline(m_context->getDevice(), m_pipeline, nullptr);
	m_pipeline = VK_NULL_HANDLE;
	vkDestroyPipelineLayout(m_context->getDevice(), m_pipelineLayout, nullptr);
	m_pipelineLayout = VK_NULL_HANDLE;
	vkDestroyFramebuffer(m_context->getDevice(), m_framebuffer, nullptr);
	m_framebuffer = VK_NULL_HANDLE;
	vkDestroyRenderPass(m_context->getDevice(), m_renderPass, nullptr);
	m_renderPass = VK_NULL_HANDLE;
	vkDestroySampler(m_context->getDevice(), m_sampler, nullptr);
	m_sampler = VK_NULL_HANDLE;
	m_shadowMoments.destroy(m_context->getDevice(), m_context->getAllocator());
	m_shadowMomentsDepth.destroy(m_context->getDevice(),
	                             m_context->getAllocator());

	vkDestroyPipeline(m_context->getDevice(), m_dpsmPipeline, nullptr);
	m_dpsmPipeline = VK_NULL_HANDLE;
	vkDestroyPipelineLayout(m_context->getDevice(), m_dpsmPipelineLayout,
	                        nullptr);
	m_dpsmPipelineLayout = VK_NULL_HANDLE;

	vkDestroyPipeline(m_context->getDevice(), m_blurPipeline, nullptr);
	m_blurPipeline = VK_NULL_HANDLE;
	vkDestroyPipelineLayout(m_context->getDevice(), m_blurPipelineLayout,
	                        nullptr);
	m_blurPipelineLayout = VK_NULL_HANDLE;
	vkDestroyFramebuffer(m_context->getDevice(), m_blurScratchFramebuffer,
	                     nullptr);
	m_blurScratchFramebuffer = VK_NULL_HANDLE;
	vkDestroyFramebuffer(m_context->getDevice(), m_blurResolvedFramebuffer,
	                     nullptr);
	m_blurResolvedFramebuffer = VK_NULL_HANDLE;
	vkDestroyRenderPass(m_context->getDevice(), m_blurRenderPass, nullptr);
	m_blurRenderPass = VK_NULL_HANDLE;
	vkDestroyDescriptorSetLayout(m_context->getDevice(), m_blurSetLayout,
	                             nullptr);
	m_blurSetLayout = VK_NULL_HANDLE;
	m_shadowBlurScratch.destroy(m_context->getDevice(),
	                            m_context->getAllocator());
	m_shadowResolved.destroy(m_context->getDevice(), m_context->getAllocator());

	for (uint32_t i = 0; i < CASCADE_COUNT; i++) {
		vkDestroyFramebuffer(m_context->getDevice(), m_cascades[i].framebuffer,
		                     nullptr);
		m_cascades[i].framebuffer = VK_NULL_HANDLE;
		vkDestroyFramebuffer(m_context->getDevice(),
		                     m_cascades[i].blurScratchFramebuffer, nullptr);
		m_cascades[i].blurScratchFramebuffer = VK_NULL_HANDLE;
		vkDestroyFramebuffer(m_context->getDevice(),
		                     m_cascades[i].blurResolvedFramebuffer, nullptr);
		m_cascades[i].blurResolvedFramebuffer = VK_NULL_HANDLE;

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
	m_cascadeUBO = VK_NULL_HANDLE;
	m_cascadeUBOAllocation = VK_NULL_HANDLE;
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

	const ComponentPool<MeshRenderer>* pool =
	    world.getComponentPool<MeshRenderer>();
	if (!pool) return;

	// Pool pointers resolved once, not per access. World::hasComponent and
	// getComponent each locate the pool by type_index before touching it, so
	// the four calls this loop used to make cost four typeid hashes on top of
	// four entity hashes, per entity. Going through the pool directly - and
	// through tryGet, which folds the has()+get() pair into one lookup -
	// leaves one hash per component.
	const ComponentPool<Transform>* transformPool =
	    world.getComponentPool<Transform>();
	const ComponentPool<BoundingBox>* boundsPool =
	    world.getComponentPool<BoundingBox>();
	if (!transformPool || !boundsPool) return;

	const std::vector<Entity>& entities = pool->getEntities();
	const std::vector<MeshRenderer>& renderers = pool->getComponents();

	// One entry per submesh, so casters outnumber entities; the pool size is
	// the floor, not the target, but it stops the first frames reallocating.
	m_casters.reserve(entities.size());

	for (size_t i = 0; i < entities.size(); i++) {
		const Entity entity = entities[i];
		const MeshRenderer& meshRenderer = renderers[i];

		if (!meshRenderer.visible) continue;

		// Unlike World::hasComponent, this does not consult EntityManager for
		// liveness - an entity is here because it is in the MeshRenderer pool.
		// Nothing destroys entities yet; when something does, whatever removes
		// the component has to remove it from every pool, or a dead entity
		// keeps casting. TransformSystem::update already relies on the same.
		const BoundingBox* boundingBox = boundsPool->tryGet(entity);
		if (!boundingBox) continue;

		const AABB& bounds = boundingBox->world;
		if (!bounds.isValid()) continue;

		const Transform* transform = transformPool->tryGet(entity);
		if (!transform) continue;

		const glm::mat4& worldMatrix = transform->worldMatrix;

		for (uint32_t meshID : meshRenderer.getMeshIDs()) {
			const RenderSystem::MeshInfo* mesh =
			    renderSystem.getMeshInfo(meshID);
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

	const glm::mat4 cameraView = cameraSystem.getViewMatrix(world);
	const glm::vec3 cameraPos = glm::vec3(glm::inverse(cameraView)[3]);
	const Frustum cameraFrustum = Frustum::fromViewProjection(
	    cameraSystem.getProjectionMatrix(world, aspectRatio) * cameraView);

	struct ScoredLight {
		Entity entity;
		float score;
		bool isPoint;  // carried so the allocation loop needs no second lookup
	};
	std::vector<ScoredLight> candidates;

	// Iterated through the Light pool rather than world.view<Transform,
	// Light>(). View walks the whole 0..MAX_ENTITIES handle range with a hash
	// lookup per component per step, so scoring nine lights cost 65,535
	// iterations - a measured 0.49 ms/frame, independent of scene size.
	// collectCasters above set the precedent.
	//
	// Order changes from ascending-handle to pool-insertion. Nothing here
	// depends on it: the candidates are sorted by score immediately below.
	const ComponentPool<Light>* lightPool = world.getComponentPool<Light>();
	const ComponentPool<Transform>* lightTransforms =
	    world.getComponentPool<Transform>();

	const std::vector<Entity> emptyEntities;
	const std::vector<Light> emptyLights;
	const std::vector<Entity>& lightEntities =
	    lightPool ? lightPool->getEntities() : emptyEntities;
	const std::vector<Light>& lightComponents =
	    lightPool ? lightPool->getComponents() : emptyLights;

	candidates.reserve(lightEntities.size());

	for (size_t li = 0; lightTransforms && li < lightEntities.size(); li++) {
		const Entity entity = lightEntities[li];
		const Light& light = lightComponents[li];
		if (!light.castsShadow) continue;

		if (light.type == LightType::Directional) continue;

		// view<Transform, Light> used to guarantee the Transform; preserve it.
		const Transform* transformPtr = lightTransforms->tryGet(entity);
		if (!transformPtr) continue;
		const Transform& transform = *transformPtr;
		glm::vec3 lightPos = glm::vec3(transform.worldMatrix[3]);

		// lighting.frag's point and spot attenuation is a hard zero at
		// dist >= radius, so a light whose influence sphere misses the view
		// frustum cannot light - and therefore cannot shadow - any visible
		// pixel. Skipping it here leaves GPULight::castsShadow at 0, which is
		// the already-supported unshadowed path, not a new state. Conservative
		// for spots: their cone is strictly inside this sphere.
		if (!cameraFrustum.intersects(lightPos, light.radius)) continue;

		glm::vec3 diff = cameraPos - lightPos;
		float distSq = glm::max(glm::dot(diff, diff), 0.01f);

		candidates.push_back({entity, light.intensity / distSq,
		                      light.type == LightType::Point});
	}

	std::sort(candidates.begin(), candidates.end(),
	          [](const ScoredLight& a, const ScoredLight& b) {
		          return a.score > b.score;
	          });

	// Greedy assignment with overflow borrowing: each candidate, in score
	// order, takes the highest tier with a free slot.
	std::array<uint32_t, TIER_COUNT> tierNextSlot{};

	for (const ScoredLight& candidate : candidates) {
		const bool isPoint = candidate.isPoint;
		const uint32_t tileCount = isPoint ? 2 : 1;

		// Point lights start below the top tiers - see DPSM_MIN_TIER.
		const uint32_t firstTier = isPoint ? DPSM_MIN_TIER : 0;

		for (uint32_t tier = firstTier; tier < TIER_COUNT; tier++) {
			if (tierNextSlot[tier] + tileCount <= m_tiers[tier].slotCount) {
				ShadowSlotAssignment assignment{};
				assignment.entity = candidate.entity;
				assignment.representation =
				    isPoint ? ShadowRepresentation::DualParaboloid
				            : ShadowRepresentation::Perspective;
				assignment.tileCount = tileCount;

				for (uint32_t t = 0; t < tileCount; t++) {
					uint32_t slot = tierNextSlot[tier]++;
					assignment.tiles[t] = {tier, slot,
					                       computeAtlasRegion(tier, slot)};
				}

				m_activeCasters.push_back(assignment);
				break;
			}
			// Tier full for this light's tile requirement - falls through to
			// the next (lower) tier. A 2-tile point light skips a tier with
			// exactly 1 free slot rather than taking it and leaving the light
			// with a single hemisphere; a half-assignment isn't handled and
			// would need explicit rollback if it came up.
		}
	}

	GPULight* lights = lightSystem.getLightsForWrite();
	uint32_t lightCount = lightSystem.getLightCount();

	for (ShadowSlotAssignment& assignment : m_activeCasters) {
		uint32_t index = lightSystem.getLightIndex(assignment.entity);
		if (index == UINT32_MAX || index >= lightCount) continue;

		if (assignment.representation == ShadowRepresentation::DualParaboloid) {
			const Transform& transform =
			    *lightTransforms->tryGet(assignment.entity);
			const Light& light = lightPool->get(assignment.entity);

			assignment.dpsmView = computeDPSMViewMatrix(transform);
			// Same translation dpsmView was built from - cached so render()
			// doesn't have to invert the matrix back to recover it.
			assignment.dpsmLightPos = glm::vec3(transform.worldMatrix[3]);
			assignment.dpsmNear = POINT_SHADOW_NEAR;
			assignment.dpsmFar = glm::max(light.radius, POINT_SHADOW_FAR_MIN);

			lights[index].castsShadow = 1;
			lights[index].dpsmView = assignment.dpsmView;
			lights[index].dpsmAtlasRegionFront =
			    assignment.tiles[0].atlasRegion;
			lights[index].dpsmAtlasRegionBack = assignment.tiles[1].atlasRegion;
			lights[index].dpsmNear = assignment.dpsmNear;
			lights[index].dpsmFar = assignment.dpsmFar;
			continue;
		}

		const Transform& transform = *lightTransforms->tryGet(assignment.entity);
		const Light& light = lightPool->get(assignment.entity);
		assignment.matrices = computeSpotLightSpaceMatrix(transform, light);
		assignment.frustum =
		    Frustum::fromViewProjection(assignment.matrices.viewProjection);

		lights[index].castsShadow = 1;
		lights[index].lightSpaceMatrix = assignment.matrices.viewProjection;
		lights[index].shadowAtlasRegion = assignment.tiles[0].atlasRegion;
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

static void recordDPSMCaster(VkCommandBuffer cmd, VkPipelineLayout layout,
                             const ShadowCaster& caster, const glm::mat4& view,
                             float nearPlane, float farPlane,
                             float hemisphereSign) {
	DPSMPushConstants push{};
	push.model = caster.worldMatrix;
	push.view = view;
	push.nearPlane = nearPlane;
	push.farPlane = farPlane;
	push.hemisphereSign = hemisphereSign;

	vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push),
	                   &push);
	vkCmdDrawIndexed(cmd, caster.indexCount, 1, caster.firstIndex,
	                 caster.firstVertex, 0);
}

// Overlap test on the caster's whole bounds against the point light's
// hemisphere plane. tileIndex 0 = front (+Z in the corrected view convention
// above), 1 = back.
//
// Deliberately *not* an exclusive partition: a caster straddling the plane is
// handed to both tiles, and depth_dpsm.vert's gl_ClipDistance cuts each
// triangle at the plane so neither tile sees the other's half. Assigning a
// straddler whole to one side - which the center test used to do - leaves the
// other side with vertices past the paraboloid's pole, where the warp
// diverges and the triangle smears across the entire tile.
static bool casterInDPSMHemisphere(const glm::mat4& view, const AABB& bounds,
                                   uint32_t tileIndex) {
	const glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
	const glm::vec3 extent = (bounds.max - bounds.min) * 0.5f;

	// Third row of the view rotation - the world-space axis view-space z
	// measures along. Projecting the extent onto its absolute value gives the
	// bounds' half-width in z without transforming all eight corners.
	const glm::vec3 zAxis(view[0][2], view[1][2], view[2][2]);
	const float centerZ = (view * glm::vec4(center, 1.0f)).z;
	const float halfWidthZ = glm::dot(extent, glm::abs(zAxis));

	return tileIndex == 0 ? (centerZ + halfWidthZ >= 0.0f)
	                      : (centerZ - halfWidthZ <= 0.0f);
}

// An occluder farther from the light than its radius can only ever shadow
// receivers that are themselves out of range, so dropping it costs nothing -
// and it pays for the straddlers the test above now draws twice.
static bool casterWithinDPSMRange(const glm::vec3& lightPos,
                                  const AABB& bounds, float farPlane) {
	const glm::vec3 closest = glm::clamp(lightPos, bounds.min, bounds.max);
	return glm::distance(lightPos, closest) <= farPlane;
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
			if (assignment.representation != ShadowRepresentation::Perspective)
				continue;

			const ShadowTileSlot& tileSlot = assignment.tiles[0];
			const ShadowTier& tier = m_tiers[tileSlot.tier];
			uint32_t tileX = tileSlot.slot * tier.resolution;
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

		bool hasDPSMCasters = false;
		for (const ShadowSlotAssignment& assignment : m_activeCasters) {
			if (assignment.representation ==
			    ShadowRepresentation::DualParaboloid) {
				hasDPSMCasters = true;
				break;
			}
		}

		if (hasDPSMCasters) {
			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			                  m_dpsmPipeline);

			for (const ShadowSlotAssignment& assignment : m_activeCasters) {
				if (assignment.representation !=
				    ShadowRepresentation::DualParaboloid)
					continue;

				for (uint32_t hemisphere = 0; hemisphere < assignment.tileCount;
				     hemisphere++) {
					const ShadowTileSlot& tileSlot =
					    assignment.tiles[hemisphere];
					const ShadowTier& tier = m_tiers[tileSlot.tier];
					uint32_t tileX = tileSlot.slot * tier.resolution;
					uint32_t tileY = tier.yOffset;

					VkViewport viewport{};
					viewport.x = static_cast<float>(tileX);
					viewport.y = static_cast<float>(tileY);
					viewport.width = static_cast<float>(tier.resolution);
					viewport.height = static_cast<float>(tier.resolution);
					viewport.minDepth = 0.0f;
					viewport.maxDepth = 1.0f;
					vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

					VkRect2D scissor{{static_cast<int32_t>(tileX),
					                  static_cast<int32_t>(tileY)},
					                 {tier.resolution, tier.resolution}};
					vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

					// hemisphere 0 = front/+1 (depth_dpsm.vert's pole at
					// dir.z -> +1), 1 = back/-1 - matches
					// computePointLightViewMatrix's +Z convention.
					float hemisphereSign = (hemisphere == 0) ? 1.0f : -1.0f;

					for (const ShadowCaster& caster : m_casters) {
						if (!casterInDPSMHemisphere(assignment.dpsmView,
						                            caster.bounds, hemisphere))
							continue;
						if (!casterWithinDPSMRange(assignment.dpsmLightPos,
						                           caster.bounds,
						                           assignment.dpsmFar))
							continue;

						recordDPSMCaster(commandBuffer, m_dpsmPipelineLayout,
						                 caster, assignment.dpsmView,
						                 assignment.dpsmNear,
						                 assignment.dpsmFar, hemisphereSign);
					}
				}
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
