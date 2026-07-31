#include <stb_image.h>
#include "lighting/iblsystem.h"

namespace bouken {

void IBLSystem::loadEquirectSource(const std::string& hdrPath) {
	int width = 0, height = 0, channels = 0;
	float* pixels =
	    stbi_loadf(hdrPath.c_str(), &width, &height, &channels, STBI_rgb_alpha);

	if (!pixels)
		throw std::runtime_error("IBLSystem: failed to load HDR file: " +
		                         hdrPath);

	const VkDeviceSize imageSize =
	    static_cast<VkDeviceSize>(width) * height * 4 * sizeof(float);

	// --- Staging buffer ---
	VkBuffer stagingBuffer{};
	VmaAllocation stagingAlloc{};

	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = imageSize;
	bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

	VmaAllocationCreateInfo stagingAllocInfo{};
	stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	stagingAllocInfo.flags =
	    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
	    VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo stagingInfo{};
	if (vmaCreateBuffer(m_context.getAllocator(), &bufferInfo,
	                    &stagingAllocInfo, &stagingBuffer, &stagingAlloc,
	                    &stagingInfo) != VK_SUCCESS) {
		stbi_image_free(pixels);
		throw std::runtime_error(
		    "IBLSystem: failed to create HDR staging buffer");
	}

	memcpy(stagingInfo.pMappedData, pixels, imageSize);
	stbi_image_free(pixels);

	// --- Destination image: single mip, sampled once by the equirect->cubemap
	// pass ---
	create2DImage(static_cast<uint32_t>(width), static_cast<uint32_t>(height),
	              1, VK_FORMAT_R32G32B32A32_SFLOAT,
	              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
	              m_equirectSource, m_equirectSourceAlloc,
	              m_equirectSourceView);
	nameImage(m_equirectSource, "ibl_equirect_source");
	nameImageView(m_equirectSourceView, "ibl_equirect_source_view");

	// --- Upload ---
	VkCommandBuffer cmd = m_context.beginSingleTimeCommands();

	VkImageMemoryBarrier toDst{};
	toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toDst.image = m_equirectSource;
	toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	toDst.srcAccessMask = 0;
	toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
	                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
	                     nullptr, 1, &toDst);

	VkBufferImageCopy region{};
	region.bufferOffset = 0;
	region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	region.imageSubresource.mipLevel = 0;
	region.imageSubresource.baseArrayLayer = 0;
	region.imageSubresource.layerCount = 1;
	region.imageExtent = {static_cast<uint32_t>(width),
	                      static_cast<uint32_t>(height), 1};

	vkCmdCopyBufferToImage(cmd, stagingBuffer, m_equirectSource,
	                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

	VkImageMemoryBarrier toShaderRead{};
	toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toShaderRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toShaderRead.image = m_equirectSource;
	toShaderRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	toShaderRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
	                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
	                     nullptr, 1, &toShaderRead);

	m_context.endSingleTimeCommands(cmd);

	vmaDestroyBuffer(m_context.getAllocator(), stagingBuffer, stagingAlloc);
}

void IBLSystem::generateMipmapsCube(VkImage cubemap, uint32_t resolution,
                                    uint32_t mipLevels, VkFormat format) {
	VkCommandBuffer cmd = m_context.beginSingleTimeCommands();

	// Mip 0 already arrives in TRANSFER_DST_OPTIMAL (left there by the
	// equirect->cubemap compute dispatch). Mips 1..N-1 are still UNDEFINED
	// from allocation and need transitioning before they can be blit
	// destinations.
	if (mipLevels > 1) {
		VkImageMemoryBarrier initBarrier{};
		initBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		initBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		initBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		initBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		initBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		initBarrier.image = cubemap;
		initBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 1,
		                                mipLevels - 1, 0, 6};
		initBarrier.srcAccessMask = 0;
		initBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
		                     nullptr, 1, &initBarrier);
	}

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = cubemap;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 6;  // all faces at once, every level

	int32_t mipWidth = static_cast<int32_t>(resolution);
	int32_t mipHeight = static_cast<int32_t>(resolution);

	for (uint32_t i = 1; i < mipLevels; i++) {
		// Transition level i-1 (all 6 faces) from DST to SRC
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.subresourceRange.levelCount = 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
		                     nullptr, 1, &barrier);

		// Blit all 6 faces from level i-1 to level i in one call
		// (layerCount 6 on both sides blits each face to its corresponding
		// face)
		VkImageBlit blit{};
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 6;
		blit.srcOffsets[0] = {0, 0, 0};
		blit.srcOffsets[1] = {mipWidth, mipHeight, 1};

		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 6;
		blit.dstOffsets[0] = {0, 0, 0};
		blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1,
		                      mipHeight > 1 ? mipHeight / 2 : 1, 1};

		vkCmdBlitImage(cmd, cubemap, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		               cubemap, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
		               VK_FILTER_LINEAR);

		// Transition level i-1 to SHADER_READ_ONLY - done with it
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
		                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
		                     nullptr, 0, nullptr, 1, &barrier);

		if (mipWidth > 1) mipWidth /= 2;
		if (mipHeight > 1) mipHeight /= 2;
	}

	// Transition the last mip level (all 6 faces) to SHADER_READ_ONLY -
	// it was left in TRANSFER_DST by the equirect dispatch's earlier barrier.
	// levelCount is set explicitly here rather than relying on the loop
	// above having run - if mipLevels == 1, that loop never executes and
	// levelCount would otherwise be left at its zero-initialized value,
	// producing an invalid (levelCount == 0) subresource range.
	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.subresourceRange.levelCount = 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
	                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
	                     nullptr, 1, &barrier);

	m_context.endSingleTimeCommands(cmd);
}

