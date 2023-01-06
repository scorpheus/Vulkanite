#pragma once

#include <map>
#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

#include "loaderGltf.h"
#include "VulkanBuffer.h"

struct UBOParams;
struct StorageImage;
struct matGLTF;
struct Vertex;

VkPipelineShaderStageCreateInfo loadShader(const std::string &fileName, VkShaderStageFlagBits stage);

void createDescriptorSetLayout(VkDescriptorSetLayout &descriptorSetLayout);
void createDescriptorSetLayoutMotionVector(VkDescriptorSetLayout &descriptorSetLayout);
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

void createUniformBuffers(std::vector<VkBuffer> &uniformBuffers,
                          std::vector<VkDeviceMemory> &uniformBuffersMemory,
                          std::vector<void*> &uniformBuffersMapped,
                          VkDeviceSize bufferSize);

void createDescriptorPool(VkDescriptorPool &descriptorPool);
void createDescriptorSets(std::vector<VkDescriptorSet> &descriptorSets,
                          const std::vector<VkBuffer> &uniformBuffers,
                          const VkDeviceSize &uniformBufferSize,
                          const VkDescriptorSetLayout &descriptorSetLayout,
                          const VkDescriptorPool &descriptorPool,
                          const std::vector<VkBuffer> &uniformParamsBuffers,
                          const VkDeviceSize &uniformParamsBufferSize);
void createDescriptorPoolMotionVector(VkDescriptorPool &descriptorPool);
void createDescriptorSetsMotionVector(std::vector<VkDescriptorSet> &descriptorSets,
                                      const std::vector<VkBuffer> &uniformBuffers,
                                      const VkDeviceSize &uniformBufferSize,
                                      const VkDescriptorSetLayout &descriptorSetLayout,
                                      const VkDescriptorPool &descriptorPool);

void createUniformParamsBuffers(VkDeviceSize bufferSize, std::vector<VkBuffer> &uniformParamsBuffers, std::vector<VkDeviceMemory> &uniformParamsBuffersMemory, std::vector<void *> &uniformParamsBuffersMapped);

void createFramebuffers(const VkRenderPass &renderPass, std::vector<VkFramebuffer> &framebuffers, const std::vector<StorageImage> &colorImage, const std::vector<StorageImage> &depthImage);
VkFormat findDepthFormat();
void createRenderPass(VkRenderPass &renderPass, const VkFormat &colorImageFormat, const VkFormat &depthImageFormat, VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT);

void updateUniformBuffer(uint32_t currentFrame, const objectGLTF &obj, const glm::mat4 &parent_world);
void updateUniformBufferMotionVector(uint32_t currentFrame, objectGLTF &obj, const glm::mat4 &parent_world);
void updateUniformParamsBuffer(UBOParams &uboParams, std::vector<void *> &uniformParamsBuffersMapped, uint32_t currentFrame);
