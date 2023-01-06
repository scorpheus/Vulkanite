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

struct matGLTF {
	bool doubleSided{false};
	uint32_t albedoTex{0};
	uint32_t metallicRoughnessTex{0};
	uint32_t aoTex{0};
	uint32_t normalTex{0};
	uint32_t emissiveTex{0};
	uint32_t transmissionTex{0};

	float metallicFactor{1};
	float roughnessFactor{1};
	float alphaMask{0};
	float alphaMaskCutoff{0};
	float transmissionFactor{0};
	float ior{1.5f};

	int colorTextureSet{0};
	int metallicRoughnessTextureSet{0};
	int normalTextureSet{-1};
	int occlusionTextureSet{0};
	int emissiveTextureSet{0};
	int transmissionTextureSet{0};
	glm::vec4 baseColorFactor{1, 1, 1, 1};
	glm::vec3 emissiveFactor{0, 0, 0};
};

struct primMeshGLTF {
	uint32_t id{0};
	// Vulkan
	std::vector<Vertex> vertices;
	VkBuffer vertexBuffer;
	VkDeviceMemory vertexBufferMemory;
	std::vector<uint32_t> indices;
	VkBuffer indexBuffer;
	VkDeviceMemory indexBufferMemory;
};

struct objectGLTF {
	std::vector<objectGLTF> children;
	uint32_t id{0};
	std::string name;
	glm::mat4 world{1};
	glm::mat4 PrevModelViewProjectionMat{1};

	uint32_t mat;

	// Vulkan
	uint32_t primMesh;

	VkDescriptorPool descriptorPool;
	std::vector<VkDescriptorSet> descriptorSets;

	std::vector<VkBuffer> uniformBuffers;
	std::vector<VkDeviceMemory> uniformBuffersMemory;
	std::vector<void*> uniformBuffersMapped;
};

std::vector<objectGLTF> loadSceneGltf(const std::string &scenePath);
