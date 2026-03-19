#include "vulkancontext.h"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

void VulkanContext::init(GLFWwindow* window) {
	createInstance();
	setupDebugMessenger();
	createSurface(window);
	pickPhysicalDevice();
	createLogicalDevice();
	createAllocator();
	createCommandPool();
}

void VulkanContext::cleanup() {
	vkDestroyCommandPool(m_device, m_commandPool, nullptr);
	vmaDestroyAllocator(m_allocator);
	vkDestroyDevice(m_device, nullptr);

	if (m_enableValidationLayers) {
		auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
		    m_instance, "vkDestroyDebugUtilsMessengerEXT");
		if (func != nullptr) {
			func(m_instance, m_debugMessenger, nullptr);
		}
	}

	vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
	vkDestroyInstance(m_instance, nullptr);
}

void VulkanContext::createInstance() {
	if (m_enableValidationLayers && !checkValidationLayerSupport()) {
		throw std::runtime_error(
		    "Validation layers requested, but not available.");
	}

	// Application info
	VkApplicationInfo appInfo{};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Bunny Engine";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "No Engine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_3;

	// Instance create info
	VkInstanceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	// Required GLFW extensions
	auto extensions = getRequiredExtensions();
	createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
	createInfo.ppEnabledExtensionNames = extensions.data();

	// Add debug messenger for instance creation/destruction
	VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
	if (m_enableValidationLayers) {
		createInfo.enabledLayerCount =
		    static_cast<uint32_t>(m_validationLayers.size());
		createInfo.ppEnabledLayerNames = m_validationLayers.data();

		populateDebugMessengerCreateInfo(debugCreateInfo);
		createInfo.pNext =
		    (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
	} else {
		createInfo.enabledLayerCount = 0;
		createInfo.pNext = nullptr;
	}

	if (vkCreateInstance(&createInfo, nullptr, &m_instance) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create Vulkan instance!");
	}
}

void VulkanContext::setupDebugMessenger() {
	if (!m_enableValidationLayers) return;

	VkDebugUtilsMessengerCreateInfoEXT createInfo{};
	populateDebugMessengerCreateInfo(createInfo);

	// Load extension function
	auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
	    m_instance, "vkCreateDebugUtilsMessengerEXT");

	if (func == nullptr) {
		throw std::runtime_error(
		    "Failed to load vkCreateDebugUtilsMessengerEXT!");
	}

	if (func(m_instance, &createInfo, nullptr, &m_debugMessenger) !=
	    VK_SUCCESS) {
		throw std::runtime_error("Failed to set up debug messenger!");
	}
}

void VulkanContext::createSurface(GLFWwindow* window) {
	if (glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface) !=
	    VK_SUCCESS) {
		throw std::runtime_error("Failed to create window surface!");
	}
}

void VulkanContext::pickPhysicalDevice() {
	uint32_t deviceCount = 0;
	vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);

	if (deviceCount == 0) {
		throw std::runtime_error("Failed to find GPUs with Vulkan support.");
	}

	std::vector<VkPhysicalDevice> devices(deviceCount);
	vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

	for (const auto& device : devices) {
		if (isDeviceSuitable(device)) {
			m_physicalDevice = device;
			break;
		}
	}

	if (m_physicalDevice == VK_NULL_HANDLE) {
		throw std::runtime_error("Failed to find a suitable GPU.");
	}

	// Print selected device info
	VkPhysicalDeviceProperties deviceProperties;
	vkGetPhysicalDeviceProperties(m_physicalDevice, &deviceProperties);
	std::cout << "Selected GPU: " << deviceProperties.deviceName << std::endl;
}

void VulkanContext::createLogicalDevice() {
	QueueFamilyIndices indices = findQueueFamilies(m_physicalDevice);

	// Create queue create infos
	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(),
	                                          indices.presentFamily.value()};

	float queuePriority = 1.0f;
	for (uint32_t queueFamily : uniqueQueueFamilies) {
		VkDeviceQueueCreateInfo queueCreateInfo{};
		queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueCreateInfo.queueFamilyIndex = queueFamily;
		queueCreateInfo.queueCount = 1;
		queueCreateInfo.pQueuePriorities = &queuePriority;
		queueCreateInfos.push_back(queueCreateInfo);
	}

	// Specify device features we want to use
	VkPhysicalDeviceFeatures deviceFeatures{};
	deviceFeatures.samplerAnisotropy = VK_TRUE;

	// Create logical device
	VkDeviceCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.queueCreateInfoCount =
	    static_cast<uint32_t>(queueCreateInfos.size());
	createInfo.pQueueCreateInfos = queueCreateInfos.data();
	createInfo.pEnabledFeatures = &deviceFeatures;

	// Device extensions
	createInfo.enabledExtensionCount =
	    static_cast<uint32_t>(m_deviceExtensions.size());
	createInfo.ppEnabledExtensionNames = m_deviceExtensions.data();

	if (m_enableValidationLayers) {
		createInfo.enabledLayerCount =
		    static_cast<uint32_t>(m_validationLayers.size());
		createInfo.ppEnabledLayerNames = m_validationLayers.data();
	} else {
		createInfo.enabledLayerCount = 0;
	}

	if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) !=
	    VK_SUCCESS) {
		throw std::runtime_error("Failed to create logical device.");
	}

	// Retrieve queue handles
	vkGetDeviceQueue(m_device, indices.graphicsFamily.value(), 0,
	                 &m_graphicsQueue);
	vkGetDeviceQueue(m_device, indices.presentFamily.value(), 0,
	                 &m_presentQueue);
}

