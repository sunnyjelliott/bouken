#include "gpu/hostbuffer.h"
#include <vk_mem_alloc.h>
#include "vulkancontext.h"

void HostBuffer::allocate(VulkanContext& context, VkDeviceSize cap,
                          VkBufferUsageFlags usage) {
	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = cap;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
	                  VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo result{};
	if (vmaCreateBuffer(context.getAllocator(), &bufferInfo, &allocInfo,
	                    &buffer, &allocation, &result) != VK_SUCCESS) {
		throw std::runtime_error("HostBuffer: failed to allocate buffer");
	}

	mapped = result.pMappedData;
	capacity = cap;
	size = 0;
	memset(mapped, 0, capacity);
}

OldBufferAllocation HostBuffer::grow(VulkanContext& context,
                                     VkDeviceSize newCapacity) {
	std::cout << "HostBuffer: growing " << capacity << " -> " << newCapacity
	          << " bytes" << std::endl;
	// TODO: Candidate for structured debug logging system.

	OldBufferAllocation old{buffer, allocation};

	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = newCapacity;
	bufferInfo.usage =
	    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;  // preserved by caller
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
	                  VMA_ALLOCATION_CREATE_MAPPED_BIT;

	VmaAllocationInfo result{};
	if (vmaCreateBuffer(context.getAllocator(), &bufferInfo, &allocInfo,
	                    &buffer, &allocation, &result) != VK_SUCCESS) {
		throw std::runtime_error("HostBuffer: failed to grow buffer");
	}

	void* newMapped = result.pMappedData;
	memcpy(newMapped, mapped, static_cast<size_t>(size));
	memset(static_cast<uint8_t*>(newMapped) + size, 0, newCapacity - size);

	mapped = newMapped;
	capacity = newCapacity;

	return old;
}

void HostBuffer::destroy(VulkanContext& context) {
	if (buffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(context.getAllocator(), buffer, allocation);
		buffer = VK_NULL_HANDLE;
		allocation = VK_NULL_HANDLE;
		mapped = nullptr;
		capacity = 0;
		size = 0;
	}
}