void IBLSystem::loadEnvironment(const std::string& hdrPath) {
	checkInitialized("loadEnvironment()");

	vkResetDescriptorPool(m_context.getDevice(), m_computeDescriptorPool, 0);
	destroyEnvironmentResources();  // no-op on first call, tears down prior env
	                                // on reload

	// --- Equirect source (persistent - kept for debug/visualization) ---
	loadEquirectSource(hdrPath);

	// --- Environment cubemap ---
	const uint32_t envMips =
	    mipLevelsForResolution(m_config.envCubemapResolution);
	createCubemapImage(
	    m_config.envCubemapResolution, envMips, VK_FORMAT_R16G16B16A16_SFLOAT,
	    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
	        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
	    m_envCubemap, m_envCubemapAlloc, m_envCubemapView);
	nameImage(m_envCubemap, "ibl_env_cubemap");
	nameImageView(m_envCubemapView, "ibl_env_cubemap_view");

	dispatchEquirectToCubemap(m_equirectSourceView);
	generateMipmapsCube(m_envCubemap, m_config.envCubemapResolution, envMips,
	                    VK_FORMAT_R16G16B16A16_SFLOAT);

	// --- SH irradiance ---
	dispatchSHProjection();

	// --- Prefiltered specular cubemap ---
	createCubemapImage(
	    m_config.prefilteredResolution, m_config.prefilteredMipLevels,
	    VK_FORMAT_R16G16B16A16_SFLOAT,
	    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
	    m_prefilteredEnv, m_prefilteredEnvAlloc, m_prefilteredEnvView);
	nameImage(m_prefilteredEnv, "ibl_prefiltered_env");
	nameImageView(m_prefilteredEnvView, "ibl_prefiltered_env_view");

	m_prefilteredMipViews.resize(m_config.prefilteredMipLevels, VK_NULL_HANDLE);

	for (uint32_t mip = 0; mip < m_config.prefilteredMipLevels; ++mip) {
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_prefilteredEnv;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = mip;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 6;

		if (vkCreateImageView(m_context.getDevice(), &viewInfo, nullptr,
		                      &m_prefilteredMipViews[mip]) != VK_SUCCESS) {
			throw std::runtime_error(
			    "IBLSystem: failed to create prefiltered mip view " +
			    std::to_string(mip));
		}
		nameImageView(m_prefilteredMipViews[mip],
		              ("ibl_prefiltered_mip_" + std::to_string(mip)).c_str());
	}

	dispatchPrefilter();

	m_ready = true;
}

void IBLSystem::destroyEnvironmentResources() {
	// Equirect source
	vkDestroyImageView(m_context.getDevice(), m_equirectSourceView, nullptr);
	if (m_equirectSource != VK_NULL_HANDLE)
		vmaDestroyImage(m_context.getAllocator(), m_equirectSource,
		                m_equirectSourceAlloc);
	m_equirectSourceView = VK_NULL_HANDLE;
	m_equirectSource = VK_NULL_HANDLE;
	m_equirectSourceAlloc = VK_NULL_HANDLE;

	// Env cubemap
	vkDestroyImageView(m_context.getDevice(), m_envCubemapView, nullptr);
	if (m_envCubemap != VK_NULL_HANDLE)
		vmaDestroyImage(m_context.getAllocator(), m_envCubemap,
		                m_envCubemapAlloc);
	m_envCubemapView = VK_NULL_HANDLE;
	m_envCubemap = VK_NULL_HANDLE;
	m_envCubemapAlloc = VK_NULL_HANDLE;

	// Prefiltered cubemap + its per-mip views
	for (auto& view : m_prefilteredMipViews) {
		vkDestroyImageView(m_context.getDevice(), view, nullptr);
	}
	m_prefilteredMipViews.clear();
	vkDestroyImageView(m_context.getDevice(), m_prefilteredEnvView, nullptr);
	if (m_prefilteredEnv != VK_NULL_HANDLE)
		vmaDestroyImage(m_context.getAllocator(), m_prefilteredEnv,
		                m_prefilteredEnvAlloc);
	m_prefilteredEnvView = VK_NULL_HANDLE;
	m_prefilteredEnv = VK_NULL_HANDLE;
	m_prefilteredEnvAlloc = VK_NULL_HANDLE;
}

}  // namespace bouken