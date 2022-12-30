#include "loader.h"

#include <string>
#include <vector>
#include <array>
#include <stdexcept>
#include <chrono>
#include <cstring>

#include "core_utils.h"
#include "vertex_config.h"
#include "texture.h"
#include "VulkanBuffer.h"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>

#include <cmrc/cmrc.hpp>

#include "raytrace.h"
CMRC_DECLARE(gltf_rc);

#include "camera.h"
#include "loaderGltf.h"

struct UniformBufferObject {
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 invView;
	glm::mat4 proj;
};

struct UBOParams {
	glm::vec4 lightDir;
	float envRot;
	float exposure;
	glm::mat4 SHRed;
	glm::mat4 SHGreen;
	glm::mat4 SHBlue;
} uboParams;

VkBuffer uboParamsBuffer;

std::vector<VkBuffer> uniformParamsBuffers;
std::vector<VkDeviceMemory> uniformParamsBuffersMemory;
std::vector<void*> uniformParamsBuffersMapped;

static std::vector<char> readFile(const std::string &filename) {
	auto cmrcFS = cmrc::gltf_rc::get_filesystem();
	auto fileRC = cmrcFS.open(filename);
	std::vector<char> buffer;
	buffer.insert(buffer.begin(), fileRC.begin(), fileRC.end());
	return buffer;

	//std::ifstream file(filename, std::ios::ate | std::ios::binary);

	//if (!file.is_open()) {
	//	throw std::runtime_error("failed to open file!");
	//}
	//size_t fileSize = (size_t)file.tellg();
	//std::vector<char> buffer(fileSize);
	//file.seekg(0);
	//file.read(buffer.data(), fileSize);
	//file.close();

	//return buffer;
}

void createVertexBuffer(const std::vector<Vertex> &vertices, VkBuffer &vertexBuffer, VkDeviceMemory &vertexBufferMemory) {
	const VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer,
	             stagingBufferMemory);


	// fill the vertex buffer
	void *data;
	vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
	memcpy(data, vertices.data(), (size_t)bufferSize);
	vkUnmapMemory(device, stagingBufferMemory);

	createBuffer(bufferSize,
	             VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
	             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
	             vertexBuffer, vertexBufferMemory);

	copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

	vkDestroyBuffer(device, stagingBuffer, nullptr);
	vkFreeMemory(device, stagingBufferMemory, nullptr);
}

void createIndexBuffer(const std::vector<uint32_t> &indices, VkBuffer &indexBuffer, VkDeviceMemory &indexBufferMemory) {
	const VkDeviceSize bufferSize = sizeof(indices[0]) * indices.size();

	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer,
	             stagingBufferMemory);

	void *data;
	vkMapMemory(device, stagingBufferMemory, 0, bufferSize, 0, &data);
	memcpy(data, indices.data(), (size_t)bufferSize);
	vkUnmapMemory(device, stagingBufferMemory);

	createBuffer(bufferSize,
	             VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
	             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
	             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexBuffer, indexBufferMemory);

	copyBuffer(stagingBuffer, indexBuffer, bufferSize);

	vkDestroyBuffer(device, stagingBuffer, nullptr);
	vkFreeMemory(device, stagingBufferMemory, nullptr);
}

VkShaderModule createShaderModule(const std::vector<char> &code) {
	VkShaderModuleCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = code.size();
	createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

	VkShaderModule shaderModule;
	if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
		throw std::runtime_error("failed to create shader module!");
	}

	return shaderModule;
}