void VulkanContext::createCommandPool() {
	QueueFamilyIndices queueFamilyIndices = findQueueFamilies(m_physicalDevice);

	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

	if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool) !=
	    VK_SUCCESS) {
		throw std::runtime_error("Failed to create command pool!");
	}
}

void VulkanContext::createAllocator() {
	VmaAllocatorCreateInfo allocatorInfo{};
	allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
	allocatorInfo.physicalDevice = m_physicalDevice;
	allocatorInfo.device = m_device;
	allocatorInfo.instance = m_instance;

	if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create VMA allocator!");
	}
}

bool VulkanContext::checkValidationLayerSupport() {
	uint32_t layerCount;
	vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

	std::vector<VkLayerProperties> availableLayers(layerCount);
	vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

	// Check if all requested layers are available
	for (const char* layerName : m_validationLayers) {
		bool layerFound = false;

		for (const auto& layerProperties : availableLayers) {
			if (strcmp(layerName, layerProperties.layerName) == 0) {
				layerFound = true;
				break;
			}
		}

		if (!layerFound) {
			return false;
		}
	}

	return true;
}

std::vector<const char*> VulkanContext::getRequiredExtensions() {
	uint32_t glfwExtensionCount = 0;
	const char** glfwExtensions;
	glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

	std::vector<const char*> extensions(glfwExtensions,
	                                    glfwExtensions + glfwExtensionCount);

	// Add debug utils extension if validation layers are enabled
	if (m_enableValidationLayers) {
		extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	return extensions;
}

void VulkanContext::populateDebugMessengerCreateInfo(
    VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
	createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo.messageSeverity =
	    VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
	    VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
	    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
	                         VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
	                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo.pfnUserCallback = debugCallback;
}

bool VulkanContext::isDeviceSuitable(VkPhysicalDevice device) {
	QueueFamilyIndices indices = findQueueFamilies(device);
	bool extensionsSupported = checkDeviceExtensionSupport(device);

	bool swapChainAdequate = false;
	if (extensionsSupported) {
		SwapChainSupportDetails swapChainSupport =
		    querySwapChainSupport(device);
		swapChainAdequate = !swapChainSupport.formats.empty() &&
		                    !swapChainSupport.presentModes.empty();
	}

	return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

bool VulkanContext::checkDeviceExtensionSupport(VkPhysicalDevice device) {
	uint32_t extensionCount;
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
	                                     nullptr);

	std::vector<VkExtensionProperties> availableExtensions(extensionCount);
	vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
	                                     availableExtensions.data());

	std::set<std::string> requiredExtensions(m_deviceExtensions.begin(),
	                                         m_deviceExtensions.end());

	for (const auto& extension : availableExtensions) {
		requiredExtensions.erase(extension.extensionName);
	}

	return requiredExtensions.empty();
}

QueueFamilyIndices VulkanContext::findQueueFamilies(
    VkPhysicalDevice device) const {
	QueueFamilyIndices indices;

	// Get all queue families
	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
	                                         nullptr);

	std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
	vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
	                                         queueFamilies.data());

	// Find queue families that support what we need
	int i = 0;
	for (const auto& queueFamily : queueFamilies) {
		// Check for graphics support
		if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			indices.graphicsFamily = i;
		}

		// Check for presentation support
		VkBool32 presentSupport = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_surface,
		                                     &presentSupport);
		if (presentSupport) {
			indices.presentFamily = i;
		}

		// Early exit if we found everything
		if (indices.isComplete()) {
			break;
		}

		i++;
	}

	return indices;
}

SwapChainSupportDetails VulkanContext::querySwapChainSupport(
    VkPhysicalDevice device) {
	SwapChainSupportDetails details;

	// Capabilities
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, m_surface,
	                                          &details.capabilities);

	// Formats
	uint32_t formatCount;
	vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount,
	                                     nullptr);
	if (formatCount != 0) {
		details.formats.resize(formatCount);
		vkGetPhysicalDeviceSurfaceFormatsKHR(device, m_surface, &formatCount,
		                                     details.formats.data());
	}

	// Present modes
	uint32_t presentModeCount;
	vkGetPhysicalDeviceSurfacePresentModesKHR(device, m_surface,
	                                          &presentModeCount, nullptr);
	if (presentModeCount != 0) {
		details.presentModes.resize(presentModeCount);
		vkGetPhysicalDeviceSurfacePresentModesKHR(
		    device, m_surface, &presentModeCount, details.presentModes.data());
	}

	return details;
}

VkCommandBuffer VulkanContext::beginSingleTimeCommands() {
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandPool = m_commandPool;
	allocInfo.commandBufferCount = 1;

	VkCommandBuffer commandBuffer;
	vkAllocateCommandBuffers(m_device, &allocInfo, &commandBuffer);

	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

	vkBeginCommandBuffer(commandBuffer, &beginInfo);

	return commandBuffer;
}

void VulkanContext::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
	vkEndCommandBuffer(commandBuffer);

	VkSubmitInfo submitInfo{};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;

	vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
	vkQueueWaitIdle(m_graphicsQueue);  // Wait for transfer to complete

	vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
	std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
	return VK_FALSE;
}
