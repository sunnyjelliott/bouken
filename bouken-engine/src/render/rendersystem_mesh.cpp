#include "meshloader.h"
#include "render/rendersystem.h"

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

void RenderSystem::flushMeshUploads() {
	if (!m_allVertices.empty() && !m_allIndices.empty()) {
		uploadMeshData(m_allVertices, m_allIndices);
	}
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
