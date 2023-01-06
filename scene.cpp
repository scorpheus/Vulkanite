#include "texture.h"
#include "scene.h"

#include <functional>
#include <array>

#include "dlss.h"
#include "rasterizer.h"
#include "raytrace.h"

#include <cmrc/cmrc.hpp>
#include <glm/ext/matrix_transform.hpp>
CMRC_DECLARE(gltf_rc);

SceneVulkanite sceneGLTF;

void loadSceneGLTF() {
	sceneGLTF.envMap.name = "envMap";
	auto cmrcFS = cmrc::gltf_rc::get_filesystem();
	auto envmapRC = cmrcFS.open(ENVMAP);

	createTextureImage(reinterpret_cast<const unsigned char*>(envmapRC.cbegin()), envmapRC.size(), sceneGLTF.envMap.textureImage, sceneGLTF.envMap.textureImageMemory,
	                   sceneGLTF.envMap.mipLevels, true);
	sceneGLTF.envMap.textureImageView = createTextureImageView(sceneGLTF.envMap.textureImage, sceneGLTF.envMap.mipLevels, VK_FORMAT_R32G32B32A32_SFLOAT);
	createTextureSampler(sceneGLTF.envMap.textureSampler, sceneGLTF.envMap.mipLevels);

	sceneGLTF.roots = loadSceneGltf(MODEL_GLTF_PATH);
}

void initSceneGLTF() {

#ifdef DRAW_RASTERIZE
	DLSS_SCALE = 1.0;
#else
	// dlss
	initDLSS();
#endif

	// setup motion pass
	VkExtent3D extent = {static_cast<uint32_t>(swapChainExtent.width * DLSS_SCALE), static_cast<uint32_t>(swapChainExtent.height * DLSS_SCALE), 1};

	createStorageImage(sceneGLTF.storageImagesDepth, findDepthFormat(), VK_IMAGE_ASPECT_DEPTH_BIT, extent);
#ifdef DRAW_RASTERIZE
	createStorageImage(sceneGLTF.storageImagesRasterize, swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, extent);
	createRenderPass(sceneGLTF.renderPass, swapChainImageFormat, findDepthFormat(), VK_SAMPLE_COUNT_1_BIT);
#else
	createStorageImage(sceneGLTF.storageImagesMotionVector, VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, extent);
	createRenderPass(sceneGLTF.renderPass, VK_FORMAT_R32G32_SFLOAT, findDepthFormat(), VK_SAMPLE_COUNT_1_BIT);
#endif


#ifdef DRAW_RASTERIZE
	createFramebuffers(sceneGLTF.renderPass, sceneGLTF.rasterizerFramebuffers, sceneGLTF.storageImagesRasterize, sceneGLTF.storageImagesDepth);
#else
	createFramebuffers(sceneGLTF.renderPass, sceneGLTF.rasterizerFramebuffers, sceneGLTF.storageImagesMotionVector, sceneGLTF.storageImagesDepth);
#endif

	// create 2 graphics pipeline (without/without alpha);
#if !defined DRAW_RASTERIZE
	createDescriptorSetLayoutMotionVector(sceneGLTF.descriptorSetLayout);
	createGraphicsPipeline("spv/shaderMotionVector.vert.spv", "spv/shaderMotionVector.frag.spv", sceneGLTF.pipelineLayout, sceneGLTF.graphicsPipeline, sceneGLTF.renderPass, msaaSamples, sceneGLTF.descriptorSetLayout, false);
	createGraphicsPipeline("spv/shaderMotionVector.vert.spv", "spv/shaderMotionVector.frag.spv", sceneGLTF.pipelineLayoutAlpha, sceneGLTF.graphicsPipelineAlpha, sceneGLTF.renderPass, msaaSamples, sceneGLTF.descriptorSetLayout, true);
#endif

#ifdef DRAW_RASTERIZE
	createUniformParamsBuffers(sizeof(UBOParams), sceneGLTF.uniformParamsBuffers, sceneGLTF.uniformParamsBuffersMemory, sceneGLTF.uniformParamsBuffersMapped);
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		updateUniformParamsBuffer(sceneGLTF.uboParams, sceneGLTF.uniformParamsBuffersMapped, i);
#endif

	// load gltf
	loadSceneGLTF();

#if !defined DRAW_RASTERIZE
	// setup raytrace
	createStorageImage(sceneGLTF.storageImagesRaytrace, swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, extent);

	vulkanite_raytrace::InitRaytrace();

	// make blas
	std::function<void(const objectGLTF &)> makeBLASf;
	makeBLASf = [&](const objectGLTF &obj) {
		if (sceneGLTF.primsMeshCache[obj.primMesh])
			vulkanite_raytrace::createBottomLevelAccelerationStructure(obj);
		for (const auto &objChild : obj.children)
			makeBLASf(objChild);
	};
	for (const auto &o : sceneGLTF.roots)
		makeBLASf(o);

	// make las
	std::function<void(const objectGLTF &, const glm::mat4 &)> makeTLASf;
	makeTLASf = [&](const objectGLTF &obj, const glm::mat4 &parent_world) {
		if (sceneGLTF.primsMeshCache[obj.primMesh])
			vulkanite_raytrace::createTopLevelAccelerationStructureInstance(obj, obj.world * parent_world);
		for (const auto &objChild : obj.children)
			makeTLASf(objChild, obj.world * parent_world);
	};
	for (const auto &o : sceneGLTF.roots)
		makeTLASf(o, glm::mat4(1));

	vulkanite_raytrace::createTopLevelAccelerationStructure();

	vulkanite_raytrace::createUniformBuffer();
	vulkanite_raytrace::createRayTracingPipeline();
	vulkanite_raytrace::createShaderBindingTables();
	vulkanite_raytrace::createDescriptorSets();

#endif
}

