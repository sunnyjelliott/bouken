#pragma once
#include "pch.h"
#include "vulkancontext.h"

typedef struct VmaAllocation_T* VmaAllocation;

class SwapChain {
   public:
	void init(VulkanContext& context, GLFWwindow* window, uint32_t width,
	          uint32_t height);
	void cleanup();

	// Getters
	VkSwapchainKHR getSwapChain() const { return m_swapChain; }
	VkFormat getImageFormat() const { return m_imageFormat; }
	VkExtent2D getExtent() const { return m_extent; }
	const std::vector<VkImageView>& getImageViews() const {
		return m_imageViews;
	}
	const std::vector<VkFramebuffer>& getFramebuffers() const {
		return m_framebuffers;
	}
	size_t getImageCount() const { return m_images.size(); }

	// Framebuffer management (needs render pass from outside)
	void createFramebuffers(VkRenderPass renderPass);
	void cleanupFramebuffers();

   private:
	void createSwapChain(VulkanContext& context, uint32_t width,
	                     uint32_t height);
	void createImageViews(VulkanContext& context);

	// Helper functions
	VkSurfaceFormatKHR chooseSwapSurfaceFormat(
	    const std::vector<VkSurfaceFormatKHR>& availableFormats);
	VkPresentModeKHR chooseSwapPresentMode(
	    const std::vector<VkPresentModeKHR>& availablePresentModes);
	VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities,
	                            uint32_t width, uint32_t height);
	// Vulkan objects
	VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
	std::vector<VkImage> m_images;
	std::vector<VkImageView> m_imageViews;
	VkFormat m_imageFormat;
	VkExtent2D m_extent;

	std::vector<VkFramebuffer> m_framebuffers;

	// Keep reference to device for cleanup
	VkDevice m_device = VK_NULL_HANDLE;
	VmaAllocator m_allocator = VK_NULL_HANDLE;

	// TODO: Add MSAA support - will need resolve attachments
	// TODO: HDR support - will need different format selection
};