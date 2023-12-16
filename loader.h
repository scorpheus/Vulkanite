#pragma once

#include <string>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEFAULT_ALIGNED_GENTYPES
#include <memory>
#include <glm/glm.hpp>

#include "core_utils.h"

struct Vertex;


struct textureVulkanite {
	uint32_t id{0};
	std::string name;
	uint32_t mipLevels;
	VkImage textureImage;
	VkDeviceMemory textureImageMemory;
	VkImageView textureImageView;
	VkSampler textureSampler;

	//~textureVulkanite() {
	//	vkDestroySampler(device, textureSampler, nullptr);
	//	vkDestroyImageView(device, textureImageView, nullptr);
	//	vkDestroyImage(device, textureImage, nullptr);
	//	vkFreeMemory(device, textureImageMemory, nullptr);
	//}
};

struct matVulkanite {
	uint32_t id{0};
	uint32_t albedoTex{0};
	uint32_t metallicTex{0};
	uint32_t roughnessTex{0};
	uint32_t aoTex{0};
	uint32_t normalTex{0};
	uint32_t emissiveTex{0};
	uint32_t transmissionTex{0};
	
	int colorTextureSet{0};
	int metallicTextureSet{0};
	int roughnessTextureSet{0};
	int normalTextureSet{-1};
	int occlusionTextureSet{0};
	int emissiveTextureSet{0};
	int transmissionTextureSet{0};

	int doubleSided{false};

	float occlusionFactor{1};
	float metallicFactor{1};
	float roughnessFactor{1};
	float alphaMask{0};
	float alphaMaskCutoff{0};
	float transmissionFactor{0};
	float ior{1.5f};

	glm::vec4 baseColorFactor{1, 1, 1, 1};
	glm::vec3 emissiveFactor{0, 0, 0};
};

struct primMeshVulkanite {
	uint32_t id{0};
	// Vulkan
	std::vector<Vertex> vertices;
	VkBuffer vertexBuffer;
	VkDeviceMemory vertexBufferMemory;
	std::vector<uint32_t> indices;
	VkBuffer indexBuffer;
	VkDeviceMemory indexBufferMemory;
};

struct objectVulkanite {
	std::vector<objectVulkanite> children;
	uint32_t id{0}, idInstanceRaytrace{0};
	std::string name;
	glm::mat4 world{1};
	glm::mat4 PrevModelViewProjectionMat{1};

	uint32_t mat{0}, matCacheID{0};

	// Vulkan
	uint32_t primMesh{std::numeric_limits<uint32_t>::max()};

	VkDescriptorPool descriptorPool, descriptorPoolMotionVector;
	std::vector<VkDescriptorSet> descriptorSets, descriptorSetsMotionVector;

	std::vector<VkBuffer> uniformBuffers, uniformBuffersMotionVector;
	std::vector<VkDeviceMemory> uniformBuffersMemory, uniformBuffersMemoryMotionVector;
	std::vector<void*> uniformBuffersMapped, uniformBuffersMappedMotionVector;
};

std::vector<objectVulkanite> loadSceneGLTF(const std::string &scenePath);
#ifdef ACTIVATE_USD
std::vector<objectVulkanite> loadSceneUSD(const std::string &scenePath);
#endif
