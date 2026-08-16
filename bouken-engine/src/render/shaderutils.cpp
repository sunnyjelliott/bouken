#include "render/shaderutils.h"
#include "vulkancontext.h"

std::vector<char> ShaderUtils::readFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);
	if (!file.is_open()) {
		throw std::runtime_error("Failed to open file: " + filename);
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	return buffer;
}

VkShaderModule ShaderUtils::createShaderModule(VulkanContext& context,
                                               const std::vector<char>& code) {
	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VkShaderModule module;
	if (vkCreateShaderModule(context.getDevice(), &createInfo, nullptr,
	                         &module) != VK_SUCCESS) {
		throw std::runtime_error("Failed to create shader module!");
	}
	return module;
}