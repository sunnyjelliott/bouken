#pragma once
#include "gpubuffer.h"

// DEVICE_LOCAL. Written via staging buffer + vkCmdCopyBuffer.
// Grow records a full-content copy into a provided command buffer;
// caller destroys the returned old allocation after submit completes.
struct DeviceBuffer : GpuBuffer {
	VkBufferUsageFlags usage =
	    0;  // stored so grow can recreate with same usage

	void allocate(VulkanContext& context, VkDeviceSize capacity,
	              VkBufferUsageFlags usage);

	// Appends data from a staging buffer into this buffer at the current size
	// offset. Caller is responsible for creating and destroying the staging
	// buffer.
	void appendFromStaging(VkCommandBuffer cmdBuffer, VkBuffer stagingBuffer,
	                       VkDeviceSize dataSize);

	// Grows to newCapacity, recording a full-content blit into cmdBuffer.
	// Returns old handles for deferred destruction after submit completes.
	// Logs on grow.
	// TODO: Candidate for structured debug logging system.
	OldBufferAllocation grow(VulkanContext& context, VkDeviceSize newCapacity,
	                         VkCommandBuffer cmdBuffer);

	void destroy(VulkanContext& context);
};