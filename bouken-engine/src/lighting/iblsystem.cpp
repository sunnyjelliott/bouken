#include "iblsystem.h"

bouken::IBLSystem::IBLSystem(VulkanContext& context, const IBLConfig& config)
    : m_context(context), m_config(config) {}

void bouken::IBLSystem::init() {
	if (m_initialized)
		throw std::runtime_error("IBLSystem: init() called more than once");

	// 1. Compute pipelines, DSLs, transient descriptor pool
	createComputePipelines();

	// 2. Persistent samplers
	const uint32_t envMaxMip =
	    mipLevelsForResolution(m_config.envCubemapResolution) - 1;
	m_envSampler =
	    createSampler(static_cast<float>(envMaxMip), VK_FILTER_LINEAR,
	                  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	                  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

	m_prefilteredSampler =
	    createSampler(static_cast<float>(m_config.prefilteredMipLevels - 1),
	                  VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	                  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

	m_brdfLutSampler = createSampler(0.0f, VK_FILTER_LINEAR,
	                                 VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
	                                 VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

	// Equirect source: longitude is periodic, so U must REPEAT or the atan
	// seam in dirToEquirect() shows as a vertical seam in the env cubemap.
	// Latitude is not periodic - V clamps at the poles.
	m_equirectSampler = createSampler(0.0f, VK_FILTER_LINEAR,
	                                  VK_SAMPLER_ADDRESS_MODE_REPEAT,
	                                  VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

	nameSampler(m_envSampler, "ibl_env_sampler");
	nameSampler(m_prefilteredSampler, "ibl_prefiltered_sampler");
	nameSampler(m_brdfLutSampler, "ibl_brdf_lut_sampler");
	nameSampler(m_equirectSampler, "ibl_equirect_sampler");

	// 3. BRDF LUT image
	create2DImage(m_config.brdfLutResolution, m_config.brdfLutResolution, 1,
	              VK_FORMAT_R16G16_SFLOAT,
	              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
	              m_brdfLut, m_brdfLutAlloc, m_brdfLutView);
	nameImage(m_brdfLut, "ibl_brdf_lut");
	nameImageView(m_brdfLutView, "ibl_brdf_lut_view");

	// 4. SH coefficient buffer - GPU-only, written by compute (storage),
	//    read by lighting shader (uniform). Fixed size, allocated once.
	m_shCoeffBuffer.allocate(m_context, sizeof(SHCoefficients),
	                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
	                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
	nameBuffer(m_shCoeffBuffer.buffer, "ibl_sh_coefficients");

	// 5. BRDF LUT is view/environment independent - dispatch once, now
	dispatchBRDFLut();

	m_initialized = true;
}

void bouken::IBLSystem::cleanup() {
	if (!m_initialized)
		return;  // nothing was created - safe no-op, not an error

	// Per-environment resources (safe to call even if no environment was
	// ever loaded - destroyEnvironmentResources() guards on VK_NULL_HANDLE)
	destroyEnvironmentResources();

	// Persistent resources created in init()
	m_shCoeffBuffer.destroy(m_context);

	vkDestroyImageView(m_context.getDevice(), m_brdfLutView, nullptr);
	vmaDestroyImage(m_context.getAllocator(), m_brdfLut, m_brdfLutAlloc);
	m_brdfLutView = VK_NULL_HANDLE;
	m_brdfLut = VK_NULL_HANDLE;

	vkDestroySampler(m_context.getDevice(), m_envSampler, nullptr);
	vkDestroySampler(m_context.getDevice(), m_prefilteredSampler, nullptr);
	vkDestroySampler(m_context.getDevice(), m_brdfLutSampler, nullptr);
	vkDestroySampler(m_context.getDevice(), m_equirectSampler, nullptr);
	m_envSampler = VK_NULL_HANDLE;
	m_prefilteredSampler = VK_NULL_HANDLE;
	m_brdfLutSampler = VK_NULL_HANDLE;
	m_equirectSampler = VK_NULL_HANDLE;

	// Compute pipelines, layouts, DSLs, transient descriptor pool
	destroyComputePipelines();

	m_initialized = false;
}

void bouken::IBLSystem::dispatchEquirectToCubemap(VkImageView equirectView) {
	VkCommandBuffer cmd = m_context.beginSingleTimeCommands();

	// --- Transition cubemap to GENERAL for storage write ---
	VkImageMemoryBarrier toGeneral{};
	toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toGeneral.image = m_envCubemap;
	toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};
	toGeneral.srcAccessMask = 0;
	toGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
	                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
	                     nullptr, 1, &toGeneral);

	// --- Build per-face storage image views (2D_ARRAY, one layer each) ---
	std::array<VkImageView, 6> faceViews{};
	for (uint32_t face = 0; face < 6; ++face) {
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_envCubemap;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
		viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = face;
		viewInfo.subresourceRange.layerCount = 1;
		vkCreateImageView(m_context.getDevice(), &viewInfo, nullptr,
		                  &faceViews[face]);
	}

	// --- Allocate and write descriptor set ---
	VkDescriptorSet ds = m_context.allocateDescriptorSet(
	    m_computeDescriptorPool, m_equirectToCubemapDSL);

	VkDescriptorImageInfo equirectInfo{};
	equirectInfo.sampler = m_equirectSampler;
	equirectInfo.imageView = equirectView;
	equirectInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	// imageCube storage: all 6 faces as a single 2D_ARRAY view covering all
	// layers
	VkImageView cubemapStorageView = VK_NULL_HANDLE;
	{
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_envCubemap;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		viewInfo.format = VK_FORMAT_R16G16B16A16_SFLOAT;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 6;
		vkCreateImageView(m_context.getDevice(), &viewInfo, nullptr,
		                  &cubemapStorageView);
	}

	VkDescriptorImageInfo cubemapInfo{};
	cubemapInfo.imageView = cubemapStorageView;
	cubemapInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	std::array<VkWriteDescriptorSet, 2> writes{};
	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstSet = ds;
	writes[0].dstBinding = 0;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[0].descriptorCount = 1;
	writes[0].pImageInfo = &equirectInfo;

	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstSet = ds;
	writes[1].dstBinding = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	writes[1].descriptorCount = 1;
	writes[1].pImageInfo = &cubemapInfo;

	vkUpdateDescriptorSets(m_context.getDevice(), 2, writes.data(), 0, nullptr);

	// --- Dispatch ---
	struct PushConstants {
		uint32_t faceSize;
	} pushConst{m_config.envCubemapResolution};

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
	                  m_equirectToCubemapPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
	                        m_equirectToCubemapLayout, 0, 1, &ds, 0, nullptr);
	vkCmdPushConstants(cmd, m_equirectToCubemapLayout,
	                   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConst),
	                   &pushConst);

	// z = 6: one workgroup layer per face
	uint32_t groups = (m_config.envCubemapResolution + 15) / 16;
	vkCmdDispatch(cmd, groups, groups, 6);

	// --- Transition mip 0 (all 6 faces) from GENERAL to TRANSFER_DST ---
	// generateMipmapsCube() picks up from here to build the rest of the
	// mip chain via blit, and owns the final SHADER_READ_ONLY transition.
	VkImageMemoryBarrier toTransferDst{};
	toTransferDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toTransferDst.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	toTransferDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	toTransferDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toTransferDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toTransferDst.image = m_envCubemap;
	toTransferDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6};
	toTransferDst.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	toTransferDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	                     VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
	                     nullptr, 1, &toTransferDst);

	m_context.endSingleTimeCommands(cmd);

	// --- Cleanup transient views ---
	vkDestroyImageView(m_context.getDevice(), cubemapStorageView, nullptr);
	for (auto& v : faceViews)
		vkDestroyImageView(m_context.getDevice(), v, nullptr);
}

