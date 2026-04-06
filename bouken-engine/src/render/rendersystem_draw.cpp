#include "camerasystem.h"
#include "render/framedata.h"
#include "render/rendersystem.h"

void RenderSystem::recordCommandBuffer(VkCommandBuffer commandBuffer,
                                       uint32_t imageIndex,
                                       SwapChain& swapChain, World& world,
                                       const CameraSystem& cameraSystem,
                                       MaterialManager& materialManager) {
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
		throw std::runtime_error("Failed to begin recording command buffer!");
	}

	// Compute per-frame camera data once — shared across all passes
	const VkExtent2D extent = swapChain.getExtent();
	const float aspect = static_cast<float>(extent.width) / extent.height;
	const glm::mat4 view = cameraSystem.getViewMatrix(world);
	const glm::mat4 projection =
	    cameraSystem.getProjectionMatrix(world, aspect);
	const glm::vec3 cameraPos = glm::vec3(glm::inverse(view)[3]);

	updateFrameUBO(imageIndex, swapChain, view, projection, cameraPos);

	recordDepthPrepass(commandBuffer, view, projection, extent);
	recordGeometryPass(commandBuffer, view, projection, extent,
	                   materialManager);
	recordLightingPass(commandBuffer);
	recordTonemapPass(commandBuffer, imageIndex, swapChain);

	if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
		throw std::runtime_error("Failed to record command buffer!");
	}
}

void RenderSystem::updateFrameUBO(uint32_t imageIndex, SwapChain& swapChain,
                                  const glm::mat4& view,
                                  const glm::mat4& projection,
                                  const glm::vec3& cameraPos) {
	const VkExtent2D extent = swapChain.getExtent();

	FrameUBO ubo{};
	ubo.view = view;
	ubo.projection = projection;
	ubo.viewProjection = projection * view;
	ubo.invProjection = glm::inverse(ubo.projection);
	ubo.invView = glm::inverse(ubo.view);
	ubo.cameraPosition = glm::vec4(cameraPos, 1.0f);
	ubo.screenExtent = glm::vec2(static_cast<float>(extent.width),
	                             static_cast<float>(extent.height));
	ubo.time = 0.0f;  // TODO: wire up engine time

	memcpy(m_frameUBOMapped[imageIndex], &ubo, sizeof(FrameUBO));
}

void RenderSystem::recordDepthPrepass(VkCommandBuffer commandBuffer,
                                      const glm::mat4& view,
                                      const glm::mat4& projection,
                                      VkExtent2D extent) {
	const float aspect = static_cast<float>(extent.width) / extent.height;

	VkRenderPassBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	beginInfo.renderPass = m_depthPrepass.renderPass;
	beginInfo.framebuffer = m_depthPrepass.framebuffer;
	beginInfo.renderArea.offset = {0, 0};
	beginInfo.renderArea.extent = extent;

	VkClearValue depthClear{};
	depthClear.depthStencil = {1.0f, 0};
	beginInfo.clearValueCount = 1;
	beginInfo.pClearValues = &depthClear;

	vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                  m_depthPrepass.pipeline);

	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(extent.width);
	viewport.height = static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor{{0, 0}, extent};
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	VkBuffer vertexBuffers[] = {m_vertexBuffer.buffer};
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
	vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer.buffer, 0,
	                     VK_INDEX_TYPE_UINT32);

	for (const RenderItem& item : m_renderItems) {
		auto it = m_meshes.find(item.meshID);
		if (it == m_meshes.end()) continue;

		struct {
			glm::mat4 model;
			glm::mat4 view;
			glm::mat4 projection;
		} push;

		push.model = item.modelMatrix;
		push.view = view;
		push.projection = projection;

		vkCmdPushConstants(commandBuffer, m_depthPrepass.layout,
		                   VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

		const MeshInfo& mesh = it->second;
		vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, mesh.firstIndex,
		                 mesh.firstVertex, 0);
	}

	vkCmdEndRenderPass(commandBuffer);
}

