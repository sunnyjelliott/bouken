#pragma once
#include "pch.h"

class VulkanContext;

namespace ShaderUtils {
std::vector<char> readFile(const std::string& filename);
VkShaderModule createShaderModule(VulkanContext& context,
                                  const std::vector<char>& code);
}  // namespace ShaderUtils