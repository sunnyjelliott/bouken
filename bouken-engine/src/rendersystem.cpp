#include "rendersystem.h"

#include <vk_mem_alloc.h>

#include "camerasystem.h"
#include "materialmanager.h"
#include "meshloader.h"
#include "primitives.h"
#include "render/framedata.h"
#include "texturemanager.h"
#include "vulkantexturebackend.h"
#include "world.h"

static std::vector<char> readFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("Failed to open file: " + filename);
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	return buffer;
}

static uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                               uint32_t typeFilter,
                               VkMemoryPropertyFlags properties) {
	VkPhysicalDeviceMemoryProperties memProperties;
	vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

	for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
		if ((typeFilter & (1 << i)) &&
		    (memProperties.memoryTypes[i].propertyFlags & properties) ==
		        properties) {
			return i;
		}
	}

	throw std::runtime_error("Failed to find suitable memory type!");
}

void RenderSystem::initialize(VulkanContext& context, SwapChain& swapChain) {
	m_context = &context;
	m_swapChain = &swapChain;
	m_swapChainFormat = swapChain.getImageFormat();

	createDepthResources();
	createGBufferTargets();
	createHDRTarget();
	createSamplers();
	createDescriptorSetLayouts();
	createDescriptorPool();
	createFrameUBOs();
	createFrameDescriptorSets();
	createLightingDescriptorSet();
	createTonemapDescriptorSet();
	createDepthPrepass();
	createGeometryPass();
	createGBufferFramebuffer();
	createLightingPass();
	createTonemapPass();
	swapChain.createFramebuffers(m_tonemap.renderPass);
	createMeshBuffers();
	createCommandBuffer();
	createSyncObjects();
}

void RenderSystem::cleanup() {
	// Sync objects
	vkDestroySemaphore(m_context->getDevice(), m_imageAvailableSemaphore,
	                   nullptr);
	vkDestroySemaphore(m_context->getDevice(), m_renderFinishedSemaphore,
	                   nullptr);
	vkDestroyFence(m_context->getDevice(), m_inFlightFence, nullptr);

	// Mesh buffers
	vmaDestroyBuffer(m_context->getAllocator(), m_indexBuffer,
	                 m_indexBufferAllocation);
	vmaDestroyBuffer(m_context->getAllocator(), m_vertexBuffer,
	                 m_vertexBufferAllocation);

	// Material UBOs
	for (size_t i = 0; i < m_materialUBOs.size(); i++) {
		vmaDestroyBuffer(m_context->getAllocator(), m_materialUBOs[i],
		                 m_materialUBOAllocations[i]);
	}

	// Frame UBOs
	for (size_t i = 0; i < m_frameUBOs.size(); i++) {
		vmaDestroyBuffer(m_context->getAllocator(), m_frameUBOs[i],
		                 m_frameUBOAllocations[i]);
	}

	// Descriptor pool (implicitly frees all sets)
	vkDestroyDescriptorPool(m_context->getDevice(), m_descriptorPool, nullptr);

	// Samplers
	vkDestroySampler(m_context->getDevice(), m_gbufferSampler, nullptr);

	// Pipelines and layouts
	vkDestroyPipeline(m_context->getDevice(), m_depthPrepass.pipeline, nullptr);
	vkDestroyPipelineLayout(m_context->getDevice(), m_depthPrepass.layout,
	                        nullptr);
	vkDestroyRenderPass(m_context->getDevice(), m_depthPrepass.renderPass,
	                    nullptr);
	vkDestroyFramebuffer(m_context->getDevice(), m_depthPrepass.framebuffer,
	                     nullptr);

	vkDestroyPipeline(m_context->getDevice(), m_geometry.pipeline, nullptr);
	vkDestroyPipelineLayout(m_context->getDevice(), m_geometry.layout, nullptr);
	vkDestroyRenderPass(m_context->getDevice(), m_geometry.renderPass, nullptr);
	vkDestroyFramebuffer(m_context->getDevice(), m_gbuffer.framebuffer,
	                     nullptr);

	vkDestroyPipeline(m_context->getDevice(), m_lighting.pipeline, nullptr);
	vkDestroyPipelineLayout(m_context->getDevice(), m_lighting.layout, nullptr);
	vkDestroyRenderPass(m_context->getDevice(), m_lighting.renderPass, nullptr);
	vkDestroyFramebuffer(m_context->getDevice(), m_hdr.framebuffer, nullptr);

	vkDestroyPipeline(m_context->getDevice(), m_tonemap.pipeline, nullptr);
	vkDestroyPipelineLayout(m_context->getDevice(), m_tonemap.layout, nullptr);
	vkDestroyRenderPass(m_context->getDevice(), m_tonemap.renderPass, nullptr);

	// Descriptor set layouts
	vkDestroyDescriptorSetLayout(m_context->getDevice(), m_frameSetLayout,
	                             nullptr);
	vkDestroyDescriptorSetLayout(m_context->getDevice(), m_lightingSetLayout,
	                             nullptr);
	vkDestroyDescriptorSetLayout(m_context->getDevice(), m_tonemapSetLayout,
	                             nullptr);
	vkDestroyDescriptorSetLayout(m_context->getDevice(), m_materialSetLayout,
	                             nullptr);
	vkDestroyDescriptorSetLayout(m_context->getDevice(), m_objectSetLayout,
	                             nullptr);

	// Render targets
	m_depth.target.destroy(m_context->getDevice(), m_context->getAllocator());
	m_gbuffer.baseColorMetallic.destroy(m_context->getDevice(),
	                                    m_context->getAllocator());
	m_gbuffer.normals.destroy(m_context->getDevice(),
	                          m_context->getAllocator());
	m_gbuffer.roughnessAOSpecID.destroy(m_context->getDevice(),
	                                    m_context->getAllocator());
	m_gbuffer.emissiveFlags.destroy(m_context->getDevice(),
	                                m_context->getAllocator());
	m_hdr.target.destroy(m_context->getDevice(), m_context->getAllocator());
}

uint32_t RenderSystem::loadMesh(const std::string& filepath) {
	LoadedMesh loadedMesh = MeshLoader::loadOBJ(filepath);

	uint32_t meshID = m_nextMeshID++;

	m_meshes[meshID] = {
	    .firstVertex = static_cast<uint32_t>(m_allVertices.size()),
	    .vertexCount = static_cast<uint32_t>(loadedMesh.vertices.size()),
	    .firstIndex = static_cast<uint32_t>(m_allIndices.size()),
	    .indexCount = static_cast<uint32_t>(loadedMesh.indices.size())};

	m_allVertices.insert(m_allVertices.end(), loadedMesh.vertices.begin(),
	                     loadedMesh.vertices.end());
	m_allIndices.insert(m_allIndices.end(), loadedMesh.indices.begin(),
	                    loadedMesh.indices.end());

	// Re-upload all mesh data
	uploadMeshData(m_allVertices, m_allIndices);

	return meshID;
}