void bouken::IBLSystem::dispatchSHProjection() {
	const uint32_t faceSize = m_config.envCubemapResolution;
	const uint32_t groupsXY = (faceSize + 15) / 16;
	const uint32_t numWorkgroups = groupsXY * groupsXY * 6;

	// Intermediate buffer: numWorkgroups × 9 coefficients × vec4
	const VkDeviceSize partialSize = numWorkgroups * 9 * sizeof(glm::vec4);

	// Transient device-local buffer for partial sums - allocated and freed per
	// call.
	// TODO: worth making persistent on IBLSystem if profiling shows allocation
	// cost.
	VkBuffer partialBuf{};
	VmaAllocation partialAlloc{};
	{
		VkBufferCreateInfo bufInfo{};
		bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufInfo.size = partialSize;
		bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

		vmaCreateBuffer(m_context.getAllocator(), &bufInfo, &allocInfo,
		                &partialBuf, &partialAlloc, nullptr);
	}

	VkCommandBuffer cmd = m_context.beginSingleTimeCommands();

	VkDescriptorSet ds = m_context.allocateDescriptorSet(
	    m_computeDescriptorPool, m_shProjectDSL);

	VkDescriptorImageInfo envInfo{};
	envInfo.sampler = m_envSampler;
	envInfo.imageView = m_envCubemapView;
	envInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkDescriptorBufferInfo partialInfo{};
	partialInfo.buffer = partialBuf;
	partialInfo.offset = 0;
	partialInfo.range = partialSize;

	VkDescriptorBufferInfo shOutInfo{};
	shOutInfo.buffer = m_shCoeffBuffer.buffer;
	shOutInfo.offset = 0;
	shOutInfo.range = sizeof(SHCoefficients);

	std::array<VkWriteDescriptorSet, 3> writes{};
	writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[0].dstSet = ds;
	writes[0].dstBinding = 0;
	writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[0].descriptorCount = 1;
	writes[0].pImageInfo = &envInfo;

	writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[1].dstSet = ds;
	writes[1].dstBinding = 1;
	writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[1].descriptorCount = 1;
	writes[1].pBufferInfo = &partialInfo;

	writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[2].dstSet = ds;
	writes[2].dstBinding = 2;
	writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	writes[2].descriptorCount = 1;
	writes[2].pBufferInfo = &shOutInfo;

	vkUpdateDescriptorSets(m_context.getDevice(), 3, writes.data(), 0, nullptr);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_shProjectPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
	                        m_shProjectLayout, 0, 1, &ds, 0, nullptr);

	struct PushConstants {
		uint32_t pass;
		uint32_t faceSize;
		uint32_t numWorkgroups;
	};

	// --- Pass 1 ---
	PushConstants pc1{0, faceSize, numWorkgroups};
	vkCmdPushConstants(cmd, m_shProjectLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
	                   sizeof(pc1), &pc1);
	vkCmdDispatch(cmd, groupsXY, groupsXY, 6);

	// Barrier between passes: storage write → storage read
	VkBufferMemoryBarrier passBarrier{};
	passBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	passBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	passBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	passBarrier.buffer = partialBuf;
	passBarrier.offset = 0;
	passBarrier.size = partialSize;

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 1,
	                     &passBarrier, 0, nullptr);

	// --- Pass 2: single workgroup reduction ---
	// Dispatch exactly 1 workgroup; localIdx walks the partial sum array
	PushConstants pc2{1, faceSize, numWorkgroups};
	vkCmdPushConstants(cmd, m_shProjectLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
	                   sizeof(pc2), &pc2);
	vkCmdDispatch(cmd, 1, 1, 1);

	m_context.endSingleTimeCommands(cmd);

	vmaDestroyBuffer(m_context.getAllocator(), partialBuf, partialAlloc);
}

