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
	float _pad[2];
};

class LightSystem {
   public:
	void initialize(VulkanContext& context);
	void cleanup();

	void update(World& world);

	VkBuffer getBuffer() const { return m_buffer.buffer; }
	VkDeviceSize getBufferSize() const { return m_buffer.capacity; }
	uint32_t getLightCount() const { return m_lightCount; }

   private:
	VulkanContext* m_context = nullptr;
	HostBuffer m_buffer;
	uint32_t m_lightCount = 0;

	// Initial light buffer reservation.
	// TODO: Select based on scene light budget or config.
	static constexpr uint32_t MAX_LIGHTS = 256;
};