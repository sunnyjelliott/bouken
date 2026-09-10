#pragma once
#include "frustum.h"
#include "pch.h"

#include "camerasystem.h"
#include "entity.h"
#include "light.h"
#include "render/rendertarget.h"
#include "transform.h"

class VulkanContext;
class LightSystem;
class RenderSystem;
class World;

// ---------------------------------------------------------------------------
// Types shared by both shadow paths
// ---------------------------------------------------------------------------

struct LightSpaceMatrices {
	glm::mat4 view;
	glm::mat4 projection;
	glm::mat4 viewProjection;  // fused - what GPULight::lightSpaceMatrix stores
};

// ---------------------------------------------------------------------------
// Spot-light shadow atlas
//
// One 4096x4096 atlas split into resolution tiers; each shadow-casting spot
// light is assigned a tile in the highest tier with a free slot.
// ---------------------------------------------------------------------------

static constexpr uint32_t ATLAS_SIZE = 4096;
static constexpr uint32_t TIER_COUNT = 4;
static constexpr std::array<uint32_t, TIER_COUNT> TIER_RESOLUTIONS = {
    2048, 1024, 512, 256};
static constexpr std::array<uint32_t, TIER_COUNT> TIER_SLOT_COUNTS = {2, 4, 8,
                                                                      16};

struct ShadowTier {
	uint32_t resolution;
	uint32_t slotCount;
	uint32_t yOffset;  // pixel offset of this tier's band within the atlas
};

struct ShadowSlotAssignment {
	Entity entity = NULL_ENTITY;
	uint32_t tier = 0;
	uint32_t slot = 0;
	glm::vec4 atlasRegion;  // xy = uv offset, zw = uv scale
	LightSpaceMatrices matrices;
	Frustum frustum;  // matrices.viewProjection, for draw-time caster culling
};

// ---------------------------------------------------------------------------
// Per-frame caster list
//
// One entry per submesh, gathered once in update() and reused by every shadow
// pass that frame - the spot atlas tiles and all three cascades. Draw
// arguments are resolved at collect time so the record loops never touch the
// ECS or RenderSystem's mesh map.
// ---------------------------------------------------------------------------

struct ShadowCaster {
	glm::mat4 worldMatrix;
	AABB bounds;  // world space - what the per-pass frustum test uses
	uint32_t indexCount = 0;
	uint32_t firstIndex = 0;
	uint32_t firstVertex = 0;
};

// ---------------------------------------------------------------------------
// Directional light cascades
// ---------------------------------------------------------------------------

static constexpr uint32_t CASCADE_COUNT = 3;
static constexpr uint32_t CASCADE_RESOLUTION = 2048;
static constexpr float CASCADE_SHADOW_DISTANCE = 50.0f;
static constexpr float CASCADE_SPLIT_LAMBDA = 0.6f;

struct CascadeTarget {
	RenderTarget moments;
	RenderTarget depth;
	RenderTarget blurScratch;
	RenderTarget resolved;                       // sampled by lighting.frag
	VkFramebuffer framebuffer = VK_NULL_HANDLE;  // moments + depth
	VkFramebuffer blurScratchFramebuffer = VK_NULL_HANDLE;
	VkFramebuffer blurResolvedFramebuffer = VK_NULL_HANDLE;
	VkDescriptorSet blurSetMoments = VK_NULL_HANDLE;
	VkDescriptorSet blurSetScratch = VK_NULL_HANDLE;
};

struct CascadeUBOData {
	glm::mat4 cascadeMatrices[CASCADE_COUNT];
	glm::vec4 splitDepths;  // .xyz = 3 split far-distances, .w unused
	uint32_t hasDirectionalShadow;
	float _pad[3];
};

// ---------------------------------------------------------------------------
// ShadowSystem
// ---------------------------------------------------------------------------

class ShadowSystem {
   public:
	// Lifecycle
	void initialize(VulkanContext& context);
	void cleanup();

	// Per-frame

	// Computes the light-space matrix for the current shadow caster and
	// patches it into LightSystem's GPULight buffer. Must run after
	// LightSystem::update() in the same frame.
	//
	// Also gathers the frame's caster list, which is why RenderSystem is
	// needed here - draw arguments are resolved once, at collect time.
	void update(World& world, LightSystem& lightSystem,
	            const CameraSystem& cameraSystem, float aspectRatio,
	            const RenderSystem& renderSystem);

	// Records the depth-only shadow pass. Called by RenderSystem as part of
	// its own command buffer recording. Takes no World: everything it draws
	// came out of the caster list update() already gathered.
	void render(VkCommandBuffer cmd, RenderSystem& renderSystem);

	// Accessors
	VkImageView getShadowMapView() const {
		return m_shadowResolved.getImageView();
	}
	VkSampler getShadowSampler() const { return m_sampler; }

	VkBuffer getCascadeUBO() const { return m_cascadeUBO; }

	VkImageView getCascadeResolvedView(uint32_t index) const {
		return m_cascades[index].resolved.getImageView();
	}

   private:
	// Per-frame caster gather - shadowsystem.cpp
	//
	// Iterates the MeshRenderer pool directly rather than world.view<>():
	// View walks the whole 0..MAX_ENTITIES handle range with a hash lookup
	// per component per step, so it costs the same on a 20-entity scene as a
	// 60000-entity one. TransformSystem::update sets the same precedent.
	void collectCasters(World& world, const RenderSystem& renderSystem);