void bouken::IBLSystem::dispatchPrefilter() {
	VkCommandBuffer cmd = m_context.beginSingleTimeCommands();

	// Transition all mip levels of the prefiltered cubemap to GENERAL
	VkImageMemoryBarrier toGeneral{};
	toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toGeneral.image = m_prefilteredEnv;
	toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0,
	                              m_config.prefilteredMipLevels, 0, 6};
	toGeneral.srcAccessMask = 0;
	toGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
	                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
	                     nullptr, 1, &toGeneral);

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_prefilterPipeline);

	for (uint32_t mip = 0; mip < m_config.prefilteredMipLevels; ++mip) {
		uint32_t mipSize = std::max(1u, m_config.prefilteredResolution >> mip);
		float roughness = static_cast<float>(mip) /
		                  static_cast<float>(m_config.prefilteredMipLevels - 1);

		// Bind the per-mip storage view created in init()
		VkDescriptorSet ds = m_context.allocateDescriptorSet(
		    m_computeDescriptorPool, m_prefilterDSL);

		VkDescriptorImageInfo envInfo{};
		envInfo.sampler = m_envSampler;
		envInfo.imageView = m_envCubemapView;
		envInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkDescriptorImageInfo mipInfo{};
		mipInfo.imageView = m_prefilteredMipViews[mip];
		mipInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

		std::array<VkWriteDescriptorSet, 2> writes{};
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = ds;
		writes[0].dstBinding = 0;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[0].descriptorCount = 1;
		writes[0].pImageInfo = &envInfo;

		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = ds;
		writes[1].dstBinding = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
		writes[1].descriptorCount = 1;
		writes[1].pImageInfo = &mipInfo;

		vkUpdateDescriptorSets(m_context.getDevice(), 2, writes.data(), 0,
		                       nullptr);

		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
		                        m_prefilterLayout, 0, 1, &ds, 0, nullptr);

		struct PushConstants {
			uint32_t faceSize;
			uint32_t sourceFaceSize;
			uint32_t sampleCount;
			float roughness;
		} pushConst{mipSize, m_config.envCubemapResolution,
		            m_config.prefilterSampleCount, roughness};

		vkCmdPushConstants(cmd, m_prefilterLayout, VK_SHADER_STAGE_COMPUTE_BIT,
		                   0, sizeof(pushConst), &pushConst);

		uint32_t groups = std::max(1u, (mipSize + 15) / 16);
		vkCmdDispatch(cmd, groups, groups, 6);

		// Barrier between mip dispatches: each mip write must complete
		// before the next reads from the source cubemap at a lower mip
		VkImageMemoryBarrier mipBarrier{};
		mipBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		mipBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		mipBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		mipBarrier.image = m_prefilteredEnv;
		mipBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, 0, 6};
		mipBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		mipBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0,
		                     nullptr, 0, nullptr, 1, &mipBarrier);
	}

	// Final transition to SHADER_READ_ONLY for sampling in the lighting pass
	VkImageMemoryBarrier toShaderRead{};
	toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toShaderRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toShaderRead.image = m_prefilteredEnv;
	toShaderRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0,
	                                 m_config.prefilteredMipLevels, 0, 6};
	toShaderRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
	                     0, nullptr, 1, &toShaderRead);

	m_context.endSingleTimeCommands(cmd);
}

