#pragma once

#include <map>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

#include "loaderGltf.h"
#include "VulkanBuffer.h"

struct matGLTF;
struct Vertex;
void loadSceneGLTF();
void drawSceneGLTF(VkCommandBuffer commandBuffer, uint32_t currentFrame);
void deleteModel();

VkPipelineShaderStageCreateInfo loadShader(const std::string &fileName, VkShaderStageFlagBits stage);

void createDescriptorSetLayout(VkDescriptorSetLayout &descriptorSetLayout);
void createGraphicsPipeline(const std::string &vertexPath,
                            const std::string &fragPath,
                            VkPipelineLayout &pipelineLayout,
                            VkPipeline &graphicsPipeline,
                            const VkRenderPass &renderPass,
                            const VkSampleCountFlagBits &msaaSamples,
                            const VkDescriptorSetLayout &descriptorSetLayout,
                            const float &alphaMask);

void createVertexBuffer(const std::vector<Vertex> &vertices, VkBuffer &vertexBuffer, VkDeviceMemory &vertexBufferMemory);
void createIndexBuffer(const std::vector<uint32_t> &indices, VkBuffer &indexBuffer, VkDeviceMemory &indexBufferMemory);

void createUniformBuffers(std::vector<VkBuffer> &uniformBuffers, std::vector<VkDeviceMemory> &uniformBuffersMemory, std::vector<void*> &uniformBuffersMapped);

void createDescriptorPool(VkDescriptorPool &descriptorPool);
void createDescriptorSets(std::vector<VkDescriptorSet> &descriptorSets,
                          const std::vector<VkBuffer> &uniformBuffers,
                          const VkDescriptorSetLayout &descriptorSetLayout,
                          const VkDescriptorPool &descriptorPool);

struct SceneVulkanite {
	textureGLTF envMap;

	std::vector<objectGLTF> roots;

	VkBuffer allVerticesBuffer;
	VkBuffer allIndicesBuffer;
	vks::Buffer offsetPrimsBuffer;
	vks::Buffer materialsCacheBuffer;

	std::map<uint32_t, std::shared_ptr<textureGLTF>> textureCache;
	std::map<uint32_t, std::shared_ptr<primMeshGLTF>> primsMeshCache;
	std::vector<matGLTF> materialsCache;
};
extern SceneVulkanite sceneGLTF;

