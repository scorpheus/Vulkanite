#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include <glm/glm.hpp>

#include "core_utils.h"

struct Vertex;


struct textureGLTF {
	std::string name;
	uint32_t mipLevels;
	VkImage textureImage;
	VkDeviceMemory textureImageMemory;
	VkImageView textureImageView;
	VkSampler textureSampler;

	//~textureGLTF() {
	//	vkDestroySampler(device, textureSampler, nullptr);
	//	vkDestroyImageView(device, textureImageView, nullptr);
	//	vkDestroyImage(device, textureImage, nullptr);
	//	vkFreeMemory(device, textureImageMemory, nullptr);
	//}
};

struct PushConstBlockMaterial {
	float metallicFactor;
	float roughnessFactor;
	float alphaMask;
	float alphaMaskCutoff;
	int colorTextureSet;
	int metallicRoughnessTextureSet;
	int normalTextureSet;
	int occlusionTextureSet;
	int emissiveTextureSet;
	glm::vec4 baseColorFactor;
	glm::vec3 emissiveFactor;
};

struct matGLTF {
	textureGLTF albedoTex;
	textureGLTF metallicRoughnessTex;
	textureGLTF aoTex;
	textureGLTF normalTex;
	textureGLTF emissiveTex;

	PushConstBlockMaterial pushConstBlockMaterial;
};

struct objectGLTF {
	std::vector<objectGLTF> children;
	std::string name;
	glm::mat4 world {1};

	matGLTF mat;

	// Vulkan
	std::vector<Vertex> vertices;
	VkBuffer vertexBuffer;
	VkDeviceMemory vertexBufferMemory;
	std::vector<uint32_t> indices;
	VkBuffer indexBuffer;
	VkDeviceMemory indexBufferMemory;

	VkDescriptorPool descriptorPool;
	std::vector<VkDescriptorSet> descriptorSets;
	VkDescriptorSetLayout descriptorSetLayout;

	VkPipelineLayout pipelineLayout;
	VkPipeline graphicsPipeline;

	std::vector<VkBuffer> uniformBuffers;
	std::vector<VkDeviceMemory> uniformBuffersMemory;
	std::vector<void *> uniformBuffersMapped;
	
};

std::vector<objectGLTF> loadSceneGltf(const std::string &scenePath);
