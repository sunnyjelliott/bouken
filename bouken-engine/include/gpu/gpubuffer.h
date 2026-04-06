#pragma once
#include "pch.h"

typedef struct VmaAllocation_T* VmaAllocation;
class VulkanContext;

struct GpuBuffer {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
	VkDeviceSize capacity = 0;
	VkDeviceSize size = 0;

	bool isValid() const { return buffer != VK_NULL_HANDLE; }
	bool hasCapacity(VkDeviceSize required) const {
		return size + required <= capacity;
	}
};

struct OldBufferAllocation {
	VkBuffer buffer = VK_NULL_HANDLE;
	VmaAllocation allocation = VK_NULL_HANDLE;
};