	// Shared GPU objects - shadowsystem_resources.cpp
	void createRenderPass();
	void createBlurRenderPass();
	void createBlurDescriptorResources();
	void createSampler();

	// Shared pipelines - shadowsystem_pipelines.cpp
	void createPipeline();
	void createBlurPipeline();

	// Spot atlas - shadowsystem_atlas.cpp
	void computeTierLayout();
	glm::vec4 computeAtlasRegion(uint32_t tier, uint32_t slot) const;
	LightSpaceMatrices computeSpotLightSpaceMatrix(const Transform& transform,
	                                               const Light& light) const;

	void createShadowTarget();
	void createFramebuffer();
	void createBlurTargets();
	void createBlurFramebuffers();

	// One-time UNDEFINED -> SHADER_READ_ONLY_OPTIMAL transition for the blur
	// targets. render() early-returns when there are no casters, which would
	// otherwise leave m_shadowResolved in UNDEFINED while the lighting
	// descriptor set declares it SHADER_READ_ONLY_OPTIMAL.
	void prepareBlurTargetLayouts();

	// Two fullscreen passes: moments -> scratch (horizontal), scratch ->
	// resolved (vertical). Recorded right after the moments pass.
	void renderBlur(VkCommandBuffer cmd);

	// Directional cascades - shadowsystem_cascades.cpp
	void createCascadeUBO();
	void createCascadeTargets();
	void createCascadeFramebuffers();        // reuses m_renderPass
	void createCascadeBlurFramebuffers();    // reuses m_blurRenderPass
	void createCascadeBlurDescriptorSets();  // allocates from the same pool as
	                                         // the atlas's blur sets
	std::array<float, CASCADE_COUNT + 1> computeSplitDepths(
	    float nearPlane, float farPlane) const;
	std::array<glm::vec3, 8> computeFrustumCornersWorldSpace(
	    const glm::mat4& cameraView, float fovRadians, float aspect,
	    float splitNear, float splitFar) const;
	LightSpaceMatrices computeCascadeViewProjection(
	    const std::array<glm::vec3, 8>& corners, const AABB& sceneBounds,
	    const glm::vec3& lightDirection, uint32_t resolution) const;
	void updateCascades(World& world, const CameraSystem& cameraSystem,
	                    float aspectRatio);
	void renderCascadeBlur(VkCommandBuffer cmd);

	// Shared state
	VulkanContext* m_context = nullptr;

	// Rebuilt every frame by collectCasters(); consumed by both shadow paths.
	std::vector<ShadowCaster> m_casters;
	AABB m_sceneBounds;  // union of m_casters bounds - feeds the cascade
	                     // near-plane pull-back

	VkRenderPass m_renderPass = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkSampler m_sampler = VK_NULL_HANDLE;

	VkRenderPass m_blurRenderPass =
	    VK_NULL_HANDLE;  // shared by both directions
	VkPipeline m_blurPipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_blurPipelineLayout = VK_NULL_HANDLE;

	VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_blurSetLayout = VK_NULL_HANDLE;

	// Spot atlas state
	static constexpr uint32_t SHADOW_MAP_SIZE = 2048;  // single tile, step 1
	static constexpr float SHADOW_NEAR = 0.5f;
	static constexpr float SHADOW_FAR = 30.0f;

	std::array<ShadowTier, TIER_COUNT> m_tiers{};
	std::vector<ShadowSlotAssignment> m_activeCasters;

	RenderTarget m_shadowMoments;
	RenderTarget m_shadowMomentsDepth;
	VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
	RenderTarget m_shadowBlurScratch;  // RGBA16F, horizontal-blur intermediate
	RenderTarget m_shadowResolved;     // RGBA16F, final blurred moments - what
	                                   // lighting.frag samples

	VkFramebuffer m_blurScratchFramebuffer = VK_NULL_HANDLE;
	VkFramebuffer m_blurResolvedFramebuffer = VK_NULL_HANDLE;

	VkDescriptorSet m_blurSetMoments =
	    VK_NULL_HANDLE;  // samples m_shadowMoments (horizontal pass input)
	VkDescriptorSet m_blurSetScratch =
	    VK_NULL_HANDLE;  // samples m_shadowBlurScratch (vertical pass input)

	// Directional cascade state
	std::array<CascadeTarget, CASCADE_COUNT> m_cascades;
	Entity m_directionalCaster = NULL_ENTITY;
	std::array<LightSpaceMatrices, CASCADE_COUNT>
	    m_cascadeMatrices;  // for render()'s push constants

	// Ortho volume of each cascade, for draw-time culling. Already extended
	// toward the light by computeCascadeViewProjection's near-plane pull-back,
	// so a plain 6-plane test also catches off-slice occluders.
	std::array<Frustum, CASCADE_COUNT> m_cascadeFrusta;

	VkBuffer m_cascadeUBO = VK_NULL_HANDLE;
	VmaAllocation m_cascadeUBOAllocation = VK_NULL_HANDLE;
	void* m_cascadeUBOMapped = nullptr;
};
