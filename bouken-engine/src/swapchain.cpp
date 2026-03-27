#include "swapchain.h"

#include <vk_mem_alloc.h>

void SwapChain::init(VulkanContext& context, GLFWwindow* window, uint32_t width,
                     uint32_t height) {
	m_device = context.getDevice();
	m_allocator = context.getAllocator();
	createSwapChain(context, width, height);
	createImageViews(context);
}

void SwapChain::cleanup() {
	for (auto framebuffer : m_framebuffers) {
		vkDestroyFramebuffer(m_device, framebuffer, nullptr);
	}

	for (auto imageView : m_imageViews) {
		vkDestroyImageView(m_device, imageView, nullptr);
	}

	vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
}

void SwapChain::createFramebuffers(VkRenderPass renderPass) {
	m_framebuffers.resize(m_imageViews.size());
	for (size_t i = 0; i < m_imageViews.size(); i++) {
		VkImageView attachment = m_imageViews[i];
		VkFramebufferCreateInfo framebufferInfo{};
		framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		framebufferInfo.renderPass = renderPass;
		framebufferInfo.attachmentCount = 1;
		framebufferInfo.pAttachments = &attachment;
		framebufferInfo.width = m_extent.width;
		framebufferInfo.height = m_extent.height;
		framebufferInfo.layers = 1;

		if (vkCreateFramebuffer(m_device, &framebufferInfo, nullptr,
		                        &m_framebuffers[i]) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create swapchain framebuffer!");
		}
	}
}

void SwapChain::cleanupFramebuffers() {
	for (auto framebuffer : m_framebuffers) {
		vkDestroyFramebuffer(m_device, framebuffer, nullptr);
	}
}

void SwapChain::createSwapChain(VulkanContext& context, uint32_t width,
                                uint32_t height) {
	SwapChainSupportDetails swapChainSupport =
	    context.querySwapChainSupport(context.getPhysicalDevice());

	VkSurfaceFormatKHR surfaceFormat =
	    chooseSwapSurfaceFormat(swapChainSupport.formats);
	VkPresentModeKHR presentMode =
	    chooseSwapPresentMode(swapChainSupport.presentModes);
	VkExtent2D extent =
	    chooseSwapExtent(swapChainSupport.capabilities, width, height);

	// Request one more than minimum to avoid waiting on driver
	uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
	if (swapChainSupport.capabilities.maxImageCount > 0 &&
	    imageCount > swapChainSupport.capabilities.maxImageCount) {
		imageCount = swapChainSupport.capabilities.maxImageCount;
	}

	VkSwapchainCreateInfoKHR createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = context.getSurface();
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = surfaceFormat.format;
	createInfo.imageColorSpace = surfaceFormat.colorSpace;
	createInfo.imageExtent = extent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

	QueueFamilyIndices indices = context.getQueueFamilyIndices();
	uint32_t queueFamilyIndices[] = {indices.graphicsFamily.value(),
	                                 indices.presentFamily.value()};

	if (indices.graphicsFamily != indices.presentFamily) {
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = queueFamilyIndices;
	} else {
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		createInfo.queueFamilyIndexCount = 0;
		createInfo.pQueueFamilyIndices = nullptr;
	}

	createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = presentMode;
	createInfo.clipped = VK_TRUE;
	createInfo.oldSwapchain = VK_NULL_HANDLE;

	if (vkCreateSwapchainKHR(context.getDevice(), &createInfo, nullptr,
	                         &m_swapChain) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create swap chain!");
	}

	// Retrieve swap chain images
	vkGetSwapchainImagesKHR(context.getDevice(), m_swapChain, &imageCount,
	                        nullptr);
	m_images.resize(imageCount);
	vkGetSwapchainImagesKHR(context.getDevice(), m_swapChain, &imageCount,
	                        m_images.data());

	// Store format and extent for later use
	m_imageFormat = surfaceFormat.format;
	m_extent = extent;
}

void SwapChain::createImageViews(VulkanContext& context) {
	m_imageViews.resize(m_images.size());

	for (size_t i = 0; i < m_images.size(); i++) {
		VkImageViewCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		createInfo.image = m_images[i];

		// How to interpret the image data
		createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		createInfo.format = m_imageFormat;

		// Component mapping
		createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

		// Subresource range describes the image's purpose and which part to
		// access
		createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		createInfo.subresourceRange.baseMipLevel = 0;
		createInfo.subresourceRange.levelCount = 1;
		createInfo.subresourceRange.baseArrayLayer = 0;
		createInfo.subresourceRange.layerCount = 1;

		if (vkCreateImageView(context.getDevice(), &createInfo, nullptr,
		                      &m_imageViews[i]) != VK_SUCCESS) {
			throw std::runtime_error("Failed to create image views!");
		}
	}
}

VkSurfaceFormatKHR SwapChain::chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& availableFormats) {
	// Prefer SRGB if available
	for (const auto& availableFormat : availableFormats) {
		if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
		    availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
			return availableFormat;
		}
	}

	// Otherwise just use the first format
	return availableFormats[0];
}

VkPresentModeKHR SwapChain::chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR>& availablePresentModes) {
	// Prefer mailbox (triple buffering) if available
	for (const auto& availablePresentMode : availablePresentModes) {
		if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
			return availablePresentMode;
		}
	}

	// FIFO is guaranteed to be available
	return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D SwapChain::chooseSwapExtent(
    const VkSurfaceCapabilitiesKHR& capabilities, uint32_t width,
    uint32_t height) {
	if (capabilities.currentExtent.width !=
	    std::numeric_limits<uint32_t>::max()) {
		return capabilities.currentExtent;
	} else {
		VkExtent2D actualExtent = {static_cast<uint32_t>(width),
		                           static_cast<uint32_t>(height)};

		actualExtent.width =
		    std::clamp(actualExtent.width, capabilities.minImageExtent.width,
		               capabilities.maxImageExtent.width);
		actualExtent.height =
		    std::clamp(actualExtent.height, capabilities.minImageExtent.height,
		               capabilities.maxImageExtent.height);

		return actualExtent;
	}
}