void bouken::IBLSystem::dispatchBRDFLut() {
	VkCommandBuffer cmd = m_context.beginSingleTimeCommands();

	// Transition LUT to GENERAL for storage write
	VkImageMemoryBarrier toGeneral{};
	toGeneral.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
	toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toGeneral.image = m_brdfLut;
	toGeneral.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	toGeneral.srcAccessMask = 0;
	toGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
	                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
	                     nullptr, 1, &toGeneral);

	VkDescriptorSet ds =
	    m_context.allocateDescriptorSet(m_computeDescriptorPool, m_brdfLutDSL);

	VkDescriptorImageInfo lutInfo{};
	lutInfo.imageView = m_brdfLutView;
	lutInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = ds;
	write.dstBinding = 0;
	write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
	write.descriptorCount = 1;
	write.pImageInfo = &lutInfo;

	vkUpdateDescriptorSets(m_context.getDevice(), 1, &write, 0, nullptr);

	struct PushConstants {
		uint32_t resolution;
		uint32_t sampleCount;
	} pushConst{m_config.brdfLutResolution, m_config.prefilterSampleCount};

	vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_brdfLutPipeline);
	vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
	                        m_brdfLutLayout, 0, 1, &ds, 0, nullptr);
	vkCmdPushConstants(cmd, m_brdfLutLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
	                   sizeof(pushConst), &pushConst);

	uint32_t groups = (m_config.brdfLutResolution + 15) / 16;
	vkCmdDispatch(cmd, groups, groups, 1);

	// Transition to SHADER_READ_ONLY for sampling in the lighting pass
	VkImageMemoryBarrier toShaderRead{};
	toShaderRead.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	toShaderRead.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
	toShaderRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	toShaderRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toShaderRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	toShaderRead.image = m_brdfLut;
	toShaderRead.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	toShaderRead.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	toShaderRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr,
	                     0, nullptr, 1, &toShaderRead);

	m_context.endSingleTimeCommands(cmd);
}

void bouken::IBLSystem::createCubemapImage(uint32_t resolution,
                                           uint32_t mipLevels, VkFormat format,
                                           VkImageUsageFlags usage,
                                           VkImage& outImage,
                                           VmaAllocation& outAllocation,
                                           VkImageView& outView) {
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = format;
	imageInfo.extent = {resolution, resolution, 1};
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = 6;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = usage;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

	if (vmaCreateImage(m_context.getAllocator(), &imageInfo, &allocInfo,
	                   &outImage, &outAllocation, nullptr) != VK_SUCCESS) {
		throw std::runtime_error("IBLSystem: failed to create cubemap image");
	}

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = outImage;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = mipLevels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 6;

	if (vkCreateImageView(m_context.getDevice(), &viewInfo, nullptr,
	                      &outView) != VK_SUCCESS) {
		throw std::runtime_error(
		    "IBLSystem: failed to create cubemap image view");
	}
}