void createDescriptorSetLayout(VkDescriptorSetLayout &descriptorSetLayout) {
	VkDescriptorSetLayoutBinding uboLayoutBinding{};
	uboLayoutBinding.binding = 0;
	uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboLayoutBinding.descriptorCount = 1;
	uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	uboLayoutBinding.pImmutableSamplers = nullptr; // Optional

	VkDescriptorSetLayoutBinding uboParamsLayoutBinding{};
	uboParamsLayoutBinding.binding = 1;
	uboParamsLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	uboParamsLayoutBinding.descriptorCount = 1;
	uboParamsLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	uboParamsLayoutBinding.pImmutableSamplers = nullptr; // Optional

	VkDescriptorSetLayoutBinding samplerEnvMapLayoutBinding;
	samplerEnvMapLayoutBinding.binding = 2;
	samplerEnvMapLayoutBinding.descriptorCount = 1;
	samplerEnvMapLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerEnvMapLayoutBinding.pImmutableSamplers = nullptr;
	samplerEnvMapLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// all textures
	VkDescriptorSetLayoutBinding samplerTexturesLayoutBinding;
	samplerTexturesLayoutBinding.binding = 3;
	samplerTexturesLayoutBinding.descriptorCount = sceneGLTF.textureCache.size();
	samplerTexturesLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	samplerTexturesLayoutBinding.pImmutableSamplers = nullptr;
	samplerTexturesLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

	// all materials
	VkDescriptorSetLayoutBinding materialsLayoutBinding;
	materialsLayoutBinding.binding = 4;
	materialsLayoutBinding.descriptorCount = 1;
	materialsLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	materialsLayoutBinding.pImmutableSamplers = nullptr;
	materialsLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	
	std::array<VkDescriptorSetLayoutBinding, 5> bindings = {uboLayoutBinding, uboParamsLayoutBinding, samplerEnvMapLayoutBinding, samplerTexturesLayoutBinding, materialsLayoutBinding
	};
	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
	layoutInfo.pBindings = bindings.data();

	if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
		throw std::runtime_error("failed to create descriptor set layout!");
	}
}

void createDescriptorPool(VkDescriptorPool &descriptorPool) {
	std::array<VkDescriptorPoolSize, 5> poolSizes{};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[2].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	poolSizes[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[3].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	poolSizes[4].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	poolSizes[4].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	
	VkDescriptorPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	poolInfo.pPoolSizes = poolSizes.data();
	poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

	if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
		throw std::runtime_error("failed to create descriptor pool!");
	}
}

void createDescriptorSets(std::vector<VkDescriptorSet> &descriptorSets,
                          const std::vector<VkBuffer> &uniformBuffers,
                          const VkDescriptorSetLayout &descriptorSetLayout,
                          const VkDescriptorPool &descriptorPool) {
	std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
	VkDescriptorSetAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = descriptorPool;
	allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	allocInfo.pSetLayouts = layouts.data();

	descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
	if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
		throw std::runtime_error("failed to allocate descriptor sets!");
	}

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		VkDescriptorBufferInfo bufferInfo{};
		bufferInfo.buffer = uniformBuffers[i];
		bufferInfo.offset = 0;
		bufferInfo.range = sizeof(UniformBufferObject);

		VkDescriptorBufferInfo bufferInfoParam{};
		bufferInfoParam.buffer = uniformParamsBuffers[i];
		bufferInfoParam.offset = 0;
		bufferInfoParam.range = sizeof(UBOParams);

		VkDescriptorImageInfo imageEnvMapInfo;
		imageEnvMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageEnvMapInfo.imageView = sceneGLTF.envMap.textureImageView;
		imageEnvMapInfo.sampler = sceneGLTF.envMap.textureSampler;

		std::vector<VkDescriptorImageInfo> imageAllTexturesInfo;
		imageAllTexturesInfo.reserve(sceneGLTF.textureCache.size());
		for (const auto &t : sceneGLTF.textureCache) {
			VkDescriptorImageInfo imageTextureMapInfo;
			imageTextureMapInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			imageTextureMapInfo.imageView = t.second->textureImageView;
			imageTextureMapInfo.sampler = t.second->textureSampler;
			imageAllTexturesInfo.push_back(imageTextureMapInfo);
		}
		
		VkDescriptorBufferInfo materialsInfo{sceneGLTF.materialsCacheBuffer.buffer, 0, VK_WHOLE_SIZE};

		std::array<VkWriteDescriptorSet, 5> descriptorWrites{};

		descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[0].dstSet = descriptorSets[i];
		descriptorWrites[0].dstBinding = 0;
		descriptorWrites[0].dstArrayElement = 0;
		descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrites[0].descriptorCount = 1;
		descriptorWrites[0].pBufferInfo = &bufferInfo;

		descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[1].dstSet = descriptorSets[i];
		descriptorWrites[1].dstBinding = 1;
		descriptorWrites[1].dstArrayElement = 0;
		descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptorWrites[1].descriptorCount = 1;
		descriptorWrites[1].pBufferInfo = &bufferInfoParam;

		descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[2].dstSet = descriptorSets[i];
		descriptorWrites[2].dstBinding = 2;
		descriptorWrites[2].dstArrayElement = 0;
		descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrites[2].descriptorCount = 1;
		descriptorWrites[2].pImageInfo = &imageEnvMapInfo;

		descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[3].dstSet = descriptorSets[i];
		descriptorWrites[3].dstBinding = 3;
		descriptorWrites[3].dstArrayElement = 0;
		descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		descriptorWrites[3].descriptorCount = imageAllTexturesInfo.size();
		descriptorWrites[3].pImageInfo = imageAllTexturesInfo.data();

		descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptorWrites[4].dstSet = descriptorSets[i];
		descriptorWrites[4].dstBinding = 4;
		descriptorWrites[4].dstArrayElement = 0;
		descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		descriptorWrites[4].descriptorCount = 1;
		descriptorWrites[4].pBufferInfo = &materialsInfo;

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
	}
}

