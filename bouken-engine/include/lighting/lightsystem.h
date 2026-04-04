#pragma once
#include "light.h"
#include "pch.h"
#include "world.h"

typedef struct VmaAllocation_T* VmaAllocation;
typedef struct VmaAllocator_T* VmaAllocator;
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
	void initialize(VulkanContext& context, VmaAllocator allocator);
	void cleanup();

	// Gather Light+Transform components, build and upload GPULight array
	// Called once per frame before RenderSystem::drawFrame
	void update(World& world);

	// RenderSystem reads these to write the descriptor set
	VkBuffer getBuffer() const { return m_buffer; }
	VkDeviceSize getBufferSize() const { return m_bufferSize; }
	uint32_t getLightCount() const { return m_lightCount; }

   private:
	void createBuffer(uint32_t maxLights);

	VulkanContext* m_context = nullptr;
	VmaAllocator m_allocator = nullptr;

	VkBuffer m_buffer = VK_NULL_HANDLE;
	VmaAllocation m_allocation = VK_NULL_HANDLE;
	void* m_mapped = nullptr;
	VkDeviceSize m_bufferSize = 0;

	uint32_t m_lightCount = 0;

	// TODO: Dynamic resizing - currently fixed at initialization.
	// Exceeding MAX_LIGHTS silently drops lights with a logged warning.
	// Resize strategy: grow-only double-capacity recreate + descriptor
	// re-write.
	static constexpr uint32_t MAX_LIGHTS = 256;
};