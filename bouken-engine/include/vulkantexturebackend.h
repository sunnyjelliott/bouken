#pragma once
#include "itexturebackend.h"

typedef struct VmaAllocation_T* VmaAllocation;
class VulkanContext;

struct VulkanTextureBindingData {
	VkImageView imageView;
	VkSampler sampler;
};

class VulkanTextureBackend : public ITextureBackend {
   public:
	void initialize(VulkanContext& context);
	void cleanup();

	BackendTextureHandle createTexture(const TextureCreateInfo& info) override;
	void destroyTexture(BackendTextureHandle handle) override;
	void* getBindingData(BackendTextureHandle handle) override;

   private:
	struct VulkanTexture {
		VkImage image = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		VkImageView imageView = VK_NULL_HANDLE;
		VkSampler sampler = VK_NULL_HANDLE;
		VulkanTextureBindingData bindingData;
	};

	VulkanContext* m_context = nullptr;
	std::unordered_map<BackendTextureHandle, VulkanTexture> m_textures;
	uint32_t m_nextHandle = 1;

	void createTextureImage(const TextureCreateInfo& info,
	                        VulkanTexture& texture);
	void createImageView(VulkanTexture& texture);
	void createSampler(VulkanTexture& texture);
};