VkPipelineShaderStageCreateInfo loadShader(const std::string &fileName, VkShaderStageFlagBits stage) {
	const auto shaderCode = readFile(fileName);

	VkShaderModule shaderModule = createShaderModule(shaderCode);

	VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
	vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertShaderStageInfo.stage = stage;
	vertShaderStageInfo.module = shaderModule;
	vertShaderStageInfo.pName = "main";
	return vertShaderStageInfo;
}

void createGraphicsPipeline(const std::string &vertexPath,
                            const std::string &fragPath,
                            VkPipelineLayout &pipelineLayout,
                            VkPipeline &graphicsPipeline,
                            const VkRenderPass &renderPass,
                            const VkSampleCountFlagBits &msaaSamples,
                            const VkDescriptorSetLayout &descriptorSetLayout,
                            const float &alphaMask) {
	// vertex/Frag shader	
	VkPipelineShaderStageCreateInfo shaderStages[] = {loadShader(vertexPath, VK_SHADER_STAGE_VERTEX_BIT), loadShader(fragPath, VK_SHADER_STAGE_FRAGMENT_BIT)};

	// dynamic states
	std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

	VkPipelineDynamicStateCreateInfo dynamicState{};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
	dynamicState.pDynamicStates = dynamicStates.data();

	auto bindingDescription = Vertex::getBindingDescription();
	auto attributeDescriptions = Vertex::getAttributeDescriptions();

	// vertex
	VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
	vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInputInfo.vertexBindingDescriptionCount = 1;
	vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
	vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
	vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	VkPipelineViewportStateCreateInfo viewportState{};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer{};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;
	rasterizer.depthBiasConstantFactor = 0.0f; // Optional
	rasterizer.depthBiasClamp = 0.0f; // Optional
	rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

	VkPipelineMultisampleStateCreateInfo multisampling{};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_TRUE; // enable sample shading in the pipeline
	multisampling.rasterizationSamples = msaaSamples;
	multisampling.minSampleShading = .2f; // min fraction for sample shading; closer to one is smooth
	multisampling.pSampleMask = nullptr; // Optional
	multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
	multisampling.alphaToOneEnable = VK_FALSE; // Optional

	VkPipelineColorBlendAttachmentState colorBlendAttachment{};
	colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
	                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	colorBlendAttachment.blendEnable = alphaMask == 0 ? VK_FALSE : VK_TRUE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; // Optional
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; // Optional
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
	colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
	colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

	VkPipelineColorBlendStateCreateInfo colorBlending{};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.logicOpEnable = VK_FALSE;
	colorBlending.logicOp = VK_LOGIC_OP_COPY; // Optional
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &colorBlendAttachment;
	colorBlending.blendConstants[0] = 0.0f; // Optional
	colorBlending.blendConstants[1] = 0.0f; // Optional
	colorBlending.blendConstants[2] = 0.0f; // Optional
	colorBlending.blendConstants[3] = 0.0f; // Optional

	VkPipelineDepthStencilStateCreateInfo depthStencil{};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = VK_TRUE;
	depthStencil.depthWriteEnable = VK_TRUE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencil.depthBoundsTestEnable = VK_FALSE;
	depthStencil.minDepthBounds = 0.0f; // Optional
	depthStencil.maxDepthBounds = 1.0f; // Optional

	VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
	VkPushConstantRange pushConstantRange{};
	pushConstantRange.size = sizeof(uint32_t);
	pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstantRange.offset = 0;
	pipelineLayoutInfo.pushConstantRangeCount = 1;
	pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

	if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
		throw std::runtime_error("failed to create pipeline layout!");
	}

	VkGraphicsPipelineCreateInfo pipelineInfo{};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.stageCount = 2;
	pipelineInfo.pStages = shaderStages;
	pipelineInfo.pVertexInputState = &vertexInputInfo;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = pipelineLayout;
	pipelineInfo.renderPass = renderPass;
	pipelineInfo.subpass = 0;
	pipelineInfo.basePipelineHandle = VK_NULL_HANDLE; // Optional
	pipelineInfo.basePipelineIndex = -1; // Optional

	if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS) {
		throw std::runtime_error("failed to create graphics pipeline!");
	}

	vkDestroyShaderModule(device, shaderStages[0].module, nullptr);
	vkDestroyShaderModule(device, shaderStages[1].module, nullptr);
}

