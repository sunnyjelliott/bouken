#pragma once
#include "entity.h"
#include "light.h"
#include "pch.h"
#include "render/rendertarget.h"
#include "transform.h"

class VulkanContext;
class LightSystem;
class RenderSystem;
class World;

struct LightSpaceMatrices {
	glm::mat4 view;
	glm::mat4 projection;
	glm::mat4 viewProjection;  // fused - what GPULight::lightSpaceMatrix stores
};

class ShadowSystem {
   public:
	void initialize(VulkanContext& context);
	void cleanup();

	// Computes the light-space matrix for the current shadow caster and
	// patches it into LightSystem's GPULight buffer. Must run after
	// LightSystem::update() in the same frame.
	void update(World& world, LightSystem& lightSystem);

	// Records the depth-only shadow pass. Called by RenderSystem as part of
	// its own command buffer recording.
	void render(VkCommandBuffer cmd, World& world, RenderSystem& renderSystem);

	VkImageView getShadowMapView() const {
		return m_shadowTarget.getImageView();
	}
	VkSampler getShadowSampler() const { return m_sampler; }

   private:
	void createShadowTarget();
	void createRenderPass();
	void createPipeline();
	void createFramebuffer();
	void createSampler();

	LightSpaceMatrices computeSpotLightSpaceMatrix(const Transform& transform,
	                                               const Light& light) const;

	VulkanContext* m_context = nullptr;

	static constexpr uint32_t SHADOW_MAP_SIZE = 2048;  // single tile, step 1
	static constexpr float SHADOW_NEAR = 0.5f;
	static constexpr float SHADOW_FAR = 30.0f;

	RenderTarget m_shadowTarget;
	VkRenderPass m_renderPass = VK_NULL_HANDLE;
	VkFramebuffer m_framebuffer = VK_NULL_HANDLE;
	VkPipeline m_pipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkSampler m_sampler = VK_NULL_HANDLE;

	Entity m_shadowCaster = NULL_ENTITY;
};