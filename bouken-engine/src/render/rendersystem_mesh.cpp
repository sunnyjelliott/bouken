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
	m_meshBufferDirty = true;

	return meshID;
}

void RenderSystem::flushMeshUploads() {
	if (!m_meshBufferDirty) return;

	const VkDeviceSize newVertexBytes =
	    sizeof(Vertex) * (m_allVertices.size() - m_uploadedVertexCount);
	const VkDeviceSize newIndexBytes =
	    sizeof(uint32_t) * (m_allIndices.size() - m_uploadedIndexCount);

	if (newVertexBytes == 0 && newIndexBytes == 0) {
		m_meshBufferDirty = false;
		return;
	}

	// Grow buffers if needed (2x policy)
	// Recorded into the same command buffer as the append below.
	VkCommandBuffer cmd = m_context->beginSingleTimeCommands();

	std::vector<OldBufferAllocation> toDestroy;

	auto growIfNeeded = [&](DeviceBuffer& buf, VkDeviceSize required) {
		if (buf.size + required <= buf.capacity) return;
		VkDeviceSize newCapacity = buf.capacity;
		while (newCapacity < buf.size + required) newCapacity *= 2;
		OldBufferAllocation old = buf.grow(*m_context, newCapacity, cmd);
		toDestroy.push_back(buf.grow(*m_context, newCapacity, cmd));
	};

	growIfNeeded(m_vertexBuffer, newVertexBytes);
	growIfNeeded(m_indexBuffer, newIndexBytes);

	// Stage and append new vertices
	if (newVertexBytes > 0) {
		VkBuffer vertStaging;
		VmaAllocation vertStagingAlloc;
		VmaAllocationInfo vertStagingResult{};

		VkBufferCreateInfo stagingInfo{};
		stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		stagingInfo.size = newVertexBytes;
		stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo stagingAllocInfo{};
		stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		stagingAllocInfo.flags =
		    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
		    VMA_ALLOCATION_CREATE_MAPPED_BIT;

		if (vmaCreateBuffer(m_context->getAllocator(), &stagingInfo,
		                    &stagingAllocInfo, &vertStaging, &vertStagingAlloc,
		                    &vertStagingResult) != VK_SUCCESS) {
			throw std::runtime_error(
			    "flushMeshUploads: failed to create vertex staging buffer");
		}

		memcpy(vertStagingResult.pMappedData,
		       m_allVertices.data() + m_uploadedVertexCount, newVertexBytes);

		m_vertexBuffer.appendFromStaging(cmd, vertStaging, newVertexBytes);

		// Staging destroyed after submit
		vmaDestroyBuffer(m_context->getAllocator(), vertStaging,
		                 vertStagingAlloc);
	}

	// Stage and append new indices
	if (newIndexBytes > 0) {
		VkBuffer idxStaging;
		VmaAllocation idxStagingAlloc;
		VmaAllocationInfo idxStagingResult{};

		VkBufferCreateInfo stagingInfo{};
		stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		stagingInfo.size = newIndexBytes;
		stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo stagingAllocInfo{};
		stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		stagingAllocInfo.flags =
		    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
		    VMA_ALLOCATION_CREATE_MAPPED_BIT;

		if (vmaCreateBuffer(m_context->getAllocator(), &stagingInfo,
		                    &stagingAllocInfo, &idxStaging, &idxStagingAlloc,
		                    &idxStagingResult) != VK_SUCCESS) {
			throw std::runtime_error(
			    "flushMeshUploads: failed to create index staging buffer");
		}

		memcpy(idxStagingResult.pMappedData,
		       m_allIndices.data() + m_uploadedIndexCount, newIndexBytes);

		m_indexBuffer.appendFromStaging(cmd, idxStaging, newIndexBytes);

		vmaDestroyBuffer(m_context->getAllocator(), idxStaging,
		                 idxStagingAlloc);
	}

	m_context->endSingleTimeCommands(cmd);

	for (auto& old : toDestroy)
		vmaDestroyBuffer(m_context->getAllocator(), old.buffer, old.allocation);

	m_uploadedVertexCount = static_cast<uint32_t>(m_allVertices.size());
	m_uploadedIndexCount = static_cast<uint32_t>(m_allIndices.size());
	m_meshBufferDirty = false;
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

	m_meshBufferDirty = true;

	return meshID;
}
