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

	AABB aabb;
	for (const Vertex& v : vertices) {
		aabb.min = glm::min(aabb.min, v.position);
		aabb.max = glm::max(aabb.max, v.position);
	}
	m_meshAABBs[meshID] = aabb;

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

	// Grow if needed - self-contained submit, GPU idle on return
	auto growIfNeeded = [&](DeviceBuffer& buf, VkDeviceSize required) {
		if (buf.size + required <= buf.capacity) return;
		VkDeviceSize newCapacity = buf.capacity;
		while (newCapacity < buf.size + required) newCapacity *= 2;
		OldBufferAllocation old = buf.grow(*m_context, newCapacity);
		vmaDestroyBuffer(m_context->getAllocator(), old.buffer, old.allocation);
	};

	growIfNeeded(m_vertexBuffer, newVertexBytes);
	growIfNeeded(m_indexBuffer, newIndexBytes);

	// Single command buffer for both appends
	VkCommandBuffer cmd = m_context->beginSingleTimeCommands();

	auto stageAndAppend = [&](DeviceBuffer& buf, const void* data,
	                          VkDeviceSize dataSize) {
		VkBuffer staging;
		VmaAllocation stagingAlloc;
		VmaAllocationInfo stagingResult{};

		VkBufferCreateInfo stagingInfo{};
		stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		stagingInfo.size = dataSize;
		stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
		stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VmaAllocationCreateInfo stagingAllocInfo{};
		stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
		stagingAllocInfo.flags =
		    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
		    VMA_ALLOCATION_CREATE_MAPPED_BIT;

		if (vmaCreateBuffer(m_context->getAllocator(), &stagingInfo,
		                    &stagingAllocInfo, &staging, &stagingAlloc,
		                    &stagingResult) != VK_SUCCESS) {
			throw std::runtime_error(
			    "flushMeshUploads: failed to create staging buffer");
		}

		memcpy(stagingResult.pMappedData, data, dataSize);
		buf.appendFromStaging(cmd, staging, dataSize);

		return std::make_pair(staging, stagingAlloc);
	};

	std::vector<std::pair<VkBuffer, VmaAllocation>> stagingToDestroy;

	if (newVertexBytes > 0)
		stagingToDestroy.push_back(stageAndAppend(
		    m_vertexBuffer, m_allVertices.data() + m_uploadedVertexCount,
		    newVertexBytes));

	if (newIndexBytes > 0)
		stagingToDestroy.push_back(stageAndAppend(
		    m_indexBuffer, m_allIndices.data() + m_uploadedIndexCount,
		    newIndexBytes));

	m_context->endSingleTimeCommands(cmd);  // GPU idle after this

	for (auto& [buf, alloc] : stagingToDestroy)
		vmaDestroyBuffer(m_context->getAllocator(), buf, alloc);

	m_uploadedVertexCount = static_cast<uint32_t>(m_allVertices.size());
	m_uploadedIndexCount = static_cast<uint32_t>(m_allIndices.size());
	m_meshBufferDirty = false;
}

AABB RenderSystem::getMeshAABB(uint32_t meshID) const {
	auto it = m_meshAABBs.find(meshID);
	return it != m_meshAABBs.end() ? it->second : AABB{};
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

	AABB aabb;
	for (const Vertex& v : loadedMesh.vertices) {
		aabb.min = glm::min(aabb.min, v.position);
		aabb.max = glm::max(aabb.max, v.position);
	}
	m_meshAABBs[meshID] = aabb;

	return meshID;
}
