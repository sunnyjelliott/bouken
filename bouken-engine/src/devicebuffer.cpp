#include "gpu/devicebuffer.h"
#include <vk_mem_alloc.h>
#include "vulkancontext.h"

void DeviceBuffer::allocate(VulkanContext& context, VkDeviceSize cap,
                            VkBufferUsageFlags bufUsage) {
	usage = bufUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
	        VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	// TRANSFER_SRC_BIT required so the buffer can be the source of a grow blit.

	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = cap;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

	if (vmaCreateBuffer(context.getAllocator(), &bufferInfo, &allocInfo,
	                    &buffer, &allocation, nullptr) != VK_SUCCESS) {
		throw std::runtime_error("DeviceBuffer: failed to allocate buffer");
	}

	capacity = cap;
	size = 0;
}

void DeviceBuffer::appendFromStaging(VkCommandBuffer cmdBuffer,
                                     VkBuffer stagingBuffer,
                                     VkDeviceSize dataSize) {
	VkBufferCopy region{};
	region.srcOffset = 0;
	region.dstOffset = size;
	region.size = dataSize;
	vkCmdCopyBuffer(cmdBuffer, stagingBuffer, buffer, 1, &region);
	size += dataSize;
}

OldBufferAllocation DeviceBuffer::grow(VulkanContext& context,
                                       VkDeviceSize newCapacity) {
	std::cout << "DeviceBuffer: growing " << capacity << " -> " << newCapacity
	          << " bytes" << std::endl;
	// TODO: Candidate for structured debug logging system.

	OldBufferAllocation old{buffer, allocation};
	VkDeviceSize oldSize = size;

	VkBufferCreateInfo bufferInfo{};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = newCapacity;
	bufferInfo.usage = usage;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocInfo{};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;

	if (vmaCreateBuffer(context.getAllocator(), &bufferInfo, &allocInfo,
	                    &buffer, &allocation, nullptr) != VK_SUCCESS) {
		throw std::runtime_error("DeviceBuffer: failed to grow buffer");
	}

	capacity = newCapacity;
	size = oldSize;

	// Blit existing content into the new buffer, submit, and wait.
	// This must complete before the caller issues further commands
	// against the new buffer, and before the old buffer is destroyed.
	if (oldSize > 0) {
		VkCommandBuffer cmd = context.beginSingleTimeCommands();
		VkBufferCopy region{};
		region.size = oldSize;
		vkCmdCopyBuffer(cmd, old.buffer, buffer, 1, &region);
		context.endSingleTimeCommands(cmd);  // blocks until idle
	}

	return old;  // caller destroys after this returns - GPU is already idle
}

void DeviceBuffer::destroy(VulkanContext& context) {
	if (buffer != VK_NULL_HANDLE) {
		vmaDestroyBuffer(context.getAllocator(), buffer, allocation);
		buffer = VK_NULL_HANDLE;
		allocation = VK_NULL_HANDLE;
		capacity = 0;
		size = 0;
		usage = 0;
	}
}