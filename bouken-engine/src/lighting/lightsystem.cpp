#include "lighting/lightsystem.h"
#include <vk_mem_alloc.h>
#include "transform.h"
#include "vulkancontext.h"

void LightSystem::initialize(VulkanContext& context) {
	m_context = &context;

	const VkDeviceSize bufferSize =
	    sizeof(glm::vec4) + sizeof(GPULight) * MAX_LIGHTS;

	m_buffer.allocate(context, bufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

	std::cout << "LightSystem initialized (max lights: " << MAX_LIGHTS
	          << ", buffer: " << bufferSize << " bytes)" << std::endl;
}

void LightSystem::cleanup() { m_buffer.destroy(*m_context); }

void LightSystem::update(World& world) {
	m_lightCount = 0;

	// Buffer layout: [uint32 count][uint32 pad[3]][GPULight lights[MAX_LIGHTS]]
	uint32_t* countPtr = static_cast<uint32_t*>(m_buffer.mapped);
	GPULight* lightPtr = reinterpret_cast<GPULight*>(
	    static_cast<uint8_t*>(m_buffer.mapped) + sizeof(glm::vec4));

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
				gpu.directionAndCosOuter = glm::vec4(0.0f);
				gpu.cosInner = 0.0f;
				break;
			}
			case LightType::Directional: {
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

	countPtr[0] = m_lightCount;
	countPtr[1] = 0;
	countPtr[2] = 0;
	countPtr[3] = 0;
}