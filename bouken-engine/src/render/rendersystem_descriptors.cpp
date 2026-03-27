#include "materialmanager.h"
#include "render/framedata.h"
#include "render/rendersystem.h"
#include "texturemanager.h"
#include "vulkantexturebackend.h"

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
		vkUpdateDescriptorSets(m_context->getDevice(), 7, writes.data(), 0,
		                       nullptr);

		// Store descriptor set in material
		materialManager.createDescriptorSet(matID, descriptorSet);
	}

	std::cout << "Created descriptor sets for materials" << std::endl;
}