void updateSceneGLTF(float deltaTime) {
	// move in circle one pion

	static auto startTime = std::chrono::high_resolution_clock::now();
	auto currentTime = std::chrono::high_resolution_clock::now();
	float timer = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count() * 70.f;

	glm::mat4 movingMat = glm::mat4(1.0f);
	sceneGLTF.roots[5].world = glm::translate(movingMat, glm::vec3(cos(glm::radians(timer)) * 0.1f, 0.014927f, sin(glm::radians(timer)) * 0.1f));
}

void drawModelGLTF(VkCommandBuffer commandBuffer, uint32_t currentFrame, objectGLTF &obj, const glm::mat4 &parent_world, const bool &isRenderingAlphaPass) {
	if (sceneGLTF.primsMeshCache[obj.primMesh] && ((sceneGLTF.materialsCache[obj.mat].alphaMask == 0.f && !isRenderingAlphaPass) || (
		                                               sceneGLTF.materialsCache[obj.mat].alphaMask != 0.f && isRenderingAlphaPass))) {
#ifdef DRAW_RASTERIZE
		updateUniformBuffer(currentFrame, obj, parent_world);
#else
		updateUniformBufferMotionVector(currentFrame, obj, parent_world);
#endif

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		                  sceneGLTF.materialsCache[obj.mat].alphaMask ? sceneGLTF.graphicsPipelineAlpha : sceneGLTF.graphicsPipeline);

		VkBuffer vertexBuffers[] = {sceneGLTF.primsMeshCache[obj.primMesh]->vertexBuffer};
		VkDeviceSize offsets[] = {0};
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

		vkCmdBindIndexBuffer(commandBuffer, sceneGLTF.primsMeshCache[obj.primMesh]->indexBuffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
		                        sceneGLTF.materialsCache[obj.mat].alphaMask ? sceneGLTF.pipelineLayoutAlpha : sceneGLTF.pipelineLayout, 0, 1, &obj.descriptorSets[currentFrame], 0,
		                        nullptr);

#ifdef DRAW_RASTERIZE
		vkCmdPushConstants(commandBuffer, sceneGLTF.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint32_t), &obj.mat);
#endif

		vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(sceneGLTF.primsMeshCache[obj.primMesh]->indices.size()), 1, 0, 0, 0);
	}

	for (auto &objChild : obj.children)
		drawModelGLTF(commandBuffer, currentFrame, objChild, obj.world * parent_world, isRenderingAlphaPass);
}

void drawSceneGLTF(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
	// draw opaque
	for (auto &obj : sceneGLTF.roots)
		drawModelGLTF(commandBuffer, currentFrame, obj, glm::mat4(1), false);
	// draw alpha
	for (auto &obj : sceneGLTF.roots)
		drawModelGLTF(commandBuffer, currentFrame, obj, glm::mat4(1), true);
}

void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT; // Optional
	beginInfo.pInheritanceInfo = nullptr; // Optional

	if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
		throw std::runtime_error("failed to begin recording command buffer!");
	}

	// rasterize motion vector/depth pass
	std::array<VkClearValue, 2> clearValues{};
	clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
	clearValues[1].depthStencil = {1.0f, 0};

	VkRenderPassBeginInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = sceneGLTF.renderPass;
	renderPassInfo.framebuffer = sceneGLTF.rasterizerFramebuffers[currentFrame];
	renderPassInfo.renderArea.offset = {0, 0};
	renderPassInfo.renderArea.extent = {static_cast<uint32_t>(swapChainExtent.width * DLSS_SCALE), static_cast<uint32_t>(swapChainExtent.height * DLSS_SCALE)};
	renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	renderPassInfo.pClearValues = clearValues.data();

	vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(swapChainExtent.width * DLSS_SCALE);
	viewport.height = static_cast<float>(swapChainExtent.height * DLSS_SCALE);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

	VkRect2D scissor{};
	scissor.offset = {0, 0};
	scissor.extent = {static_cast<uint32_t>(swapChainExtent.width * DLSS_SCALE), static_cast<uint32_t>(swapChainExtent.height * DLSS_SCALE)};
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

	drawSceneGLTF(commandBuffer, currentFrame);

	vkCmdEndRenderPass(commandBuffer);

