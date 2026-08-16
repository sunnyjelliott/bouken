#pragma once
#include "gpu/hostbuffer.h"
#include "light.h"
#include "pch.h"
#include "world.h"

class VulkanContext;

// GPU-side representation - std430 layout
// Must match LightData struct in lighting_frag.glsl
struct GPULight {
	glm::vec4 positionAndRadius;     // xyz = world pos, w = radius
	glm::vec4 colorAndIntensity;     // xyz = color, w = intensity
	glm::vec4 directionAndCosOuter;  // xyz = direction, w = cos(outerAngle)
	uint32_t type;
	float cosInner;  // precomputed cos(innerAngle) for spot falloff
	uint32_t castsShadow;
	float _pad;
	glm::mat4 lightSpaceMatrix;
	glm::vec4 shadowAtlasRegion;
};

class LightSystem {
   public:
	void initialize(VulkanContext& context);
	void cleanup();

	void update(World& world);

	VkBuffer getBuffer() const { return m_buffer.buffer; }
	VkDeviceSize getBufferSize() const { return m_buffer.capacity; }
	uint32_t getLightCount() const { return m_lightCount; }
	uint32_t getLightIndex(Entity entity) const;

	// Exposes the mapped buffer for ShadowSystem to patch shadow-specific
	// fields into after update() has written the base fields. Caller must
	// only touch lightSpaceMatrix/shadowAtlasRegion.
	GPULight* getLightsForWrite() {
		return reinterpret_cast<GPULight*>(
		    static_cast<uint8_t*>(m_buffer.mapped) + sizeof(glm::vec4));
	}

   private:
	VulkanContext* m_context = nullptr;
	HostBuffer m_buffer;
	uint32_t m_lightCount = 0;
	std::unordered_map<Entity, uint32_t> m_entityToIndex;

	// Initial light buffer reservation.
	// TODO: Select based on scene light budget or config.
	static constexpr uint32_t MAX_LIGHTS = 256;
};