#include "IBLSystem.h"
#include "vulkancontext.h"

namespace bouken {

namespace {

std::vector<char> readFile(const std::string& filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("IBLSystem: Failed to open file: " + filename);
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	return buffer;
}

// Describes everything needed to build one compute pipeline + its DSL.
// Bindings and push constant size are shader-specific; the build logic
// that turns this into Vulkan objects is identical across all four passes.
struct ComputePipelineDesc {
	const char* shaderPath;
	std::vector<VkDescriptorSetLayoutBinding> bindings;
	uint32_t pushConstantSize;
};

void buildComputePipeline(VulkanContext& context,
                          const ComputePipelineDesc& desc,
                          VkDescriptorSetLayout& outDSL,
                          VkPipelineLayout& outLayout,
                          VkPipeline& outPipeline) {
	// --- Descriptor set layout ---
	VkDescriptorSetLayoutCreateInfo dslInfo{};
	dslInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	dslInfo.bindingCount = static_cast<uint32_t>(desc.bindings.size());
	dslInfo.pBindings = desc.bindings.data();

	if (vkCreateDescriptorSetLayout(context.getDevice(), &dslInfo, nullptr,
	                                &outDSL) != VK_SUCCESS)
		throw std::runtime_error(
		    std::string("IBLSystem: failed to create DSL for ") +
		    desc.shaderPath);

	// --- Pipeline layout (DSL + push constant range) ---
	VkPushConstantRange pushRange{};
	pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	pushRange.offset = 0;
	pushRange.size = desc.pushConstantSize;

	VkPipelineLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	layoutInfo.setLayoutCount = 1;
	layoutInfo.pSetLayouts = &outDSL;
	layoutInfo.pushConstantRangeCount = desc.pushConstantSize > 0 ? 1 : 0;
	layoutInfo.pPushConstantRanges =
	    desc.pushConstantSize > 0 ? &pushRange : nullptr;

	if (vkCreatePipelineLayout(context.getDevice(), &layoutInfo, nullptr,
	                           &outLayout) != VK_SUCCESS)
		throw std::runtime_error(
		    std::string("IBLSystem: failed to create pipeline layout for ") +
		    desc.shaderPath);

	// --- Shader module + pipeline ---
	auto code =
	    readFile(desc.shaderPath);  // matches RenderSystem::readFile pattern

	VkShaderModuleCreateInfo moduleInfo{};
	moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	moduleInfo.codeSize = code.size();
	moduleInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VkShaderModule module = VK_NULL_HANDLE;
	if (vkCreateShaderModule(context.getDevice(), &moduleInfo, nullptr,
	                         &module) != VK_SUCCESS)
		throw std::runtime_error(
		    std::string("IBLSystem: failed to create shader module for ") +
		    desc.shaderPath);

	VkPipelineShaderStageCreateInfo stageInfo{};
	stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
	stageInfo.module = module;
	stageInfo.pName = "main";

	VkComputePipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
	pipelineInfo.stage = stageInfo;
	pipelineInfo.layout = outLayout;

	if (vkCreateComputePipelines(context.getDevice(), VK_NULL_HANDLE, 1,
	                             &pipelineInfo, nullptr,
	                             &outPipeline) != VK_SUCCESS)
		throw std::runtime_error(
		    std::string("IBLSystem: failed to create compute pipeline for ") +
		    desc.shaderPath);

	vkDestroyShaderModule(context.getDevice(), module, nullptr);
}

}  // anonymous namespace

void IBLSystem::createComputePipelines() {
	// --- Equirect -> Cubemap ---
	ComputePipelineDesc equirectDesc{};
	equirectDesc.shaderPath = "shaders/ibl_equirect_to_cubemap_comp.spv";
	equirectDesc.bindings = {
	    {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
	     VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	    {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
	     nullptr},
	};
	equirectDesc.pushConstantSize = sizeof(uint32_t);  // { faceSize }

	buildComputePipeline(m_context, equirectDesc, m_equirectToCubemapDSL,
	                     m_equirectToCubemapLayout,
	                     m_equirectToCubemapPipeline);

	// --- SH Projection ---
	ComputePipelineDesc shDesc{};
	shDesc.shaderPath = "shaders/ibl_sh_project_comp.spv";
	shDesc.bindings = {
	    {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
	     VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	    {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
	     nullptr},
	    {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
	     nullptr},
	};
	shDesc.pushConstantSize =
	    sizeof(uint32_t) * 3;  // { pass, faceSize, numWorkgroups }

	buildComputePipeline(m_context, shDesc, m_shProjectDSL, m_shProjectLayout,
	                     m_shProjectPipeline);

	// --- Prefilter ---
	ComputePipelineDesc prefilterDesc{};
	prefilterDesc.shaderPath = "shaders/ibl_prefilter_comp.spv";
	prefilterDesc.bindings = {
	    {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
	     VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
	    {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
	     nullptr},
	};
	prefilterDesc.pushConstantSize =
	    sizeof(uint32_t) * 3 +
	    sizeof(float);  // { faceSize, sourceFaceSize, sampleCount, roughness }

	buildComputePipeline(m_context, prefilterDesc, m_prefilterDSL,
	                     m_prefilterLayout, m_prefilterPipeline);

	// --- BRDF LUT ---
	ComputePipelineDesc brdfDesc{};
	brdfDesc.shaderPath = "shaders/ibl_brdf_lut_comp.spv";
	brdfDesc.bindings = {
	    {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT,
	     nullptr},
	};
	brdfDesc.pushConstantSize =
	    sizeof(uint32_t) * 2;  // { resolution, sampleCount }

	buildComputePipeline(m_context, brdfDesc, m_brdfLutDSL, m_brdfLutLayout,
	                     m_brdfLutPipeline);

	// --- Transient descriptor pool ---
	// Worst case across a single loadEnvironment() call:
	//   equirect(1) + SH(1) + prefilter(prefilteredMipLevels, one per mip)
	//   + BRDF LUT(1, init-only)
	// Descriptor type totals across all bindings above:
	//   COMBINED_IMAGE_SAMPLER: equirect(1) + SH(1) + prefilter(N) = N + 2
	//   STORAGE_IMAGE:          equirect(1) + prefilter(N) + BRDF LUT(1) = N +
	//   2 STORAGE_BUFFER:         SH(2) = 2
	const uint32_t prefilterMips = m_config.prefilteredMipLevels;
	const uint32_t maxSets =
	    prefilterMips + 3;  // equirect + SH + BRDF LUT + N prefilter mips

	std::vector<VkDescriptorPoolSize> poolSizes = {
	    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, prefilterMips + 2},
	    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, prefilterMips + 2},
	    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
	};

	m_computeDescriptorPool = m_context.createDescriptorPool(poolSizes, 8);
}

void IBLSystem::destroyComputePipelines() {
	vkDestroyDescriptorPool(m_context.getDevice(), m_computeDescriptorPool,
	                        nullptr);
	m_computeDescriptorPool = VK_NULL_HANDLE;

	auto destroyPipeline = [this](VkPipeline& pipeline,
	                              VkPipelineLayout& layout,
	                              VkDescriptorSetLayout& dsl) {
		vkDestroyPipeline(m_context.getDevice(), pipeline, nullptr);
		vkDestroyPipelineLayout(m_context.getDevice(), layout, nullptr);
		vkDestroyDescriptorSetLayout(m_context.getDevice(), dsl, nullptr);
		pipeline = VK_NULL_HANDLE;
		layout = VK_NULL_HANDLE;
		dsl = VK_NULL_HANDLE;
	};

	destroyPipeline(m_equirectToCubemapPipeline, m_equirectToCubemapLayout,
	                m_equirectToCubemapDSL);
	destroyPipeline(m_shProjectPipeline, m_shProjectLayout, m_shProjectDSL);
	destroyPipeline(m_prefilterPipeline, m_prefilterLayout, m_prefilterDSL);
	destroyPipeline(m_brdfLutPipeline, m_brdfLutLayout, m_brdfLutDSL);
}

}  // namespace bouken