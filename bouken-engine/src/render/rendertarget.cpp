#include "render/rendertarget.h"
#include "vulkancontext.h"

void RenderTarget::create(VulkanContext& context, VmaAllocator allocator,
                          const RenderTargetDesc& desc) {
	m_format = desc.format;
	m_width = desc.width;
	m_height = desc.height;

	// --- Image ---
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = desc.format;
	imageInfo.extent = {desc.width, desc.height, 1};
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = desc.usage;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

	if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &m_image,
	                   &m_allocation, nullptr) != VK_SUCCESS) {
		throw std::runtime_error("RenderTarget: failed to create image: " +
		                         std::string(desc.debugName));
	}

	// --- Debug label on the image ---
	if (!desc.debugName.empty()) {
		VkDebugUtilsObjectNameInfoEXT nameInfo{};
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
		nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_image);
		nameInfo.pObjectName = desc.debugName.data();
		// No-op if the extension isn't loaded - context guards this
		context.setDebugName(nameInfo);
	}

	// --- Image view ---
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = m_image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = desc.format;
	viewInfo.subresourceRange.aspectMask = desc.aspect;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	if (vkCreateImageView(context.getDevice(), &viewInfo, nullptr,
	                      &m_imageView) != VK_SUCCESS) {
		throw std::runtime_error("RenderTarget: failed to create image view: " +
		                         std::string(desc.debugName));
	}

	// --- Debug label on the image view ---
	if (!desc.debugName.empty()) {
		std::string viewName = std::string(desc.debugName) + "_view";
		VkDebugUtilsObjectNameInfoEXT nameInfo{};
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = VK_OBJECT_TYPE_IMAGE_VIEW;
		nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_imageView);
		nameInfo.pObjectName = viewName.c_str();
		context.setDebugName(nameInfo);
	}
}

void RenderTarget::destroy(VkDevice device, VmaAllocator allocator) {
	if (m_imageView != VK_NULL_HANDLE) {
		vkDestroyImageView(device, m_imageView, nullptr);
		m_imageView = VK_NULL_HANDLE;
	}
	if (m_image != VK_NULL_HANDLE) {
		vmaDestroyImage(allocator, m_image, m_allocation);
		m_image = VK_NULL_HANDLE;
		m_allocation = VK_NULL_HANDLE;
	}
}