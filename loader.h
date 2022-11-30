#pragma once

#include <vulkan/vulkan_core.h>

struct matGLTF;
struct Vertex;
void loadSceneObj();
void loadSceneGLTF();
void drawModelObj(VkCommandBuffer commandBuffer, uint32_t currentFrame);
void drawSceneGLTF(VkCommandBuffer commandBuffer, uint32_t currentFrame);
void deleteModel();


void createDescriptorSetLayout(VkDescriptorSetLayout &descriptorSetLayout);
void createGraphicsPipeline(const std::string &vertexPath,
                            const std::string &fragPath,
                            VkPipelineLayout &pipelineLayout,
                            VkPipeline &graphicsPipeline,
                            const VkRenderPass &renderPass,
                            const VkSampleCountFlagBits &msaaSamples,
                            const VkDescriptorSetLayout &descriptorSetLayout);

void createVertexBuffer(const std::vector<Vertex> &vertices, VkBuffer &vertexBuffer, VkDeviceMemory &vertexBufferMemory);
void createIndexBuffer(const std::vector<uint32_t> &indices, VkBuffer &indexBuffer, VkDeviceMemory &indexBufferMemory);

void createUniformBuffers(std::vector<VkBuffer> &uniformBuffers, std::vector<VkDeviceMemory> &uniformBuffersMemory, std::vector<void*> &uniformBuffersMapped);

void createDescriptorPool(VkDescriptorPool &descriptorPool);
void createDescriptorSets(std::vector<VkDescriptorSet> &descriptorSets,
                          const std::vector<VkBuffer> &uniformBuffers,
                          const matGLTF &mat,
                          const VkDescriptorSetLayout &descriptorSetLayout,
                          const VkDescriptorPool &descriptorPool);
