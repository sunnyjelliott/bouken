#include "lighting/lightsystem.h"
#include <vk_mem_alloc.h>
#include "transform.h"
#include "vulkancontext.h"

void LightSystem::initialize(VulkanContext& context, VmaAllocator allocator) {
	m_context = &context;
	m_allocator = allocator;
	createBuffer(MAX_LIGHTS);
	std::cout << "LightSystem initialized (max lights: " << MAX_LIGHTS << ")"
	          << std::endl;
}

void LightSystem::cleanup() {
	if (m_buffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
		m_buffer = VK_NULL_HANDLE;
		m_allocation = VK_NULL_HANDLE;
		m_mapped = nullptr;
	}
}

void LightSystem::update(World& world) {
	m_lightCount = 0;

	// GPULight array starts after the 16-byte header (count + pad)
	// Buffer layout: [uint32 count][uint32 pad[3]][GPULight lights[MAX]]
	uint32_t* countPtr = static_cast<uint32_t*>(m_mapped);
	GPULight* lightPtr = reinterpret_cast<GPULight*>(
	    static_cast<uint8_t*>(m_mapped) + sizeof(glm::vec4));

	for (Entity entity : world.view<Transform, Light>()) {
		if (m_lightCount >= MAX_LIGHTS) {
			std::cerr << "LightSystem: MAX_LIGHTS (" << MAX_LIGHTS
			          << ") exceeded, ignoring remaining lights" << std::endl;
			break;
		}

		const Transform& transform = world.getComponent<Transform>(entity);
		const Light& light = world.getComponent<Light>(entity);

		GPULight gpu{};
		gpu.colorAndIntensity = glm::vec4(light.color, light.intensity);
		gpu.type = static_cast<uint32_t>(light.type);

		switch (light.type) {
			case LightType::Point: {
				gpu.positionAndRadius =
				    glm::vec4(transform.worldPosition(), light.radius);
				gpu.directionAndCosOuter = glm::vec4(0.0f);  // unused
				gpu.cosInner = 0.0f;                         // unused
				break;
			}
			case LightType::Directional: {
				// Extract forward vector from world rotation
				// Directional lights have no position — store direction in
				// positionAndRadius.xyz, w unused
				glm::vec3 dir = glm::normalize(
				    glm::vec3(transform.worldMatrix * glm::vec4(0, 0, -1, 0)));
				gpu.positionAndRadius = glm::vec4(dir, 0.0f);
				gpu.directionAndCosOuter = glm::vec4(dir, 0.0f);
				gpu.cosInner = 0.0f;
				break;
			}
			case LightType::Spot: {
				glm::vec3 dir = glm::normalize(
				    glm::vec3(transform.worldMatrix * glm::vec4(0, 0, -1, 0)));
				gpu.positionAndRadius =
				    glm::vec4(transform.worldPosition(), light.radius);
				gpu.directionAndCosOuter =
				    glm::vec4(dir, std::cos(glm::radians(light.outerAngle)));
				gpu.cosInner = std::cos(glm::radians(light.innerAngle));
				break;
			}
		}

		lightPtr[m_lightCount++] = gpu;
	}

	// Write count into buffer header
	countPtr[0] = m_lightCount;
	countPtr[1] = 0;
	countPtr[2] = 0;
	countPtr[3] = 0;
}

void LightSystem::createBuffer(uint32_t maxLights) {
	// Layout: vec4 header (count + pad) + GPULight array
	m_bufferSize = sizeof(glm::vec4) + sizeof(GPULight) * maxLights;

	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = m_bufferSize;
	bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
	                  VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo allocResult{};
	if (vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &m_buffer,
	                    &m_allocation, &allocResult) != VK_SUCCESS) {
		throw std::runtime_error("LightSystem: failed to create light buffer!");
	}

	m_mapped = allocResult.pMappedData;

	// Zero-initialise - count starts at 0
	memset(m_mapped, 0, m_bufferSize);

	std::cout << "  Light buffer created (" << m_bufferSize << " bytes, max "
	          << maxLights << " lights)" << std::endl;
}