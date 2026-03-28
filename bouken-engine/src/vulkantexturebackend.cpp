#include "vulkantexturebackend.h"
#include <vk_mem_alloc.h>
#include "vulkancontext.h"

void VulkanTextureBackend::initialize(VulkanContext& context) {
	m_context = &context;
	createDefaultTextures();
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

	// Calculate mip count
	const uint32_t mipLevels =
	    info.generateMipmaps
	        ? static_cast<uint32_t>(
	              std::floor(std::log2(std::max(info.width, info.height)))) +
	              1
	        : 1;
	texture.mipLevels = mipLevels;

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

	// Determine format based on channels and color space
	VkFormat format;
	if (info.channels == 4) {
		format = info.sRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
	} else {
		format = info.sRGB ? VK_FORMAT_R8G8B8_SRGB : VK_FORMAT_R8G8B8_UNORM;
	}

	// Create image
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = info.width;
	imageInfo.extent.height = info.height;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = 1;
	imageInfo.format = format;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
	                  VK_IMAGE_USAGE_TRANSFER_DST_BIT |
	                  VK_IMAGE_USAGE_SAMPLED_BIT;
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
	texture.format = format;

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
	barrier.subresourceRange.levelCount = mipLevels;
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

	if (mipLevels > 1) {
		// Blit chain — each level blits from the previous
		// After each blit, transition the source level to SHADER_READ_ONLY
		int32_t mipWidth = static_cast<int32_t>(info.width);
		int32_t mipHeight = static_cast<int32_t>(info.height);

		for (uint32_t i = 1; i < mipLevels; i++) {
			// Transition level i-1 from TRANSFER_DST to TRANSFER_SRC
			barrier.subresourceRange.baseMipLevel = i - 1;
			barrier.subresourceRange.levelCount = 1;
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr,
			                     0, nullptr, 1, &barrier);

			// Blit from level i-1 to level i
			VkImageBlit blit{};
			blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.srcSubresource.mipLevel = i - 1;
			blit.srcSubresource.baseArrayLayer = 0;
			blit.srcSubresource.layerCount = 1;
			blit.srcOffsets[0] = {0, 0, 0};
			blit.srcOffsets[1] = {mipWidth, mipHeight, 1};

			blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			blit.dstSubresource.mipLevel = i;
			blit.dstSubresource.baseArrayLayer = 0;
			blit.dstSubresource.layerCount = 1;
			blit.dstOffsets[0] = {0, 0, 0};
			blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1,
			                      mipHeight > 1 ? mipHeight / 2 : 1, 1};

			vkCmdBlitImage(cmd, texture.image,
			               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture.image,
			               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
			               VK_FILTER_LINEAR);

			// Transition level i-1 to SHADER_READ_ONLY — done with it
			barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

			vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
			                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
			                     nullptr, 0, nullptr, 1, &barrier);

			// Halve dimensions for next level — clamp to 1
			if (mipWidth > 1) mipWidth /= 2;
			if (mipHeight > 1) mipHeight /= 2;
		}

		// Transition the last mip level to SHADER_READ_ONLY
		// (it was left in TRANSFER_DST by the initial transition)
		barrier.subresourceRange.baseMipLevel = mipLevels - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
		                     nullptr, 0, nullptr, 1, &barrier);

	} else {
		// No mipmaps — single transition to SHADER_READ_ONLY
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
		                     nullptr, 0, nullptr, 1, &barrier);
	}

	m_context->endSingleTimeCommands(cmd);
	vmaDestroyBuffer(m_context->getAllocator(), stagingBuffer,
	                 stagingAllocation);
}

void VulkanTextureBackend::createImageView(VulkanTexture& texture) {
	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = texture.image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = texture.format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = texture.mipLevels;
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
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = static_cast<float>(texture.mipLevels);

	if (vkCreateSampler(m_context->getDevice(), &samplerInfo, nullptr,
	                    &texture.sampler) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create texture sampler!");
	}
}

void VulkanTextureBackend::createDefaultTextures() {
	// Create 1x1 white texture
	uint8_t whitePixel[4] = {255, 255, 255, 255};
	TextureCreateInfo whiteInfo;
	whiteInfo.pixels = whitePixel;
	whiteInfo.width = 1;
	whiteInfo.height = 1;
	whiteInfo.channels = 4;
	whiteInfo.generateMipmaps = false;
	m_defaultWhiteTexture = createTexture(whiteInfo);
	std::cout << "  Created default white texture" << std::endl;

	// Create 1x1 flat normal map (128, 128, 255 = pointing up in tangent space)
	uint8_t normalPixel[4] = {128, 128, 255, 255};
	TextureCreateInfo normalInfo;
	normalInfo.pixels = normalPixel;
	normalInfo.width = 1;
	normalInfo.height = 1;
	normalInfo.channels = 4;
	normalInfo.generateMipmaps = false;
	normalInfo.sRGB = false;
	m_defaultNormalTexture = createTexture(normalInfo);
	std::cout << "  Created default normal texture" << std::endl;
}