void RenderSystem::recordGeometryPass(VkCommandBuffer commandBuffer,
                                      const glm::mat4& view,
                                      const glm::mat4& projection,
                                      VkExtent2D extent,
                                      MaterialManager& materialManager) {
	std::array<VkClearValue, 5> clearValues{};
	clearValues[0].color = {0.0f, 0.0f, 0.0f, 0.0f};  // baseColorMetallic
	clearValues[1].color = {0.0f, 0.0f, 0.0f, 0.0f};  // normals
	clearValues[2].color = {0.0f, 0.0f, 0.0f, 0.0f};  // roughnessAOSpecID
	clearValues[3].color = {0.0f, 0.0f, 0.0f, 0.0f};  // emissiveFlags
	clearValues[4].depthStencil = {1.0f, 0};          // depth

	VkRenderPassBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	beginInfo.renderPass = m_geometry.renderPass;
	beginInfo.framebuffer = m_gbuffer.framebuffer;
	beginInfo.renderArea.offset = {0, 0};
	beginInfo.renderArea.extent = extent;
	beginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	beginInfo.pClearValues = clearValues.data();

	vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                  m_geometry.pipeline);

	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(extent.width);
	viewport.height = static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor{{0, 0}, extent};
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	VkBuffer vertexBuffers[] = {m_vertexBuffer.buffer};
	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
	vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer.buffer, 0,
	                     VK_INDEX_TYPE_UINT32);

	// Bind frame data — set 0, constant for all draws in this pass
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                        m_geometry.layout, 0, 1,
	                        &m_frameSets[m_currentImageIndex], 0, nullptr);

	for (const RenderItem& item : m_renderItems) {
		auto it = m_meshes.find(item.meshID);
		if (it == m_meshes.end()) continue;

		// Bind material — set 2
		if (item.descriptorSet != VK_NULL_HANDLE) {
			vkCmdBindDescriptorSets(
			    commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			    m_geometry.layout, 2, 1, &item.descriptorSet, 0, nullptr);
		}

		struct {
			glm::mat4 model;
			glm::mat4 view;
			glm::mat4 projection;
		} push;

		push.model = item.modelMatrix;
		push.view = view;
		push.projection = projection;

		vkCmdPushConstants(commandBuffer, m_geometry.layout,
		                   VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);

		const MeshInfo& mesh = it->second;
		vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, mesh.firstIndex,
		                 mesh.firstVertex, 0);
	}

	vkCmdEndRenderPass(commandBuffer);
}

void RenderSystem::recordLightingPass(VkCommandBuffer commandBuffer) {
	VkClearValue clearValue{};
	clearValue.color = {0.0f, 0.0f, 0.0f, 1.0f};

	VkRenderPassBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	beginInfo.renderPass = m_lighting.renderPass;
	beginInfo.framebuffer = m_hdr.framebuffer;
	beginInfo.renderArea.offset = {0, 0};
	beginInfo.renderArea.extent = m_swapChain->getExtent();
	beginInfo.clearValueCount = 1;
	beginInfo.pClearValues = &clearValue;

	vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                  m_lighting.pipeline);

	VkExtent2D extent = m_swapChain->getExtent();
	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(extent.width);
	viewport.height = static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor{{0, 0}, extent};
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	// Set 0: frame data, Set 1: G-buffer textures
	std::array<VkDescriptorSet, 2> sets = {m_frameSets[m_currentImageIndex],
	                                       m_lightingSet};
	vkCmdBindDescriptorSets(
	    commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lighting.layout, 0,
	    static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

	// Fullscreen triangle — 3 vertices, no vertex buffer
	vkCmdDraw(commandBuffer, 3, 1, 0, 0);

	vkCmdEndRenderPass(commandBuffer);
}

void RenderSystem::recordTonemapPass(VkCommandBuffer commandBuffer,
                                     uint32_t imageIndex,
                                     SwapChain& swapChain) {
	VkClearValue clearValue{};
	clearValue.color = {0.0f, 0.0f, 0.0f, 1.0f};

	VkRenderPassBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	beginInfo.renderPass = m_tonemap.renderPass;
	beginInfo.framebuffer = swapChain.getFramebuffers()[imageIndex];
	beginInfo.renderArea.offset = {0, 0};
	beginInfo.renderArea.extent = swapChain.getExtent();
	beginInfo.clearValueCount = 1;
	beginInfo.pClearValues = &clearValue;

	vkCmdBeginRenderPass(commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                  m_tonemap.pipeline);

	VkExtent2D extent = swapChain.getExtent();
	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(extent.width);
	viewport.height = static_cast<float>(extent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor{{0, 0}, extent};
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	// Set 1: HDR target
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
	                        m_tonemap.layout, 0, 1, &m_tonemapSet, 0, nullptr);

	// Fullscreen triangle - 3 vertices, no vertex buffer
	vkCmdDraw(commandBuffer, 3, 1, 0, 0);

	vkCmdEndRenderPass(commandBuffer);
}