void RenderSystem::drawFrame(SwapChain& swapChain, World& world,
                             const CameraSystem& cameraSystem,
                             MaterialManager& materialManager) {
	vkWaitForFences(m_context->getDevice(), 1, &m_inFlightFence, VK_TRUE,
	                UINT64_MAX);
	vkResetFences(m_context->getDevice(), 1, &m_inFlightFence);

	uint32_t imageIndex;
	vkAcquireNextImageKHR(m_context->getDevice(), swapChain.getSwapChain(),
	                      UINT64_MAX, m_imageAvailableSemaphore, VK_NULL_HANDLE,
	                      &imageIndex);

	m_currentImageIndex = imageIndex;

	// Gather and sort render items once per frame
	gatherRenderItems(world, cameraSystem, materialManager);

	vkResetCommandBuffer(m_commandBuffer, 0);
	recordCommandBuffer(m_commandBuffer, imageIndex, swapChain, world,
	                    cameraSystem, materialManager);

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

	VkSemaphore waitSemaphores[] = {m_imageAvailableSemaphore};
	VkPipelineStageFlags waitStages[] = {
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
	submitInfo.waitSemaphoreCount = 1;
	submitInfo.pWaitSemaphores = waitSemaphores;
	submitInfo.pWaitDstStageMask = waitStages;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &m_commandBuffer;

	VkSemaphore signalSemaphores[] = {m_renderFinishedSemaphore};
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = signalSemaphores;

	if (vkQueueSubmit(m_context->getGraphicsQueue(), 1, &submitInfo,
	                  m_inFlightFence) != VK_SUCCESS) {
		throw std::runtime_error("Failed to submit draw command buffer!");
	}

	VkPresentInfoKHR presentInfo{};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = signalSemaphores;

	VkSwapchainKHR swapChains[] = {swapChain.getSwapChain()};
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = swapChains;
	presentInfo.pImageIndices = &imageIndex;

	vkQueuePresentKHR(m_context->getPresentQueue(), &presentInfo);
}

uint32_t RenderSystem::uploadMesh(const std::vector<Vertex>& vertices,
                                  const std::vector<uint32_t>& indices) {
	if (vertices.empty() || indices.empty()) {
		std::cerr << "Cannot upload empty mesh" << std::endl;
		return 0;  // Return default cube
	}

	uint32_t meshID = m_nextMeshID++;

	// Store mesh metadata
	m_meshes[meshID] = {
	    .firstVertex = static_cast<uint32_t>(m_allVertices.size()),
	    .vertexCount = static_cast<uint32_t>(vertices.size()),
	    .firstIndex = static_cast<uint32_t>(m_allIndices.size()),
	    .indexCount = static_cast<uint32_t>(indices.size())};

	// Append to global vertex/index arrays
	m_allVertices.insert(m_allVertices.end(), vertices.begin(), vertices.end());
	m_allIndices.insert(m_allIndices.end(), indices.begin(), indices.end());

	return meshID;
}

void RenderSystem::flushMeshUploads() {
	if (!m_allVertices.empty() && !m_allIndices.empty()) {
		uploadMeshData(m_allVertices, m_allIndices);
	}
}

void RenderSystem::createMaterialDescriptorSets(
    MaterialManager& materialManager, TextureManager& textureManager) {
	std::cout << "Creating descriptor sets for "
	          << materialManager.getMaterialCount() << " materials..."
	          << std::endl;

	uint32_t defaultWhiteID = textureManager.getDefaultWhiteTextureID();
	uint32_t defaultNormalID = textureManager.getDefaultNormalTextureID();

	// Iterate through all materials
	for (uint32_t matID = 0; matID <= 200; ++matID) {
		if (!materialManager.hasMaterial(matID)) {
			continue;
		}

		const Material& material = materialManager.getMaterial(matID);

		// Allocate descriptor set
		VkDescriptorSetAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = m_descriptorPool;
		allocInfo.descriptorSetCount = 1;
		allocInfo.pSetLayouts = &m_materialSetLayout;

		VkDescriptorSet descriptorSet;
		if (vkAllocateDescriptorSets(m_context->getDevice(), &allocInfo,
		                             &descriptorSet) != VK_SUCCESS) {
			throw std::runtime_error("Failed to allocate descriptor set!");
		}

		// Create per-material constant UBO
		VkBufferCreateInfo uboInfo{};
		uboInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		uboInfo.size = sizeof(MaterialConstants);
		uboInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		uboInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo uboAllocInfo{};
		uboAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		uboAllocInfo.flags =
		    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
		    VMA_ALLOCATION_CREATE_MAPPED_BIT;

		VkBuffer uboBuffer;
		VmaAllocation uboAllocation;
		VmaAllocationInfo uboAllocResult{};

		if (vmaCreateBuffer(m_context->getAllocator(), &uboInfo, &uboAllocInfo,
		                    &uboBuffer, &uboAllocation,
		                    &uboAllocResult) != VK_SUCCESS) {
			std::cerr << "Failed to create material UBO for material " << matID
			          << std::endl;
			continue;
		}

		// Upload scalar data immediately — won't change unless material is
		// edited
		MaterialConstants scalars = material.toConstants();
		memcpy(uboAllocResult.pMappedData, &scalars, sizeof(MaterialConstants));

		m_materialUBOs.push_back(uboBuffer);
		m_materialUBOAllocations.push_back(uboAllocation);

		auto resolveBinding = [&](uint32_t textureID,
		                          uint32_t defaultID) -> uint32_t {
			return (textureID != 0) ? textureID : defaultID;
		};

		uint32_t albedoID =
		    resolveBinding(material.albedoTextureID, defaultWhiteID);
		uint32_t normalID =
		    resolveBinding(material.normalTextureID, defaultNormalID);
		uint32_t metallicID =
		    resolveBinding(material.metallicTextureID, defaultWhiteID);
		uint32_t roughnessID =
		    resolveBinding(material.roughnessTextureID, defaultWhiteID);
		uint32_t aoID = resolveBinding(material.aoTextureID, defaultWhiteID);
		uint32_t emissiveID =
		    resolveBinding(material.emissiveTextureID, defaultWhiteID);

		auto makeImageInfo = [&](uint32_t id) -> VkDescriptorImageInfo {
			VulkanTextureBindingData* data =
			    static_cast<VulkanTextureBindingData*>(
			        textureManager.getBindingData(id));
			VkDescriptorImageInfo info{};
			info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			info.imageView = data->imageView;
			info.sampler = data->sampler;
			return info;
		};

		std::array<VkDescriptorImageInfo, 6> imageInfos = {
		    makeImageInfo(albedoID),   makeImageInfo(normalID),
		    makeImageInfo(metallicID), makeImageInfo(roughnessID),
		    makeImageInfo(aoID),       makeImageInfo(emissiveID),
		};

		// Binding 6: material scalars UBO
		VkDescriptorBufferInfo scalarBufferInfo{};
		scalarBufferInfo.buffer = uboBuffer;
		scalarBufferInfo.offset = 0;
		scalarBufferInfo.range = sizeof(MaterialConstants);

		std::array<VkWriteDescriptorSet, 7> writes{};
		for (uint32_t i = 0; i < 6; i++) {
			writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			writes[i].dstSet = descriptorSet;
			writes[i].dstBinding = i;
			writes[i].dstArrayElement = 0;
			writes[i].descriptorType =
			    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			writes[i].descriptorCount = 1;
			writes[i].pImageInfo = &imageInfos[i];
		}

		writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[6].dstSet = descriptorSet;
		writes[6].dstBinding = 6;
		writes[6].dstArrayElement = 0;
		writes[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[6].descriptorCount = 1;
		writes[6].pBufferInfo = &scalarBufferInfo;

		// Update both descriptors
		vkUpdateDescriptorSets(m_context->getDevice(), 2, writes.data(), 0,
		                       nullptr);

		// Store descriptor set in material
		materialManager.createDescriptorSet(matID, descriptorSet);
	}

	std::cout << "Created descriptor sets for materials" << std::endl;
}

void RenderSystem::createDepthResources() {
	// Find a supported depth format
	const std::vector<VkFormat> candidates = {VK_FORMAT_D32_SFLOAT,
	                                          VK_FORMAT_D32_SFLOAT_S8_UINT,
	                                          VK_FORMAT_D24_UNORM_S8_UINT};

	m_depth.format = VK_FORMAT_UNDEFINED;
	for (VkFormat format : candidates) {
		VkFormatProperties props;
		vkGetPhysicalDeviceFormatProperties(m_context->getPhysicalDevice(),
		                                    format, &props);
		if (props.optimalTilingFeatures &
		    VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
			m_depth.format = format;
			break;
		}
	}

	if (m_depth.format == VK_FORMAT_UNDEFINED) {
		throw std::runtime_error("Failed to find supported depth format!");
	}

	RenderTargetDesc desc{};
	desc.width = m_swapChain->getExtent().width;
	desc.height = m_swapChain->getExtent().height;
	desc.format = m_depth.format;
	desc.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
	             VK_IMAGE_USAGE_SAMPLED_BIT;  // lighting pass will sample depth
	desc.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
	desc.debugName = "depth";

	m_depth.target.create(*m_context, m_context->getAllocator(), desc);
}

void RenderSystem::createGBufferTargets() {
	const uint32_t w = m_swapChain->getExtent().width;
	const uint32_t h = m_swapChain->getExtent().height;

	constexpr VkImageUsageFlags gbufferUsage =
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

	RenderTargetDesc baseColorDesc{};
	baseColorDesc.width = w;
	baseColorDesc.height = h;
	baseColorDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
	baseColorDesc.usage = gbufferUsage;
	baseColorDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	baseColorDesc.debugName = "gbuffer_basecolor_metallic";
	m_gbuffer.baseColorMetallic.create(*m_context, m_context->getAllocator(),
	                                   baseColorDesc);

	RenderTargetDesc normalsDesc{};
	normalsDesc.width = w;
	normalsDesc.height = h;
	normalsDesc.format = VK_FORMAT_R16G16_SNORM;
	normalsDesc.usage = gbufferUsage;
	normalsDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	normalsDesc.debugName = "gbuffer_normals";
	m_gbuffer.normals.create(*m_context, m_context->getAllocator(),
	                         normalsDesc);

	RenderTargetDesc roughnessDesc{};
	roughnessDesc.width = w;
	roughnessDesc.height = h;
	roughnessDesc.format = VK_FORMAT_R8G8B8A8_UNORM;
	roughnessDesc.usage = gbufferUsage;
	roughnessDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	roughnessDesc.debugName = "gbuffer_roughness_ao_specular_id";
	m_gbuffer.roughnessAOSpecID.create(*m_context, m_context->getAllocator(),
	                                   roughnessDesc);

	RenderTargetDesc emissiveDesc{};
	emissiveDesc.width = w;
	emissiveDesc.height = h;
	emissiveDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	emissiveDesc.usage = gbufferUsage;
	emissiveDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	emissiveDesc.debugName = "gbuffer_emissive_flags";
	m_gbuffer.emissiveFlags.create(*m_context, m_context->getAllocator(),
	                               emissiveDesc);
}

void RenderSystem::createHDRTarget() {
	RenderTargetDesc hdrDesc{};
	hdrDesc.width = m_swapChain->getExtent().width;
	hdrDesc.height = m_swapChain->getExtent().height;
	hdrDesc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	hdrDesc.usage =
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	hdrDesc.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
	hdrDesc.debugName = "hdr_target";
	m_hdr.target.create(*m_context, m_context->getAllocator(), hdrDesc);
}

void RenderSystem::createSamplers() {
	// -------------------------------------------------------
	// G-buffer sampler
	// -------------------------------------------------------
	VkSamplerCreateInfo gbufferSamplerInfo{};
	gbufferSamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	gbufferSamplerInfo.magFilter = VK_FILTER_NEAREST;
	gbufferSamplerInfo.minFilter = VK_FILTER_NEAREST;
	gbufferSamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	gbufferSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	gbufferSamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	gbufferSamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	gbufferSamplerInfo.anisotropyEnable = VK_FALSE;
	gbufferSamplerInfo.maxAnisotropy = 1.0f;
	gbufferSamplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
	gbufferSamplerInfo.unnormalizedCoordinates = VK_FALSE;
	gbufferSamplerInfo.compareEnable = VK_FALSE;
	gbufferSamplerInfo.mipLodBias = 0.0f;
	gbufferSamplerInfo.minLod = 0.0f;
	gbufferSamplerInfo.maxLod = 0.0f;

	if (vkCreateSampler(m_context->getDevice(), &gbufferSamplerInfo, nullptr,
	                    &m_gbufferSampler) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create G-buffer sampler!");
	}

	VkDebugUtilsObjectNameInfoEXT nameInfo{};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = VK_OBJECT_TYPE_SAMPLER;
	nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_gbufferSampler);
	nameInfo.pObjectName = "gbuffer_sampler";
	m_context->setDebugName(nameInfo);
}

void RenderSystem::createDescriptorSetLayouts() {
	// -------------------------------------------------------
	// Set 0 — per frame: one UBO visible to all stages
	// -------------------------------------------------------
	VkDescriptorSetLayoutBinding frameUBOBinding{};
	frameUBOBinding.binding = 0;
	frameUBOBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	frameUBOBinding.descriptorCount = 1;
	frameUBOBinding.stageFlags =
	    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo frameLayoutInfo{};
	frameLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	frameLayoutInfo.bindingCount = 1;
	frameLayoutInfo.pBindings = &frameUBOBinding;

	if (vkCreateDescriptorSetLayout(m_context->getDevice(), &frameLayoutInfo,
	                                nullptr, &m_frameSetLayout) != VK_SUCCESS) {
		throw std::runtime_error(
		    "Failed to create frame descriptor set layout!");
	}

	// -------------------------------------------------------
	// Set 1a — lighting pass: 5 G-buffer samplers + depth
	// -------------------------------------------------------
	std::array<VkDescriptorSetLayoutBinding, 5> lightingBindings{};
	for (uint32_t i = 0; i < 5; i++) {
		lightingBindings[i].binding = i;
		lightingBindings[i].descriptorType =
		    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		lightingBindings[i].descriptorCount = 1;
		lightingBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	}

	VkDescriptorSetLayoutCreateInfo lightingLayoutInfo{};
	lightingLayoutInfo.sType =
	    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	lightingLayoutInfo.bindingCount =
	    static_cast<uint32_t>(lightingBindings.size());
	lightingLayoutInfo.pBindings = lightingBindings.data();

	if (vkCreateDescriptorSetLayout(m_context->getDevice(), &lightingLayoutInfo,
	                                nullptr,
	                                &m_lightingSetLayout) != VK_SUCCESS) {
		throw std::runtime_error(
		    "Failed to create lighting descriptor set layout!");
	}

	// -------------------------------------------------------
	// Set 1b — tonemap pass: 1 HDR sampler
	// -------------------------------------------------------
	VkDescriptorSetLayoutBinding tonemapBinding{};
	tonemapBinding.binding = 0;
	tonemapBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	tonemapBinding.descriptorCount = 1;
	tonemapBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo tonemapLayoutInfo{};
	tonemapLayoutInfo.sType =
	    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	tonemapLayoutInfo.bindingCount = 1;
	tonemapLayoutInfo.pBindings = &tonemapBinding;

	if (vkCreateDescriptorSetLayout(m_context->getDevice(), &tonemapLayoutInfo,
	                                nullptr,
	                                &m_tonemapSetLayout) != VK_SUCCESS) {
		throw std::runtime_error(
		    "Failed to create tonemap descriptor set layout!");
	}

	// -------------------------------------------------------
	// Set 2 — per material: 6 texture samplers + 1 UBO
	// -------------------------------------------------------
	std::array<VkDescriptorSetLayoutBinding, 7> materialBindings{};
	for (uint32_t i = 0; i < 6; i++) {
		materialBindings[i].binding = i;
		materialBindings[i].descriptorType =
		    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		materialBindings[i].descriptorCount = 1;
		materialBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	}

	materialBindings[6].binding = 6;
	materialBindings[6].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	materialBindings[6].descriptorCount = 1;
	materialBindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	VkDescriptorSetLayoutCreateInfo materialLayoutInfo{};
	materialLayoutInfo.sType =
	    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	materialLayoutInfo.bindingCount =
	    static_cast<uint32_t>(materialBindings.size());
	materialLayoutInfo.pBindings = materialBindings.data();

	if (vkCreateDescriptorSetLayout(m_context->getDevice(), &materialLayoutInfo,
	                                nullptr,
	                                &m_materialSetLayout) != VK_SUCCESS) {
		throw std::runtime_error(
		    "Failed to create material descriptor set layout!");
	}

	// -------------------------------------------------------
	// Set 3 — per object: stub, no bindings
	// Reserved for skeletal animation / instancing
	// -------------------------------------------------------
	VkDescriptorSetLayoutCreateInfo objectLayoutInfo{};
	objectLayoutInfo.sType =
	    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	objectLayoutInfo.bindingCount = 0;
	objectLayoutInfo.pBindings = nullptr;

	if (vkCreateDescriptorSetLayout(m_context->getDevice(), &objectLayoutInfo,
	                                nullptr,
	                                &m_objectSetLayout) != VK_SUCCESS) {
		throw std::runtime_error(
		    "Failed to create object descriptor set layout!");
	}
}

void RenderSystem::createDescriptorPool() {
	const uint32_t swapImageCount =
	    static_cast<uint32_t>(m_swapChain->getImageCount());
	constexpr uint32_t MAX_MATERIALS = 100;

	std::array<VkDescriptorPoolSize, 2> poolSizes{};

	// UBOs: per swapchain image + per material
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[0].descriptorCount = swapImageCount + MAX_MATERIALS;

	// Combined image samplers: lighting(5) + tonemap(1) + materials(6 * max)
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[1].descriptorCount = 5 + 1 + (6 * MAX_MATERIALS);

	// Max sets:
	//   frame sets     : swapImageCount
	//   lighting set   : 1
	//   tonemap set    : 1
	//   material sets  : MAX_MATERIALS
	//   object sets    : 0 (stub layout, no allocations yet)
	const uint32_t maxSets = swapImageCount + 1 + 1 + MAX_MATERIALS;

	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = maxSets;

	if (vkCreateDescriptorPool(m_context->getDevice(), &poolInfo, nullptr,
	                           &m_descriptorPool) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create descriptor pool!");
	}
}

void RenderSystem::createFrameUBOs() {
	const size_t imageCount = m_swapChain->getImageCount();

	m_frameUBOs.resize(imageCount);
	m_frameUBOAllocations.resize(imageCount);
	m_frameUBOMapped.resize(imageCount);

	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = sizeof(FrameUBO);
	bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
	                  VMA_ALLOCATION_CREATE_MAPPED_BIT;

	for (size_t i = 0; i < imageCount; i++) {
		VmaAllocationInfo allocResult{};
		if (vmaCreateBuffer(m_context->getAllocator(), &bufferInfo, &allocInfo,
		                    &m_frameUBOs[i], &m_frameUBOAllocations[i],
		                    &allocResult) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create frame UBO!");
		}

		m_frameUBOMapped[i] = allocResult.pMappedData;

		// Debug label
		std::string name = "frame_ubo_" + std::to_string(i);
		VkDebugUtilsObjectNameInfoEXT nameInfo{};
		nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
		nameInfo.objectType = VK_OBJECT_TYPE_BUFFER;
		nameInfo.objectHandle = reinterpret_cast<uint64_t>(m_frameUBOs[i]);
		nameInfo.pObjectName = name.c_str();
		m_context->setDebugName(nameInfo);
	}
}

void RenderSystem::createFrameDescriptorSets() {
	const size_t imageCount = m_swapChain->getImageCount();

	// Allocate one set per swapchain image, all from the same layout
	std::vector<VkDescriptorSetLayout> layouts(imageCount, m_frameSetLayout);

	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_descriptorPool;
	allocInfo.descriptorSetCount = static_cast<uint32_t>(imageCount);
	allocInfo.pSetLayouts = layouts.data();

	m_frameSets.resize(imageCount);
	if (vkAllocateDescriptorSets(m_context->getDevice(), &allocInfo,
	                             m_frameSets.data()) != VK_SUCCESS) {
		throw std::runtime_error("Failed to allocate frame descriptor sets!");
	}

	// Write UBO binding into each set
	for (size_t i = 0; i < imageCount; i++) {
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = m_frameUBOs[i];
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(FrameUBO);

		VkWriteDescriptorSet write{};
		write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		write.dstSet = m_frameSets[i];
		write.dstBinding = 0;
		write.dstArrayElement = 0;
		write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		write.descriptorCount = 1;
		write.pBufferInfo = &bufferInfo;

		vkUpdateDescriptorSets(m_context->getDevice(), 1, &write, 0, nullptr);
	}
}

void RenderSystem::createLightingDescriptorSet() {
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &m_lightingSetLayout;

	if (vkAllocateDescriptorSets(m_context->getDevice(), &allocInfo,
	                             &m_lightingSet) != VK_SUCCESS) {
		throw std::runtime_error("Failed to allocate lighting descriptor set!");
	}

	// Binding order must match m_lightingSetLayout:
	// 0: baseColorMetallic
	// 1: normals
	// 2: roughnessAOSpecID
	// 3: emissiveFlags
	// 4: depth
	std::array<VkDescriptorImageInfo, 5> imageInfos{};
	imageInfos[0] = {m_gbufferSampler,
	                 m_gbuffer.baseColorMetallic.getImageView(),
	                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	imageInfos[1] = {m_gbufferSampler, m_gbuffer.normals.getImageView(),
	                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	imageInfos[2] = {m_gbufferSampler,
	                 m_gbuffer.roughnessAOSpecID.getImageView(),
	                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	imageInfos[3] = {m_gbufferSampler, m_gbuffer.emissiveFlags.getImageView(),
	                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	imageInfos[4] = {m_gbufferSampler, m_depth.target.getImageView(),
	                 VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};

	std::array<VkWriteDescriptorSet, 5> writes{};
	for (uint32_t i = 0; i < 5; i++) {
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = m_lightingSet;
		writes[i].dstBinding = i;
		writes[i].dstArrayElement = 0;
		writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		writes[i].descriptorCount = 1;
		writes[i].pImageInfo = &imageInfos[i];
	}

	vkUpdateDescriptorSets(m_context->getDevice(),
	                       static_cast<uint32_t>(writes.size()), writes.data(),
	                       0, nullptr);
}

void RenderSystem::createTonemapDescriptorSet() {
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_descriptorPool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &m_tonemapSetLayout;

	if (vkAllocateDescriptorSets(m_context->getDevice(), &allocInfo,
	                             &m_tonemapSet) != VK_SUCCESS) {
		throw std::runtime_error("Failed to allocate tonemap descriptor set!");
	}

	VkDescriptorImageInfo imageInfo{};
	imageInfo.sampler = m_gbufferSampler;
	imageInfo.imageView = m_hdr.target.getImageView();
	imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkWriteDescriptorSet write{};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = m_tonemapSet;
	write.dstBinding = 0;
	write.dstArrayElement = 0;
	write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	write.descriptorCount = 1;
	write.pImageInfo = &imageInfo;

	vkUpdateDescriptorSets(m_context->getDevice(), 1, &write, 0, nullptr);
}

void RenderSystem::createDepthPrepass() {
	VkAttachmentDescription depthAttachment{};
	depthAttachment.format = m_depth.format;
	depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	depthAttachment.finalLayout =
	    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthRef{};
	depthRef.attachment = 0;
	depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 0;
	subpass.pDepthStencilAttachment = &depthRef;

	// Depth prepass -> geometry pass
	VkSubpassDependency dependency{};
	dependency.srcSubpass = 0;
	dependency.dstSubpass = VK_SUBPASS_EXTERNAL;
	dependency.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &depthAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	if (vkCreateRenderPass(m_context->getDevice(), &renderPassInfo, nullptr,
	                       &m_depthPrepass.renderPass) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create depth prepass render pass!");
	}

	VkImageView depthView = m_depth.target.getImageView();

	VkFramebufferCreateInfo framebufferInfo{};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = m_depthPrepass.renderPass;
	framebufferInfo.attachmentCount = 1;
	framebufferInfo.pAttachments = &depthView;
	framebufferInfo.width = m_swapChain->getExtent().width;
	framebufferInfo.height = m_swapChain->getExtent().height;
	framebufferInfo.layers = 1;

	if (vkCreateFramebuffer(m_context->getDevice(), &framebufferInfo, nullptr,
	                        &m_depthPrepass.framebuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create depth prepass framebuffer!");
	}

	auto vertShaderCode = readFile("shaders/depth_vert.spv");
	VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);

	VkPipelineShaderStageCreateInfo vertStage{};
	vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertStage.module = vertShaderModule;
	vertStage.pName = "main";

	auto bindingDescription = Vertex::getBindingDescription();
	auto attributeDescriptions = Vertex::getAttributeDescriptions();

	// vertex sliced in shader, fine to pass full vertex data here.
	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
	vertexInputInfo.vertexAttributeDescriptionCount =
	    static_cast<uint32_t>(attributeDescriptions.size());
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// No color blend state — no color attachments
	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 0;
	colorBlending.pAttachments = nullptr;

	std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
	                                             VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount =
	    static_cast<uint32_t>(dynamicStates.size());
	dynamicState.pDynamicStates = dynamicStates.data();

	VkPushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(glm::mat4) * 3;

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 0;  // no descriptor sets needed
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	if (vkCreatePipelineLayout(m_context->getDevice(), &pipelineLayoutInfo,
	                           nullptr, &m_depthPrepass.layout) != VK_SUCCESS) {
		throw std::runtime_error(
		    "Failed to create depth prepass pipeline layout!");
	}

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 1;  // vertex only
	pipelineInfo.pStages = &vertStage;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = m_depthPrepass.layout;
	pipelineInfo.renderPass = m_depthPrepass.renderPass;
	pipelineInfo.subpass = 0;

	if (vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1,
	                              &pipelineInfo, nullptr,
	                              &m_depthPrepass.pipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create depth prepass pipeline!");
	}

	vkDestroyShaderModule(m_context->getDevice(), vertShaderModule, nullptr);
}

void RenderSystem::createGeometryPass() {
	auto makeColorAttachment = [](VkFormat format) {
		VkAttachmentDescription a{};
		a.format = format;
		a.samples = VK_SAMPLE_COUNT_1_BIT;
		a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		a.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		return a;
	};

	std::array<VkAttachmentDescription, 5> attachments = {
	    makeColorAttachment(VK_FORMAT_R8G8B8A8_UNORM),  // 0 baseColorMetallic
	    makeColorAttachment(VK_FORMAT_R16G16_SNORM),    // 1 normals
	    makeColorAttachment(VK_FORMAT_R8G8B8A8_UNORM),  // 2 roughnessAOSpecID
	    makeColorAttachment(VK_FORMAT_R16G16B16A16_SFLOAT),  // 3 emissiveFlags
	    // depth
	    [&]() {
		    VkAttachmentDescription a{};
		    a.format = m_depth.format;
		    a.samples = VK_SAMPLE_COUNT_1_BIT;
		    a.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		    a.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		    a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		    a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		    a.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		    a.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		    return a;
	    }()};

	std::array<VkAttachmentReference, 4> colorRefs = {{
	    {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
	    {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
	    {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
	    {3, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
	}};

	VkAttachmentReference depthRef{};
	depthRef.attachment = 4;
	depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
	subpass.pColorAttachments = colorRefs.data();
	subpass.pDepthStencilAttachment = &depthRef;

	// Two dependencies:
	// 1. Geometry pass waits for depth prepass to finish writing depth
	// 2. Lighting pass waits for geometry pass to finish writing G-buffer
	std::array<VkSubpassDependency, 2> dependencies;

	// Depth prepass → geometry pass
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	dependencies[0].srcAccessMask =
	    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
	dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	// Geometry pass → lighting pass
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask =
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	renderPassInfo.pAttachments = attachments.data();
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	if (vkCreateRenderPass(m_context->getDevice(), &renderPassInfo, nullptr,
	                       &m_geometry.renderPass) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create geometry render pass!");
	}

	// -------------------------------------------------------
	// Pipeline
	// -------------------------------------------------------
	auto vertShaderCode = readFile("shaders/geometry_vert.spv");
	auto fragShaderCode = readFile("shaders/geometry_frag.spv");

	VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
	VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

	VkPipelineShaderStageCreateInfo shaderStages[] = {
	    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
	     VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule, "main", nullptr},
	    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
	     VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule, "main", nullptr},
	};

	auto bindingDescription = Vertex::getBindingDescription();
	auto attributeDescriptions = Vertex::getAttributeDescriptions();

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
	vertexInputInfo.vertexAttributeDescriptionCount =
	    static_cast<uint32_t>(attributeDescriptions.size());
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_FALSE;  // prepass already wrote depth
	depthStencil.depthCompareOp =
	    VK_COMPARE_OP_EQUAL;  // only draw what the prepass accepted

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// One blend attachment per color attachment — all passthrough
	VkPipelineColorBlendAttachmentState blendPassthrough{};
	blendPassthrough.colorWriteMask =
	    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendPassthrough.blendEnable = VK_FALSE;

	std::array<VkPipelineColorBlendAttachmentState, 4> blendAttachments = {
	    blendPassthrough, blendPassthrough, blendPassthrough, blendPassthrough};

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount =
	    static_cast<uint32_t>(blendAttachments.size());
	colorBlending.pAttachments = blendAttachments.data();

	std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
	                                             VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount =
	    static_cast<uint32_t>(dynamicStates.size());
	dynamicState.pDynamicStates = dynamicStates.data();

	VkPushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(glm::mat4) * 3;  // model, view, projection

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &m_materialSetLayout;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	if (vkCreatePipelineLayout(m_context->getDevice(), &pipelineLayoutInfo,
	                           nullptr, &m_geometry.layout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create geometry pipeline layout!");
	}

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = m_geometry.layout;
	pipelineInfo.renderPass = m_geometry.renderPass;
	pipelineInfo.subpass = 0;

	if (vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1,
	                              &pipelineInfo, nullptr,
	                              &m_geometry.pipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create geometry pipeline!");
	}

	vkDestroyShaderModule(m_context->getDevice(), fragShaderModule, nullptr);
	vkDestroyShaderModule(m_context->getDevice(), vertShaderModule, nullptr);
}

void RenderSystem::createGBufferFramebuffer() {
	std::array<VkImageView, 5> attachments = {
	    m_gbuffer.baseColorMetallic.getImageView(),  // 0
	    m_gbuffer.normals.getImageView(),            // 1
	    m_gbuffer.roughnessAOSpecID.getImageView(),  // 2
	    m_gbuffer.emissiveFlags.getImageView(),      // 3
	    m_depth.target.getImageView(),               // 4
	};

	VkFramebufferCreateInfo framebufferInfo{};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = m_geometry.renderPass;
	framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
	framebufferInfo.pAttachments = attachments.data();
	framebufferInfo.width = m_swapChain->getExtent().width;
	framebufferInfo.height = m_swapChain->getExtent().height;
	framebufferInfo.layers = 1;

	if (vkCreateFramebuffer(m_context->getDevice(), &framebufferInfo, nullptr,
	                        &m_gbuffer.framebuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create G-buffer framebuffer!");
	}
}

void RenderSystem::createLightingPass() {
	// -------------------------------------------------------
	// Render pass — one HDR color attachment, no depth
	// -------------------------------------------------------
	VkAttachmentDescription colorAttachment{};
	colorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	VkAttachmentReference colorRef{};
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;

	// G-buffer reads must be complete before lighting samples them,
	// and HDR write must be complete before tonemap samples it
	std::array<VkSubpassDependency, 2> dependencies{};

	// Geometry pass → lighting pass
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask =
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[0].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	// Lighting pass → tonemap pass
	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[1].srcStageMask =
	    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
	renderPassInfo.pDependencies = dependencies.data();

	if (vkCreateRenderPass(m_context->getDevice(), &renderPassInfo, nullptr,
	                       &m_lighting.renderPass) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create lighting render pass!");
	}

	// -------------------------------------------------------
	// Framebuffer — HDR target only
	// -------------------------------------------------------
	VkImageView hdrView = m_hdr.target.getImageView();

	VkFramebufferCreateInfo framebufferInfo{};
	framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	framebufferInfo.renderPass = m_lighting.renderPass;
	framebufferInfo.attachmentCount = 1;
	framebufferInfo.pAttachments = &hdrView;
	framebufferInfo.width = m_swapChain->getExtent().width;
	framebufferInfo.height = m_swapChain->getExtent().height;
	framebufferInfo.layers = 1;

	if (vkCreateFramebuffer(m_context->getDevice(), &framebufferInfo, nullptr,
	                        &m_hdr.framebuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create lighting framebuffer!");
	}

	// -------------------------------------------------------
	// Pipeline — fullscreen triangle, no vertex input
	// -------------------------------------------------------
	auto vertCode = readFile("shaders/fullscreen_vert.spv");
	auto fragCode = readFile("shaders/lighting_frag.spv");

	VkShaderModule vertModule = createShaderModule(vertCode);
	VkShaderModule fragModule = createShaderModule(fragCode);

	VkPipelineShaderStageCreateInfo shaderStages[] = {
	    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
	     VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main", nullptr},
	    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
	     VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main", nullptr},
	};

	// No vertex input — fullscreen triangle generated in vertex shader
	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_NONE;  // fullscreen triangle, no culling
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_FALSE;  // no depth involvement
	depthStencil.depthWriteEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blendAttachment{};
	blendAttachment.colorWriteMask =
	    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendAttachment.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &blendAttachment;

	std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
	                                             VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount =
	    static_cast<uint32_t>(dynamicStates.size());
	dynamicState.pDynamicStates = dynamicStates.data();

	// Set 0: frame data, Set 1: G-buffer textures
	std::array<VkDescriptorSetLayout, 2> setLayouts = {m_frameSetLayout,
	                                                   m_lightingSetLayout};

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount =
	    static_cast<uint32_t>(setLayouts.size());
	pipelineLayoutInfo.pSetLayouts = setLayouts.data();

	if (vkCreatePipelineLayout(m_context->getDevice(), &pipelineLayoutInfo,
	                           nullptr, &m_lighting.layout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create lighting pipeline layout!");
	}

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = m_lighting.layout;
	pipelineInfo.renderPass = m_lighting.renderPass;
	pipelineInfo.subpass = 0;

	if (vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1,
	                              &pipelineInfo, nullptr,
	                              &m_lighting.pipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create lighting pipeline!");
	}

	vkDestroyShaderModule(m_context->getDevice(), fragModule, nullptr);
	vkDestroyShaderModule(m_context->getDevice(), vertModule, nullptr);
}

void RenderSystem::createTonemapPass() {
	// -------------------------------------------------------
	// Render pass — swapchain image, no depth
	// -------------------------------------------------------
	VkAttachmentDescription colorAttachment{};
	colorAttachment.format = m_swapChainFormat;
	colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
	colorAttachment.loadOp =
	    VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // we overwrite every pixel
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	VkAttachmentReference colorRef{};
	colorRef.attachment = 0;
	colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass{};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;

	// HDR target must be readable before tonemap samples it
	VkSubpassDependency dependency{};
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
	dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

	VkRenderPassCreateInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.attachmentCount = 1;
	renderPassInfo.pAttachments = &colorAttachment;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 1;
	renderPassInfo.pDependencies = &dependency;

	if (vkCreateRenderPass(m_context->getDevice(), &renderPassInfo, nullptr,
	                       &m_tonemap.renderPass) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create tonemap render pass!");
	}

	// Swapchain framebuffers are created by SwapChain::createFramebuffers,
	// called with m_tonemap.renderPass — handled in initialize()

	// -------------------------------------------------------
	// Pipeline — fullscreen triangle, reuses fullscreen_vert.spv
	// -------------------------------------------------------
	auto vertCode = readFile("shaders/fullscreen_vert.spv");
	auto fragCode = readFile("shaders/tonemap_frag.spv");

	VkShaderModule vertModule = createShaderModule(vertCode);
	VkShaderModule fragModule = createShaderModule(fragCode);

	VkPipelineShaderStageCreateInfo shaderStages[] = {
	    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
	     VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main", nullptr},
	    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
	     VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main", nullptr},
	};

	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_FALSE;
	depthStencil.depthWriteEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blendAttachment{};
	blendAttachment.colorWriteMask =
	    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendAttachment.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType =
	    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &blendAttachment;

	std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
	                                             VK_DYNAMIC_STATE_SCISSOR};
	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount =
	    static_cast<uint32_t>(dynamicStates.size());
	dynamicState.pDynamicStates = dynamicStates.data();

	// Set 1 only: HDR sampler — no frame data needed for tonemap
	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &m_tonemapSetLayout;

	if (vkCreatePipelineLayout(m_context->getDevice(), &pipelineLayoutInfo,
	                           nullptr, &m_tonemap.layout) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create tonemap pipeline layout!");
	}

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = m_tonemap.layout;
	pipelineInfo.renderPass = m_tonemap.renderPass;
	pipelineInfo.subpass = 0;

	if (vkCreateGraphicsPipelines(m_context->getDevice(), VK_NULL_HANDLE, 1,
	                              &pipelineInfo, nullptr,
	                              &m_tonemap.pipeline) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create tonemap pipeline!");
	}

	vkDestroyShaderModule(m_context->getDevice(), fragModule, nullptr);
	vkDestroyShaderModule(m_context->getDevice(), vertModule, nullptr);
}

void RenderSystem::createMeshBuffers() {
	// Load built-in meshes

	// Mesh 0: Cube
	auto cubeGeom = Primitives::createCube(1.0f);
	m_meshes[m_nextMeshID++] = {
	    .firstVertex = static_cast<uint32_t>(m_allVertices.size()),
	    .vertexCount = static_cast<uint32_t>(cubeGeom.vertices.size()),
	    .firstIndex = static_cast<uint32_t>(m_allIndices.size()),
	    .indexCount = static_cast<uint32_t>(cubeGeom.indices.size())};
	m_allVertices.insert(m_allVertices.end(), cubeGeom.vertices.begin(),
	                     cubeGeom.vertices.end());
	m_allIndices.insert(m_allIndices.end(), cubeGeom.indices.begin(),
	                    cubeGeom.indices.end());

	// Mesh 1: Sphere
	auto sphereGeom = Primitives::createSphere(1.0f, 16, 16);
	m_meshes[m_nextMeshID++] = {
	    .firstVertex = static_cast<uint32_t>(m_allVertices.size()),
	    .vertexCount = static_cast<uint32_t>(sphereGeom.vertices.size()),
	    .firstIndex = static_cast<uint32_t>(m_allIndices.size()),
	    .indexCount = static_cast<uint32_t>(sphereGeom.indices.size())};
	m_allVertices.insert(m_allVertices.end(), sphereGeom.vertices.begin(),
	                     sphereGeom.vertices.end());
	m_allIndices.insert(m_allIndices.end(), sphereGeom.indices.begin(),
	                    sphereGeom.indices.end());

	// Mesh 2: Cone
	auto coneGeom = Primitives::createCone(1.0f, 2.0f, 16);
	m_meshes[m_nextMeshID++] = {
	    .firstVertex = static_cast<uint32_t>(m_allVertices.size()),
	    .vertexCount = static_cast<uint32_t>(coneGeom.vertices.size()),
	    .firstIndex = static_cast<uint32_t>(m_allIndices.size()),
	    .indexCount = static_cast<uint32_t>(coneGeom.indices.size())};
	m_allVertices.insert(m_allVertices.end(), coneGeom.vertices.begin(),
	                     coneGeom.vertices.end());
	m_allIndices.insert(m_allIndices.end(), coneGeom.indices.begin(),
	                    coneGeom.indices.end());

	// Upload to GPU
	uploadMeshData(m_allVertices, m_allIndices);
}

void RenderSystem::createCommandBuffer() {
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = m_context->getCommandPool();
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;

	if (vkAllocateCommandBuffers(m_context->getDevice(), &allocInfo,
	                             &m_commandBuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to allocate command buffer!");
	}
}

void RenderSystem::createSyncObjects() {
	VkSemaphoreCreateInfo semaphoreInfo{};
	semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

	VkFenceCreateInfo fenceInfo{};
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

	if (vkCreateSemaphore(m_context->getDevice(), &semaphoreInfo, nullptr,
	                      &m_imageAvailableSemaphore) != VK_SUCCESS ||
	    vkCreateSemaphore(m_context->getDevice(), &semaphoreInfo, nullptr,
	                      &m_renderFinishedSemaphore) != VK_SUCCESS ||
	    vkCreateFence(m_context->getDevice(), &fenceInfo, nullptr,
	                  &m_inFlightFence) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create synchronization objects!");
	}
}

void RenderSystem::uploadMeshData(const std::vector<Vertex>& vertices,
                                  const std::vector<uint32_t>& indices) {
	if (m_vertexBuffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(m_context->getAllocator(), m_vertexBuffer,
		                 m_vertexBufferAllocation);
	}
	if (m_indexBuffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(m_context->getAllocator(), m_indexBuffer,
		                 m_indexBufferAllocation);
	}

	VkDeviceSize vertexBufferSize = sizeof(Vertex) * vertices.size();
	VkDeviceSize indexBufferSize = sizeof(uint32_t) * indices.size();

	// === VERTEX BUFFER ===

	// Create staging buffer (CPU-visible)
	VkBuffer vertexStagingBuffer;
	VmaAllocation vertexStagingAllocation;

	VkBufferCreateInfo vertexStagingInfo{};
	vertexStagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexStagingInfo.size = vertexBufferSize;
	vertexStagingInfo.usage =
	    VK_BUFFER_USAGE_TRANSFER_SRC_BIT;  // Source for transfer
	vertexStagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo vertexStagingAllocInfo{};
	vertexStagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	vertexStagingAllocInfo.flags =
	    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
	    VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo vertexStagingAllocResult;
	if (vmaCreateBuffer(m_context->getAllocator(), &vertexStagingInfo,
	                    &vertexStagingAllocInfo, &vertexStagingBuffer,
	                    &vertexStagingAllocation,
	                    &vertexStagingAllocResult) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create vertex staging buffer!");
	}

	// Copy data to staging buffer
	memcpy(vertexStagingAllocResult.pMappedData, vertices.data(),
	       (size_t)vertexBufferSize);

	// Create device-local buffer (GPU-only, fast)
	VkBufferCreateInfo vertexBufferInfo{};
	vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	vertexBufferInfo.size = vertexBufferSize;
	vertexBufferInfo.usage =
	    VK_BUFFER_USAGE_TRANSFER_DST_BIT |  // Destination for transfer
	    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;  // Will be used as vertex buffer
	vertexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo vertexAllocInfo{};
	vertexAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	vertexAllocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

	if (vmaCreateBuffer(m_context->getAllocator(), &vertexBufferInfo,
	                    &vertexAllocInfo, &m_vertexBuffer,
	                    &m_vertexBufferAllocation, nullptr) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create vertex buffer!");
	}

	// === INDEX BUFFER ===

	// Create staging buffer
	VkBuffer indexStagingBuffer;
	VmaAllocation indexStagingAllocation;

	VkBufferCreateInfo indexStagingInfo{};
	indexStagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	indexStagingInfo.size = indexBufferSize;
	indexStagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	indexStagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo indexStagingAllocInfo{};
	indexStagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	indexStagingAllocInfo.flags =
	    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
	    VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo indexStagingAllocResult;
	if (vmaCreateBuffer(m_context->getAllocator(), &indexStagingInfo,
	                    &indexStagingAllocInfo, &indexStagingBuffer,
	                    &indexStagingAllocation,
	                    &indexStagingAllocResult) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create index staging buffer!");
	}

	// Copy data to staging buffer
	memcpy(indexStagingAllocResult.pMappedData, indices.data(),
	       (size_t)indexBufferSize);

	// Create device-local buffer
	VkBufferCreateInfo indexBufferInfo{};
	indexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	indexBufferInfo.size = indexBufferSize;
	indexBufferInfo.usage =
	    VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	indexBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo indexAllocInfo{};
	indexAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	indexAllocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

	if (vmaCreateBuffer(m_context->getAllocator(), &indexBufferInfo,
	                    &indexAllocInfo, &m_indexBuffer,
	                    &m_indexBufferAllocation, nullptr) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create index buffer!");
	}

	// Copy both buffers in a single command submission
	VkCommandBuffer commandBuffer = m_context->beginSingleTimeCommands();

	VkBufferCopy vertexCopyRegion{};
	vertexCopyRegion.size = vertexBufferSize;
	vkCmdCopyBuffer(commandBuffer, vertexStagingBuffer, m_vertexBuffer, 1,
	                &vertexCopyRegion);

	VkBufferCopy indexCopyRegion{};
	indexCopyRegion.size = indexBufferSize;
	vkCmdCopyBuffer(commandBuffer, indexStagingBuffer, m_indexBuffer, 1,
	                &indexCopyRegion);

	m_context->endSingleTimeCommands(commandBuffer);

	vmaDestroyBuffer(m_context->getAllocator(), vertexStagingBuffer,
	                 vertexStagingAllocation);
	vmaDestroyBuffer(m_context->getAllocator(), indexStagingBuffer,
	                 indexStagingAllocation);
}

void RenderSystem::gatherRenderItems(World& world,
                                     const CameraSystem& cameraSystem,
                                     MaterialManager& materialManager) {
	m_renderItems.clear();

	const glm::mat4 view = cameraSystem.getViewMatrix(world);
	const glm::vec3 viewDir = glm::vec3(
	    view[0][2], view[1][2], view[2][2]);  // third row = forward vector

	for (Entity entity :
	     world.view<Transform, MeshRenderer, MaterialBinding>()) {
		const Transform& transform = world.getComponent<Transform>(entity);
		const MeshRenderer& meshRenderer =
		    world.getComponent<MeshRenderer>(entity);
		const MaterialBinding& materialBinding =
		    world.getComponent<MaterialBinding>(entity);

		if (!meshRenderer.visible) continue;

		VkDescriptorSet descriptorSet =
		    materialManager.getDescriptorSet(materialBinding.materialID);

		for (uint32_t meshID : meshRenderer.getMeshIDs()) {
			auto it = m_meshes.find(meshID);
			if (it == m_meshes.end()) continue;

			// not actual depth, just relative for sorting
			const glm::vec3 worldPos = glm::vec3(transform.worldMatrix[3]);
			const float relativeDepth = glm::dot(viewDir, worldPos);

			m_renderItems.push_back({entity, meshID, descriptorSet,
			                         transform.worldMatrix, relativeDepth});
		}
	}

	// Front-to-back — smallest depth value first
	std::sort(m_renderItems.begin(), m_renderItems.end(),
	          [](const RenderItem& a, const RenderItem& b) {
		          return a.depth < b.depth;
	          });
}

void RenderSystem::recordCommandBuffer(VkCommandBuffer commandBuffer,
                                       uint32_t imageIndex,
                                       SwapChain& swapChain, World& world,
                                       const CameraSystem& cameraSystem,
                                       MaterialManager& materialManager) {
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
		throw std::runtime_error("Failed to begin recording command buffer!");
	}

	// Compute per-frame camera data once — shared across all passes
	const VkExtent2D extent = swapChain.getExtent();
	const float aspect = static_cast<float>(extent.width) / extent.height;
	const glm::mat4 view = cameraSystem.getViewMatrix(world);
	const glm::mat4 projection =
	    cameraSystem.getProjectionMatrix(world, aspect);
	const glm::vec3 cameraPos = glm::vec3(glm::inverse(view)[3]);

	updateFrameUBO(imageIndex, swapChain, view, projection, cameraPos);

	recordDepthPrepass(commandBuffer, view, projection, extent);
	recordGeometryPass(commandBuffer, view, projection, extent,
	                   materialManager);
	recordLightingPass(commandBuffer);
	recordTonemapPass(commandBuffer, imageIndex, swapChain);

	if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to record command buffer!");
	}
}

void RenderSystem::updateFrameUBO(uint32_t imageIndex, SwapChain& swapChain,
                                  const glm::mat4& view,
                                  const glm::mat4& projection,
                                  const glm::vec3& cameraPos) {
	const VkExtent2D extent = swapChain.getExtent();

	FrameUBO ubo{};
	ubo.view = view;
	ubo.projection = projection;
	ubo.viewProjection = projection * view;
	ubo.invProjection = glm::inverse(ubo.projection);
	ubo.invView = glm::inverse(ubo.view);
	ubo.cameraPosition = glm::vec4(cameraPos, 1.0f);
	ubo.screenExtent = glm::vec2(static_cast<float>(extent.width),
	                             static_cast<float>(extent.height));
	ubo.time = 0.0f;  // TODO: wire up engine time

	memcpy(m_frameUBOMapped[imageIndex], &ubo, sizeof(FrameUBO));
}

void RenderSystem::recordDepthPrepass(VkCommandBuffer commandBuffer,
                                      const glm::mat4& view,
                                      const glm::mat4& projection,
                                      VkExtent2D extent) {
	const float aspect = static_cast<float>(extent.width) / extent.height;

	VkRenderPassBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	beginInfo.renderPass = m_depthPrepass.renderPass;
	beginInfo.framebuffer = m_depthPrepass.framebuffer;
	beginInfo.renderArea.offset = {0, 0};
	beginInfo.renderArea.extent = extent;

	VkClearValue depthClear{};
	depthClear.depthStencil = {1.0f, 0};
	beginInfo.clearValueCount = 1;
	beginInfo.pClearValues = &depthClear;

	vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                  m_depthPrepass.pipeline);

	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(extent.width);
	viewport.height = static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor{{0, 0}, extent};
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	VkBuffer vertexBuffers[] = {m_vertexBuffer};
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
	vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

	for (const RenderItem& item : m_renderItems) {
		auto it = m_meshes.find(item.meshID);
		if (it == m_meshes.end()) continue;

		struct {
			glm::mat4 model;
			glm::mat4 view;
			glm::mat4 projection;
		} push;

		push.model = item.modelMatrix;
		push.view = view;
		push.projection = projection;

		vkCmdPushConstants(commandBuffer, m_depthPrepass.layout,
		                   VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

		const MeshInfo& mesh = it->second;
		vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, mesh.firstIndex,
		                 mesh.firstVertex, 0);
	}

	vkCmdEndRenderPass(commandBuffer);
}

void RenderSystem::recordGeometryPass(VkCommandBuffer commandBuffer,
                                      const glm::mat4& view,
                                      const glm::mat4& projection,
                                      VkExtent2D extent,
                                      MaterialManager& materialManager) {
	std::array<VkClearValue, 5> clearValues{};
	clearValues[0].color = {0.0f, 0.0f, 0.0f, 0.0f};  // baseColorMetallic
	clearValues[1].color = {0.0f, 0.0f, 0.0f, 0.0f};  // normals
	clearValues[2].color = {0.0f, 0.0f, 0.0f, 0.0f};  // roughnessAOSpecID
	clearValues[3].color = {0.0f, 0.0f, 0.0f, 0.0f};  // emissiveFlags
	clearValues[4].depthStencil = {1.0f, 0};          // depth

	VkRenderPassBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	beginInfo.renderPass = m_geometry.renderPass;
	beginInfo.framebuffer = m_gbuffer.framebuffer;
	beginInfo.renderArea.offset = {0, 0};
	beginInfo.renderArea.extent = extent;
	beginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	beginInfo.pClearValues = clearValues.data();

	vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                  m_geometry.pipeline);

	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(extent.width);
	viewport.height = static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor{{0, 0}, extent};
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	VkBuffer vertexBuffers[] = {m_vertexBuffer};
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
	vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer, 0, VK_INDEX_TYPE_UINT32);

	// Bind frame data — set 0, constant for all draws in this pass
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                        m_geometry.layout, 0, 1,
	                        &m_frameSets[m_currentImageIndex], 0, nullptr);

	for (const RenderItem& item : m_renderItems) {
		auto it = m_meshes.find(item.meshID);
		if (it == m_meshes.end()) continue;

		// Bind material — set 2
		if (item.descriptorSet != VK_NULL_HANDLE) {
			vkCmdBindDescriptorSets(
			    commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			    m_geometry.layout, 2, 1, &item.descriptorSet, 0, nullptr);
		}

		struct {
			glm::mat4 model;
			glm::mat4 view;
			glm::mat4 projection;
		} push;

		push.model = item.modelMatrix;
		push.view = view;
		push.projection = projection;

		vkCmdPushConstants(commandBuffer, m_geometry.layout,
		                   VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

		const MeshInfo& mesh = it->second;
		vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, mesh.firstIndex,
		                 mesh.firstVertex, 0);
	}

	vkCmdEndRenderPass(commandBuffer);
}

void RenderSystem::recordLightingPass(VkCommandBuffer commandBuffer) {
	VkClearValue clearValue{};
	clearValue.color = {0.0f, 0.0f, 0.0f, 1.0f};

	VkRenderPassBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	beginInfo.renderPass = m_lighting.renderPass;
	beginInfo.framebuffer = m_hdr.framebuffer;
	beginInfo.renderArea.offset = {0, 0};
	beginInfo.renderArea.extent = m_swapChain->getExtent();
	beginInfo.clearValueCount = 1;
	beginInfo.pClearValues = &clearValue;

	vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                  m_lighting.pipeline);

	VkExtent2D extent = m_swapChain->getExtent();
	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(extent.width);
	viewport.height = static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor{{0, 0}, extent};
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	// Set 0: frame data, Set 1: G-buffer textures
	std::array<VkDescriptorSet, 2> sets = {m_frameSets[m_currentImageIndex],
	                                       m_lightingSet};
	vkCmdBindDescriptorSets(
	    commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lighting.layout, 0,
	    static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

	// Fullscreen triangle — 3 vertices, no vertex buffer
	vkCmdDraw(commandBuffer, 3, 1, 0, 0);

	vkCmdEndRenderPass(commandBuffer);
}

void RenderSystem::recordTonemapPass(VkCommandBuffer commandBuffer,
                                     uint32_t imageIndex,
                                     SwapChain& swapChain) {
	VkClearValue clearValue{};
	clearValue.color = {0.0f, 0.0f, 0.0f, 1.0f};

	VkRenderPassBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	beginInfo.renderPass = m_tonemap.renderPass;
	beginInfo.framebuffer = swapChain.getFramebuffers()[imageIndex];
	beginInfo.renderArea.offset = {0, 0};
	beginInfo.renderArea.extent = swapChain.getExtent();
	beginInfo.clearValueCount = 1;
	beginInfo.pClearValues = &clearValue;

	vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                  m_tonemap.pipeline);

	VkExtent2D extent = swapChain.getExtent();
	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(extent.width);
	viewport.height = static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor{{0, 0}, extent};
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	// Set 1: HDR target
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                        m_tonemap.layout, 0, 1, &m_tonemapSet, 0, nullptr);

	// Fullscreen triangle - 3 vertices, no vertex buffer
	vkCmdDraw(commandBuffer, 3, 1, 0, 0);

	vkCmdEndRenderPass(commandBuffer);
}

VkShaderModule RenderSystem::createShaderModule(const std::vector<char>& code) {
	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(m_context->getDevice(), &createInfo, nullptr,
	                         &shaderModule) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create shader module!");
	}

	return shaderModule;
}