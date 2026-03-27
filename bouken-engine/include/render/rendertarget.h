#pragma once
#include <vk_mem_alloc.h>
#include "pch.h"

class VulkanContext;

struct RenderTargetDesc {
	uint32_t width;
	uint32_t height;
	VkFormat format;
	VkImageUsageFlags usage;
	VkImageAspectFlags aspect;
	std::string_view debugName;
};

class RenderTarget {
   public:
	RenderTarget() = default;
	~RenderTarget() = default;

	RenderTarget(const RenderTarget&) = delete;
	RenderTarget& operator=(const RenderTarget&) = delete;
	RenderTarget(RenderTarget&&) noexcept = default;
	RenderTarget& operator=(RenderTarget&&) noexcept = default;

	void create(VulkanContext& context, VmaAllocator allocator,
	            const RenderTargetDesc& desc);
	void destroy(VkDevice device, VmaAllocator allocator);

	VkImage getImage() const { return m_image; }
	VkImageView getImageView() const { return m_imageView; }
	VkFormat getFormat() const { return m_format; }
	uint32_t getWidth() const { return m_width; }
	uint32_t getHeight() const { return m_height; }

   private:
	VkImage m_image = VK_NULL_HANDLE;
	VkImageView m_imageView = VK_NULL_HANDLE;
	VmaAllocation m_allocation = VK_NULL_HANDLE;

	VkFormat m_format = VK_FORMAT_UNDEFINED;
	uint32_t m_width = 0;
	uint32_t m_height = 0;
};