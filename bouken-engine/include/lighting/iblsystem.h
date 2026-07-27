#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <string>

#include "devicebuffer.h"
#include "gpu/gpubuffer.h"
#include "hostbuffer.h"
#include "vulkancontext.h"

namespace bouken {

struct SHCoefficients {
	glm::vec4 c[9];
};

static_assert(sizeof(SHCoefficients) == 9 * sizeof(glm::vec4),
              "SHCoefficients must match std140 layout");

struct IBLConfig {
	uint32_t envCubemapResolution = 512;
	uint32_t prefilteredResolution = 128;
	uint32_t prefilteredMipLevels = 5;
	uint32_t brdfLutResolution = 512;
	uint32_t prefilterSampleCount = 512;
};

class IBLSystem {
   public:
	explicit IBLSystem(VulkanContext& context, const IBLConfig& config = {});

	IBLSystem(const IBLSystem&) = delete;
	IBLSystem& operator=(const IBLSystem&) = delete;

	void init();
	void cleanup();

	void loadEnvironment(const std::string& hdrPath);

	bool isReady() const { return m_ready; }
	bool isInitialized() const { return m_initialized; }

	VkImageView envCubemapView() const {
		return (m_envCubemap != VK_NULL_HANDLE) ? m_envCubemapView
		                                        : m_defaultCubemapView;
	}
	VkImageView prefilteredEnvView() const {
		return (m_prefilteredEnv != VK_NULL_HANDLE) ? m_prefilteredEnvView
		                                            : m_defaultCubemapView;
	}
	VkImageView brdfLutView() const { return m_brdfLutView; }
	VkSampler envSampler() const { return m_envSampler; }
	VkSampler prefilteredSampler() const { return m_prefilteredSampler; }
	VkSampler brdfLutSampler() const { return m_brdfLutSampler; }
	VkBuffer shCoeffBuffer() const { return m_shCoeffBuffer.buffer; }

   private:
	void dispatchEquirectToCubemap(VkImageView equirectView);
	void dispatchSHProjection();
	void dispatchPrefilter();
	void dispatchBRDFLut();
	void createCubemapImage(uint32_t resolution, uint32_t mipLevels,
	                        VkFormat format, VkImageUsageFlags usage,
	                        VkImage& outImage, VmaAllocation& outAllocation,
	                        VkImageView& outView);
	void create2DImage(uint32_t width, uint32_t height, uint32_t mipLevels,
	                   VkFormat format, VkImageUsageFlags usage,
	                   VkImage& outImage, VmaAllocation& outAllocation,
	                   VkImageView& outView);
	// U and V are separate because equirectangular sources need U to wrap
	// (longitude is periodic) while V must clamp at the poles.
	VkSampler createSampler(float maxLod, VkFilter filter,
	                        VkSamplerAddressMode addressModeU,
	                        VkSamplerAddressMode addressModeV);

	void loadEquirectSource(const std::string& hdrPath);
	void generateMipmapsCube(VkImage cubemap, uint32_t resolution,
	                         uint32_t mipLevels, VkFormat format);

	void createComputePipelines();
	void destroyComputePipelines();
	void destroyEnvironmentResources();

	VulkanContext& m_context;
	IBLConfig m_config;
	bool m_initialized = false;
	bool m_ready = false;

	void checkInitialized(const char* caller) const;
	void checkReady(const char* caller) const;

	static uint32_t mipLevelsForResolution(uint32_t resolution);

	void nameSampler(VkSampler sampler, const char* name);
	void nameImage(VkImage image, const char* name);
	void nameImageView(VkImageView view, const char* name);
	void nameBuffer(VkBuffer buffer, const char* name);

	// --- Compute pipelines ---
	VkPipeline m_equirectToCubemapPipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_equirectToCubemapLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_equirectToCubemapDSL = VK_NULL_HANDLE;

	VkPipeline m_shProjectPipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_shProjectLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_shProjectDSL = VK_NULL_HANDLE;

	VkPipeline m_prefilterPipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_prefilterLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_prefilterDSL = VK_NULL_HANDLE;

	VkPipeline m_brdfLutPipeline = VK_NULL_HANDLE;
	VkPipelineLayout m_brdfLutLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_brdfLutDSL = VK_NULL_HANDLE;

	VkDescriptorPool m_computeDescriptorPool = VK_NULL_HANDLE;

	// --- Per-environment GPU resources ---
	VkImage m_envCubemap = VK_NULL_HANDLE;
	VmaAllocation m_envCubemapAlloc = VK_NULL_HANDLE;
	VkImageView m_envCubemapView = VK_NULL_HANDLE;

	VkImage m_prefilteredEnv = VK_NULL_HANDLE;
	VmaAllocation m_prefilteredEnvAlloc = VK_NULL_HANDLE;
	VkImageView m_prefilteredEnvView = VK_NULL_HANDLE;

	VkImage m_equirectSource = VK_NULL_HANDLE;
	VmaAllocation m_equirectSourceAlloc = VK_NULL_HANDLE;
	VkImageView m_equirectSourceView = VK_NULL_HANDLE;

	std::array<VkImageView, 5> m_prefilteredMipViews = {};

	// --- Persistent resources ---
	VkImage m_defaultCubemap = VK_NULL_HANDLE;
	VmaAllocation m_defaultCubemapAlloc = VK_NULL_HANDLE;
	VkImageView m_defaultCubemapView = VK_NULL_HANDLE;

	VkImage m_brdfLut = VK_NULL_HANDLE;
	VmaAllocation m_brdfLutAlloc = VK_NULL_HANDLE;
	VkImageView m_brdfLutView = VK_NULL_HANDLE;

	DeviceBuffer m_shCoeffBuffer;

	// --- Samplers ---
	VkSampler m_envSampler = VK_NULL_HANDLE;
	VkSampler m_prefilteredSampler = VK_NULL_HANDLE;
	VkSampler m_brdfLutSampler = VK_NULL_HANDLE;
	// Equirect HDR source: U wraps at the atan seam, V clamps at the poles
	VkSampler m_equirectSampler = VK_NULL_HANDLE;
};

};  // namespace bouken