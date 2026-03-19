#include "vulkantexturebackend.h"
#include <vk_mem_alloc.h>
#include "vulkancontext.h"

void VulkanTextureBackend::initialize(VulkanContext& context) {
	m_context = &context;
	std::cout << "VulkanTextureBackend initialized" << std::endl;
}

void VulkanTextureBackend::cleanup() {
	for (auto& [handle, texture] : m_textures) {
		vkDestroySampler(m_context->getDevice(), texture.sampler, nullptr);
		vkDestroyImageView(m_context->getDevice(), texture.imageView, nullptr);
		vmaDestroyImage(m_context->getAllocator(), texture.image,
		                texture.allocation);
	}
	m_textures.clear();
	std::cout << "VulkanTextureBackend cleaned up" << std::endl;
}

BackendTextureHandle VulkanTextureBackend::createTexture(
    const TextureCreateInfo& info) {
	VulkanTexture texture;

	createTextureImage(info, texture);
	createImageView(texture);
	createSampler(texture);

	texture.bindingData.imageView = texture.imageView;
	texture.bindingData.sampler = texture.sampler;

	BackendTextureHandle handle = reinterpret_cast<BackendTextureHandle>(
	    static_cast<uintptr_t>(m_nextHandle++));
	m_textures[handle] = std::move(texture);

	return handle;
}
void VulkanTextureBackend::destroyTexture(BackendTextureHandle handle) {
	auto it = m_textures.find(handle);
	if (it == m_textures.end()) {
		return;
	}

	VulkanTexture& texture = it->second;
	vkDestroySampler(m_context->getDevice(), texture.sampler, nullptr);
	vkDestroyImageView(m_context->getDevice(), texture.imageView, nullptr);
	vmaDestroyImage(m_context->getAllocator(), texture.image,
	                texture.allocation);

	m_textures.erase(it);
}

void* VulkanTextureBackend::getBindingData(BackendTextureHandle handle) {
	auto it = m_textures.find(handle);
	if (it == m_textures.end()) {
		return nullptr;
	}
	return &it->second.bindingData;
}

void VulkanTextureBackend::createTextureImage(const TextureCreateInfo& info,
                                              VulkanTexture& texture) {
	VkDeviceSize imageSize = info.width * info.height * info.channels;

	// Create staging buffer
	VkBuffer stagingBuffer;
	VmaAllocation stagingAllocation;

	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = imageSize;
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
	                  VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo allocResult;
	if (vmaCreateBuffer(m_context->getAllocator(), &bufferInfo, &allocInfo,
	                    &stagingBuffer, &stagingAllocation,
	                    &allocResult) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create staging buffer!");
	}

	// Copy pixel data
	memcpy(allocResult.pMappedData, info.pixels, imageSize);

	// Determine format based on channels
	VkFormat format =
	    (info.channels == 4) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8_SRGB;

	// Create image
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = info.width;
	imageInfo.extent.height = info.height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage =
	    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo imageAllocInfo{};
	imageAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	imageAllocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

	if (vmaCreateImage(m_context->getAllocator(), &imageInfo, &imageAllocInfo,
	                   &texture.image, &texture.allocation,
	                   nullptr) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create image!");
	}

	// Transition and copy
	VkCommandBuffer cmd = m_context->beginSingleTimeCommands();

	// Transition to TRANSFER_DST
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = texture.image;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.srcAccessMask = 0;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
	                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
	                     nullptr, 1, &barrier);

	// Copy buffer to image
	VkBufferImageCopy region{};
	region.bufferOffset = 0;
	region.bufferRowLength = 0;
	region.bufferImageHeight = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageOffset = {0, 0, 0};
	region.imageExtent = {info.width, info.height, 1};

	vkCmdCopyBufferToImage(cmd, stagingBuffer, texture.image,
	                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	// Transition to SHADER_READ_ONLY
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
	                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
	                     0, nullptr, 1, &barrier);

	m_context->endSingleTimeCommands(cmd);

	// Cleanup staging
	vmaDestroyBuffer(m_context->getAllocator(), stagingBuffer,
	                 stagingAllocation);
}

void VulkanTextureBackend::createImageView(VulkanTexture& texture) {
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = texture.image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	if (vkCreateImageView(m_context->getDevice(), &viewInfo, nullptr,
	                      &texture.imageView) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create texture image view!");
	}
}

void VulkanTextureBackend::createSampler(VulkanTexture& texture) {
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;
	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = 16.0f;
	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

	if (vkCreateSampler(m_context->getDevice(), &samplerInfo, nullptr,
	                    &texture.sampler) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create texture sampler!");
	}
}