void createUniformBuffers(
	std::vector<VkBuffer> &uniformBuffers,
	std::vector<VkDeviceMemory> &uniformBuffersMemory,
	std::vector<void*> &uniformBuffersMapped) {
	VkDeviceSize bufferSize = sizeof(UniformBufferObject);

	uniformBuffers.resize(MAX_FRAMES_IN_FLIGHT);
	uniformBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
	uniformBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformBuffers[i],
		             uniformBuffersMemory[i]);

		vkMapMemory(device, uniformBuffersMemory[i], 0, bufferSize, 0, &uniformBuffersMapped[i]);
	}
}

void updateUniformBuffer(uint32_t currentFrame, const objectGLTF &obj, const glm::mat4 &parent_world) {
	UniformBufferObject ubo{};
	ubo.model = obj.world * parent_world;
	ubo.view = camWorld;
	ubo.invView = glm::inverse(camWorld);
	ubo.proj = glm::perspective(glm::radians(45.0f), swapChainExtent.width / (float)swapChainExtent.height, 0.1f, 100.0f);
	ubo.proj[1][1] *= -1;

	memcpy(obj.uniformBuffersMapped[currentFrame], &ubo, sizeof(ubo));
}

void createUniformParamsBuffers() {
	VkDeviceSize bufferSize = sizeof(UBOParams);

	uniformParamsBuffers.resize(MAX_FRAMES_IN_FLIGHT);
	uniformParamsBuffersMemory.resize(MAX_FRAMES_IN_FLIGHT);
	uniformParamsBuffersMapped.resize(MAX_FRAMES_IN_FLIGHT);

	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformParamsBuffers[i],
		             uniformParamsBuffersMemory[i]);

		vkMapMemory(device, uniformParamsBuffersMemory[i], 0, bufferSize, 0, &uniformParamsBuffersMapped[i]);
	}
}

void updateUniformParamsBuffer(uint32_t currentFrame) {
	uboParams.lightDir = {0, -1, 0, 0};
	uboParams.envRot = 0.f;
	uboParams.exposure = 1.f;
	uboParams.SHRed = {
		-0.6569198369979858, -0.05074704438447952, 0.11712795495986938, 0.5405354499816895, -0.05074704438447952, 0.6569198369979858, -0.1142701804637909, -0.45706015825271606,
		0.11712795495986938, -0.1142701804637909, -1.8876700401306152, 0.3333941698074341, 0.5405354499816895, -0.45706015825271606, 0.3333941698074341, 4.457942962646484
	};
	uboParams.SHGreen = {
		-0.5982603430747986, 0.0008933552308008075, 0.11303829401731491, 0.5236333012580872, 0.0008933552308008075, 0.5982603430747986, -0.09598314762115479, -0.3767010271549225,
		0.11303829401731491, -0.09598314762115479, -1.8332494497299194, 0.3257785141468048, 0.5236333012580872, -0.3767010271549225, 0.3257785141468048, 4.3789801597595215
	};
	uboParams.SHBlue = {
		-0.6434987187385559, -0.07664437592029572, 0.10949002951383591, 0.5047624707221985, -0.07664437592029572, 0.6434987187385559, -0.11785311996936798, -0.4755648374557495,
		0.10949002951383591, -0.11785311996936798, -1.8435758352279663, 0.3278958797454834, 0.5047624707221985, -0.4755648374557495, 0.3278958797454834, 4.394355297088623
	};

	memcpy(uniformParamsBuffersMapped[currentFrame], &uboParams, sizeof(uboParams));
}

SceneVulkanite sceneGLTF;


