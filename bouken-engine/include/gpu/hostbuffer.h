#pragma once
#include "gpubuffer.h"

// HOST_VISIBLE | persistently mapped. Written directly by the CPU each frame.
// Grow is a blocking reallocate + memcpy — safe because host buffers are not
// in active GPU use when grown (called outside of frame submission).
struct HostBuffer : GpuBuffer {
	void* mapped = nullptr;

	void allocate(VulkanContext& context, VkDeviceSize capacity,
	              VkBufferUsageFlags usage);

	// Returns old allocation for caller to destroy after confirming GPU is
	// idle. Logs on grow.
	// TODO: Candidate for a structured debug logging system.
	OldBufferAllocation grow(VulkanContext& context, VkDeviceSize newCapacity);

	void destroy(VulkanContext& context);
};