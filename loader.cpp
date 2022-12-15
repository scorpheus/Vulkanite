#include "loaderObj.h"

#include <string>
#include <vector>
#include <array>
#include <stdexcept>
#include <fstream>
#include <chrono>
#include <cstring>

#include "core_utils.h"
#include "vertex_config.h"
#include "texture.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <unordered_map>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>


#include "camera.h"
#include "loaderGltf.h"

const std::string MODEL_PATH = "models/barrel.obj"; //"models/viking_room.obj";
const std::string TEXTURE_PATH = "textures/Barrel_PropMaterial_baseColor.png"; //"textures/viking_room.png"
//const std::string MODEL_GLTF_PATH = "D:/works_2/pbr/glTF-Sample-Models-master/MetalRoughSpheres/glTF-Binary/MetalRoughSpheres.glb";//"models/scene.gltf";
const std::string MODEL_GLTF_PATH = "models/DamagedHelmet.glb";

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

textureGLTF envMap;

static std::vector<char> readFile(const std::string &filename) {
	std::ifstream file(filename, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		throw std::runtime_error("failed to open file!");
	}
	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);
	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	return buffer;
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

	createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexBuffer, vertexBufferMemory);

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

	createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
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

	std::array<VkDescriptorSetLayoutBinding, 5> samplerLayoutBinding;
	for (int i = 0; i < 5; ++i) {
		samplerLayoutBinding[i].binding = i + 3;
		samplerLayoutBinding[i].descriptorCount = 1;
		samplerLayoutBinding[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		samplerLayoutBinding[i].pImmutableSamplers = nullptr;
		samplerLayoutBinding[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	}

	std::array<VkDescriptorSetLayoutBinding, 8> bindings = {
		uboLayoutBinding, uboParamsLayoutBinding, samplerEnvMapLayoutBinding, samplerLayoutBinding[0], samplerLayoutBinding[1], samplerLayoutBinding[2], samplerLayoutBinding[3],
		samplerLayoutBinding[4]
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
	std::array<VkDescriptorPoolSize, 8> poolSizes{};
	poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[0].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	poolSizes[1].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	poolSizes[1].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	poolSizes[2].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	poolSizes[2].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	for (int i = 3; i < 3 + 5; ++i) {
		poolSizes[i].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		poolSizes[i].descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
	}

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
                          const matGLTF &mat,
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
		imageEnvMapInfo.imageView = envMap.textureImageView;
		imageEnvMapInfo.sampler = envMap.textureSampler;

		std::array<VkDescriptorImageInfo, 5> imageInfo;
		imageInfo[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo[0].imageView = mat.albedoTex->textureImageView;
		imageInfo[0].sampler = mat.albedoTex->textureSampler;
		imageInfo[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo[1].imageView = mat.metallicRoughnessTex->textureImageView;
		imageInfo[1].sampler = mat.metallicRoughnessTex->textureSampler;
		imageInfo[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo[2].imageView = mat.normalTex->textureImageView;
		imageInfo[2].sampler = mat.normalTex->textureSampler;
		imageInfo[3].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo[3].imageView = mat.aoTex->textureImageView;
		imageInfo[3].sampler = mat.aoTex->textureSampler;
		imageInfo[4].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		imageInfo[4].imageView = mat.emissiveTex->textureImageView;
		imageInfo[4].sampler = mat.emissiveTex->textureSampler;

		std::array<VkWriteDescriptorSet, 3 + imageInfo.size()> descriptorWrites{};

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

		for (int j = 3; j < 3 + imageInfo.size(); ++j) {
			descriptorWrites[j].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[j].dstSet = descriptorSets[i];
			descriptorWrites[j].dstBinding = j;
			descriptorWrites[j].dstArrayElement = 0;
			descriptorWrites[j].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			descriptorWrites[j].descriptorCount = 1;
			descriptorWrites[j].pImageInfo = &imageInfo[j - 3];
		}

		vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
	}
}

void createGraphicsPipeline(const std::string &vertexPath,
                            const std::string &fragPath,
                            VkPipelineLayout &pipelineLayout,
                            VkPipeline &graphicsPipeline,
                            const VkRenderPass &renderPass,
                            const VkSampleCountFlagBits &msaaSamples,
                            const VkDescriptorSetLayout &descriptorSetLayout) {
	// vertex/Frag shader
	auto vertShaderCode = readFile(vertexPath);
	auto fragShaderCode = readFile(fragPath);

	VkShaderModule vertShaderModule = createShaderModule(vertShaderCode);
	VkShaderModule fragShaderModule = createShaderModule(fragShaderCode);

	VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
	vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertShaderStageInfo.module = vertShaderModule;
	vertShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
	fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragShaderStageInfo.module = fragShaderModule;
	fragShaderStageInfo.pName = "main";

	VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

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
	colorBlendAttachment.blendEnable = VK_FALSE;
	colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; // Optional
	colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; // Optional
	colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
	colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
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
	pushConstantRange.size = sizeof(PushConstBlockMaterial);
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

	vkDestroyShaderModule(device, fragShaderModule, nullptr);
	vkDestroyShaderModule(device, vertShaderModule, nullptr);
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
		-0.030493533238768578, 0.027004197239875793, -0.11254461854696274, -0.00038127542939037085, 0.027004197239875793, 0.030493533238768578, 0.03437162563204765,
		0.00361999380402267, -0.11254461854696274, 0.03437162563204765, 0.14184193313121796, 0.5760947465896606, -0.00038127542939037085, 0.00361999380402267, 0.5760947465896606,
		1.7490483522415161
	};
	uboParams.SHGreen = {
		-0.026353614404797554, 0.0420733205974102, -0.11874748021364212, -0.004994439892470837, 0.0420733205974102, 0.026353614404797554, 0.03807618096470833, 0.014554289169609547,
		-0.11874748021364212, 0.03807618096470833, 0.15525034070014954, 0.5655900835990906, -0.004994439892470837, 0.014554289169609547, 0.5655900835990906, 1.8003654479980469
	};
	uboParams.SHBlue = {
		-0.030493533238768578, 0.027004197239875793, -0.11254461854696274, -0.00038127542939037085, 0.027004197239875793, 0.030493533238768578, 0.03437162563204765,
		0.00361999380402267, -0.11254461854696274, 0.03437162563204765, 0.14184193313121796, 0.5760947465896606, -0.00038127542939037085, 0.00361999380402267, 0.5760947465896606,
		1.7490483522415161
	};

	memcpy(uniformParamsBuffersMapped[currentFrame], &uboParams, sizeof(uboParams));
}

std::array<objectGLTF, 2> scene;

void loadSceneObj() {
	int i = 0;
	/*for (auto &obj : scene) {
		createDescriptorSetLayout(obj.descriptorSetLayout);
		createGraphicsPipeline("spv/shader.vert.spv", "spv/shader.frag.spv", obj.pipelineLayout, obj.graphicsPipeline, renderPass, msaaSamples, obj.descriptorSetLayout);

		createTextureImage(TEXTURE_PATH, obj.textureImage, obj.textureImageMemory, obj.mipLevels);
		obj.textureImageView = createTextureImageView(obj.textureImage, obj.mipLevels, VK_FORMAT_R8G8B8A8_UNORM);
		createTextureSampler(obj.textureSampler, obj.mipLevels);

		loadObjectObj(MODEL_PATH, obj.vertices, obj.indices);

		createVertexBuffer(obj.vertices, obj.vertexBuffer, obj.vertexBufferMemory);
		createIndexBuffer(obj.indices, obj.indexBuffer, obj.indexBufferMemory);

		createUniformBuffers(obj.uniformBuffers, obj.uniformBuffersMemory, obj.uniformBuffersMapped);

		createDescriptorPool(obj.descriptorPool);
		createDescriptorSets(obj.descriptorSets, obj.uniformBuffers, obj.textureImageView, obj.textureSampler, obj.descriptorSetLayout, obj.descriptorPool);

		obj.world = glm::translate(glm::mat4(1.f), glm::vec3(i * 2, 0, 0));
		++i;
	}*/
}

void drawModelObj(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
	for (auto &obj : scene) {
		updateUniformBuffer(currentFrame, obj, obj.world);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, obj.graphicsPipeline);

		VkBuffer vertexBuffers[] = {obj.vertexBuffer};
		VkDeviceSize offsets[] = {0};
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

		vkCmdBindIndexBuffer(commandBuffer, obj.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, obj.pipelineLayout, 0, 1, &obj.descriptorSets[currentFrame], 0, nullptr);

		vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(obj.indices.size()), 1, 0, 0, 0);
	}
}

std::vector<objectGLTF> sceneGLTF;

void loadSceneGLTF() {
	envMap.name = "envMap";
	createTextureImage("textures/autoshop_01_4k.hdr", envMap.textureImage, envMap.textureImageMemory, envMap.mipLevels);
	envMap.textureImageView = createTextureImageView(envMap.textureImage, envMap.mipLevels, VK_FORMAT_R32G32B32A32_SFLOAT);
	createTextureSampler(envMap.textureSampler, envMap.mipLevels);

	createUniformParamsBuffers();
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		updateUniformParamsBuffer(i);
	sceneGLTF = loadSceneGltf(MODEL_GLTF_PATH);
	//sceneGLTF = loadSceneGltf("models/NormalTangentTest.glb");
	//sceneGLTF = loadSceneGltf("models/cube.gltf");
}

void drawModelGLTF(VkCommandBuffer commandBuffer, uint32_t currentFrame, const objectGLTF &obj, const glm::mat4 &parent_world) {
	if (obj.vertexBuffer) {
		updateUniformBuffer(currentFrame, obj, parent_world);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, obj.graphicsPipeline);

		VkBuffer vertexBuffers[] = {obj.vertexBuffer};
		VkDeviceSize offsets[] = {0};
		vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

		vkCmdBindIndexBuffer(commandBuffer, obj.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, obj.pipelineLayout, 0, 1, &obj.descriptorSets[currentFrame], 0, nullptr);

		vkCmdPushConstants(commandBuffer, obj.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstBlockMaterial), &obj.mat.pushConstBlockMaterial);

		vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(obj.indices.size()), 1, 0, 0, 0);
	}

	for (const auto &objChild : obj.children)
		drawModelGLTF(commandBuffer, currentFrame, objChild, obj.world * parent_world);
}

void drawSceneGLTF(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
	for (auto &obj : sceneGLTF)
		drawModelGLTF(commandBuffer, currentFrame, obj, glm::mat4(1));
}

void deleteModel() {
	return;
	for (auto obj : scene) {
		for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			if (i < obj.uniformBuffers.size())
				vkDestroyBuffer(device, obj.uniformBuffers[i], nullptr);
			if (i < obj.uniformBuffersMemory.size())
				vkFreeMemory(device, obj.uniformBuffersMemory[i], nullptr);
		}

		vkDestroyBuffer(device, obj.indexBuffer, nullptr);
		vkFreeMemory(device, obj.indexBufferMemory, nullptr);

		vkDestroyBuffer(device, obj.vertexBuffer, nullptr);
		vkFreeMemory(device, obj.vertexBufferMemory, nullptr);

		vkDestroyDescriptorPool(device, obj.descriptorPool, nullptr);

		vkDestroyDescriptorSetLayout(device, obj.descriptorSetLayout, nullptr);
		vkDestroyPipeline(device, obj.graphicsPipeline, nullptr);
		vkDestroyPipelineLayout(device, obj.pipelineLayout, nullptr);
	}
}