#if !defined DRAW_RASTERIZE
	// raytrace
	vulkanite_raytrace::buildCommandBuffers(commandBuffer, currentFrame);

	// dll
	RenderDLSS(commandBuffer, currentFrame, 1.0);
#endif


	// Copy final output to swap chain image
	VkImageSubresourceRange subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

	// Prepare current swap chain image as transfer destination
	setImageLayout(commandBuffer, swapChainImages[currentFrame], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange);

	// Prepare ray tracing output image as transfer source
#ifdef DRAW_RASTERIZE
	setImageLayout(commandBuffer, sceneGLTF.storageImagesRasterize[currentFrame].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, subresourceRange);
#else
	setImageLayout(commandBuffer, sceneGLTF.storageImagesDLSS[currentFrame].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, subresourceRange);
#endif

	VkImageCopy copyRegion{};
	copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
	copyRegion.srcOffset = {0, 0, 0};
	copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
	// copyRegion.dstOffset = {static_cast<int32_t>(swapChainExtent.width / 2), 0, 0};
	// copyRegion.extent = {swapChainExtent.width / 2, swapChainExtent.height, 1};
	copyRegion.dstOffset = {0, 0, 0};
	copyRegion.extent = {swapChainExtent.width, swapChainExtent.height, 1};
#ifdef DRAW_RASTERIZE
	vkCmdCopyImage(commandBuffer, sceneGLTF.storageImagesRasterize[currentFrame].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChainImages[currentFrame],
	               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
#else
	vkCmdCopyImage(commandBuffer, sceneGLTF.storageImagesDLSS[currentFrame].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChainImages[currentFrame], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
#endif

	// Transition swap chain image back for presentation
	setImageLayout(commandBuffer, swapChainImages[currentFrame], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, subresourceRange);

	// Transition ray tracing output image back to general layout
#ifdef DRAW_RASTERIZE
	setImageLayout(commandBuffer, sceneGLTF.storageImagesRasterize[currentFrame].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, subresourceRange);
#else
	setImageLayout(commandBuffer, sceneGLTF.storageImagesDLSS[currentFrame].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, subresourceRange);
#endif


	// end command buffer
	if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
		throw std::runtime_error("failed to record command buffer!");
	}
}

void destroyScene() {
	vkDestroyDescriptorSetLayout(device, sceneGLTF.descriptorSetLayout, nullptr);
	vkDestroyPipeline(device, sceneGLTF.graphicsPipeline, nullptr);
	vkDestroyPipelineLayout(device, sceneGLTF.pipelineLayout, nullptr);
	vkDestroyPipeline(device, sceneGLTF.graphicsPipelineAlpha, nullptr);
	vkDestroyPipelineLayout(device, sceneGLTF.pipelineLayoutAlpha, nullptr);

	vkDestroyRenderPass(device, sceneGLTF.renderPass, nullptr);

	for (auto framebuffer : sceneGLTF.rasterizerFramebuffers)
		vkDestroyFramebuffer(device, framebuffer, nullptr);

	deleteStorageImage(sceneGLTF.storageImagesDepth);
#ifdef DRAW_RASTERIZE
	deleteStorageImage(sceneGLTF.storageImagesRasterize);
#else
	deleteStorageImage(sceneGLTF.storageImagesRaytrace);
	deleteStorageImage(sceneGLTF.storageImagesDLSS);
	deleteStorageImage(sceneGLTF.storageImagesMotionVector);
#endif
}

void deleteModel() {
	std::function<void(objectGLTF &)> f;

	f = [=](objectGLTF &obj) {
		if (sceneGLTF.primsMeshCache[obj.primMesh]) {
			for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
				if (i < obj.uniformBuffers.size())
					vkDestroyBuffer(device, obj.uniformBuffers[i], nullptr);
				if (i < obj.uniformBuffersMemory.size())
					vkFreeMemory(device, obj.uniformBuffersMemory[i], nullptr);
			}

			// TODO delete from the cache
			// vkDestroyBuffer(device, sceneGLTF.primsMeshCache[obj.primMesh]->indexBuffer, nullptr);
			// vkFreeMemory(device, sceneGLTF.primsMeshCache[obj.primMesh]->indexBufferMemory, nullptr);

			// vkDestroyBuffer(device, sceneGLTF.primsMeshCache[obj.primMesh]->vertexBuffer, nullptr);
			// vkFreeMemory(device, sceneGLTF.primsMeshCache[obj.primMesh]->vertexBufferMemory, nullptr);
		}
		for (const auto &objChild : obj.children)
			f(obj);
	};

	for (auto o : sceneGLTF.roots) {
		f(o);
	}
}