void bouken::IBLSystem::create2DImage(uint32_t width, uint32_t height,
                                      uint32_t mipLevels, VkFormat format,
                                      VkImageUsageFlags usage,
                                      VkImage& outImage,
                                      VmaAllocation& outAllocation,
                                      VkImageView& outView) {
	VkImageCreateInfo imageInfo{};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = format;
	imageInfo.extent = {width, height, 1};
	imageInfo.mipLevels = mipLevels;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = usage;
	imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

	if (vmaCreateImage(m_context.getAllocator(), &imageInfo, &allocInfo,
	                   &outImage, &outAllocation, nullptr) != VK_SUCCESS) {
		throw std::runtime_error("IBLSystem: failed to create 2D image");
	}

	VkImageViewCreateInfo viewInfo{};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = outImage;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = mipLevels;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;

	if (vkCreateImageView(m_context.getDevice(), &viewInfo, nullptr,
	                      &outView) != VK_SUCCESS) {
		throw std::runtime_error("IBLSystem: failed to create 2D image view");
	}
}

VkSampler bouken::IBLSystem::createSampler(
    float maxLod, VkFilter filter, VkSamplerAddressMode addressModeU,
    VkSamplerAddressMode addressModeV) {
	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = filter;
	samplerInfo.minFilter = filter;
	samplerInfo.mipmapMode = (filter == VK_FILTER_NEAREST)
	                             ? VK_SAMPLER_MIPMAP_MODE_NEAREST
	                             : VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.addressModeU = addressModeU;
	samplerInfo.addressModeV = addressModeV;
	samplerInfo.addressModeW = addressModeV;
	samplerInfo.anisotropyEnable = VK_FALSE;
	samplerInfo.maxAnisotropy = 1.0f;
	samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;
	samplerInfo.compareEnable = VK_FALSE;
	samplerInfo.mipLodBias = 0.0f;
	samplerInfo.minLod = 0.0f;
	samplerInfo.maxLod = maxLod;

	VkSampler sampler = VK_NULL_HANDLE;
	if (vkCreateSampler(m_context.getDevice(), &samplerInfo, nullptr,
	                    &sampler) != VK_SUCCESS) {
		throw std::runtime_error("IBLSystem: failed to create sampler");
	}
	return sampler;
}

void bouken::IBLSystem::checkInitialized(const char* caller) const {
	if (!m_initialized)
		throw std::runtime_error(std::string("IBLSystem: ") + caller +
		                         " called before init()");
}

void bouken::IBLSystem::checkReady(const char* caller) const {
	if (!m_ready)
		throw std::runtime_error(std::string("IBLSystem: ") + caller +
		                         " called before loadEnvironment() succeeded");
}

uint32_t bouken::IBLSystem::mipLevelsForResolution(uint32_t resolution) {
	return static_cast<uint32_t>(std::floor(std::log2(resolution))) + 1;
}

void bouken::IBLSystem::nameSampler(VkSampler sampler, const char* name) {
	VkDebugUtilsObjectNameInfoEXT nameInfo{};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = VK_OBJECT_TYPE_SAMPLER;
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(sampler);
	nameInfo.pObjectName = name;
	m_context.setDebugName(nameInfo);
}

void bouken::IBLSystem::nameImage(VkImage image, const char* name) {
	VkDebugUtilsObjectNameInfoEXT nameInfo{};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = VK_OBJECT_TYPE_IMAGE;
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(image);
	nameInfo.pObjectName = name;
	m_context.setDebugName(nameInfo);
}

void bouken::IBLSystem::nameImageView(VkImageView view, const char* name) {
	VkDebugUtilsObjectNameInfoEXT nameInfo{};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = VK_OBJECT_TYPE_IMAGE_VIEW;
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(view);
	nameInfo.pObjectName = name;
	m_context.setDebugName(nameInfo);
}

void bouken::IBLSystem::nameBuffer(VkBuffer buffer, const char* name) {
	VkDebugUtilsObjectNameInfoEXT nameInfo{};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = VK_OBJECT_TYPE_BUFFER;
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(buffer);
	nameInfo.pObjectName = name;
	m_context.setDebugName(nameInfo);
}