void loadSceneGLTF() {
	sceneGLTF.envMap.name = "envMap";
	auto cmrcFS = cmrc::gltf_rc::get_filesystem();
	auto envmapRC = cmrcFS.open(ENVMAP);

	createTextureImage(reinterpret_cast<const unsigned char*>(envmapRC.cbegin()), envmapRC.size(), sceneGLTF.envMap.textureImage, sceneGLTF.envMap.textureImageMemory, sceneGLTF.envMap.mipLevels, true);
	sceneGLTF.envMap.textureImageView = createTextureImageView(sceneGLTF.envMap.textureImage, sceneGLTF.envMap.mipLevels, VK_FORMAT_R32G32B32A32_SFLOAT);
	createTextureSampler(sceneGLTF.envMap.textureSampler, sceneGLTF.envMap.mipLevels);

	createUniformParamsBuffers();
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		updateUniformParamsBuffer(i);
	sceneGLTF.roots = loadSceneGltf(MODEL_GLTF_PATH);


	// setup raytrace
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

	//vulkanite_raytrace::createTopLevelAccelerationStructureInstance(sceneGLTF[0].children[0], glm::mat4(1));

	vulkanite_raytrace::createTopLevelAccelerationStructure();

	vulkanite_raytrace::createStorageImage(swapChainImageFormat, {swapChainExtent.width, swapChainExtent.height, 1});
	vulkanite_raytrace::createUniformBuffer();
	vulkanite_raytrace::createRayTracingPipeline();
	vulkanite_raytrace::createShaderBindingTables();
	vulkanite_raytrace::createDescriptorSets();

	//std::function<void(const objectGLTF &)> f;

	//f = [&](const objectGLTF &obj) {
	//	if (obj.vertexBuffer) {
	//		auto blas = objectToVkGeometryKHR(obj);
	//		// We could add more geometry in each BLAS, but we add only one for now
	//		allBlas.emplace_back(blas);
	//	}
	//	for (const auto &objChild : obj.children)
	//		f(obj);
	//};
	//for (const auto &o : sceneGLTF)
	//	f(o);
}

void drawModelGLTF(VkCommandBuffer commandBuffer, uint32_t currentFrame, const objectGLTF &obj, const glm::mat4 &parent_world, const bool &isRenderingAlphaPass) {
	if (sceneGLTF.primsMeshCache[obj.primMesh] &&
	    ((sceneGLTF.materialsCache[obj.mat].alphaMask == 0.f && !isRenderingAlphaPass) || (sceneGLTF.materialsCache[obj.mat].alphaMask != 0.f && isRenderingAlphaPass))) {
		updateUniformBuffer(currentFrame, obj, parent_world);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, obj.graphicsPipeline);

		VkBuffer vertexBuffers[] = {sceneGLTF.primsMeshCache[obj.primMesh]->vertexBuffer};
		VkDeviceSize offsets[] = {0};
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

		vkCmdBindIndexBuffer(commandBuffer, sceneGLTF.primsMeshCache[obj.primMesh]->indexBuffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, obj.pipelineLayout, 0, 1, &obj.descriptorSets[currentFrame], 0, nullptr);

		vkCmdPushConstants(commandBuffer, obj.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(uint32_t), &obj.mat);

		vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(sceneGLTF.primsMeshCache[obj.primMesh]->indices.size()), 1, 0, 0, 0);
	}

	for (const auto &objChild : obj.children)
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
			//vkDestroyBuffer(device, sceneGLTF.primsMeshCache[obj.primMesh]->indexBuffer, nullptr);
			//vkFreeMemory(device, sceneGLTF.primsMeshCache[obj.primMesh]->indexBufferMemory, nullptr);

			//vkDestroyBuffer(device, sceneGLTF.primsMeshCache[obj.primMesh]->vertexBuffer, nullptr);
			//vkFreeMemory(device, sceneGLTF.primsMeshCache[obj.primMesh]->vertexBufferMemory, nullptr);

			vkDestroyDescriptorPool(device, obj.descriptorPool, nullptr);

			vkDestroyDescriptorSetLayout(device, obj.descriptorSetLayout, nullptr);
			vkDestroyPipeline(device, obj.graphicsPipeline, nullptr);
			vkDestroyPipelineLayout(device, obj.pipelineLayout, nullptr);
		}
		for (const auto &objChild : obj.children)
			f(obj);
	};

	for (auto o : sceneGLTF.roots) {
		f(o);
	}
}
