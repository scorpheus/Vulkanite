#pragma once

#include <vulkan/vulkan.h>


const uint32_t WIDTH = 800;
const uint32_t HEIGHT = 600;

const int MAX_FRAMES_IN_FLIGHT = 2;

extern VkInstance instance;
extern VkDebugUtilsMessengerEXT debugMessenger;
extern VkPhysicalDevice physicalDevice;
extern VkDevice device;
extern VkQueue graphicsQueue;
extern VkQueue presentQueue;
extern VkCommandPool commandPool;

extern VkRenderPass renderPass;
extern VkSampleCountFlagBits msaaSamples;

extern VkExtent2D swapChainExtent;


uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

void createBuffer(VkDeviceSize size,
                  VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties,
                  VkBuffer &buffer,
                  VkDeviceMemory &bufferMemory);
void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);

VkCommandBuffer beginSingleTimeCommands();
void endSingleTimeCommands(VkCommandBuffer commandBuffer);

VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, uint32_t mipLevels);

void createImage(uint32_t width,
                 uint32_t height,
                 uint32_t mipLevels,
                 VkSampleCountFlagBits numSamples,
                 VkFormat format,
                 VkImageTiling tiling,
                 VkImageUsageFlags usage,
                 VkMemoryPropertyFlags properties,
                 VkImage &image,
                 VkDeviceMemory &imageMemory);
void transitionImageLayout(VkImage image,
                           VkFormat format,
                           VkImageLayout oldLayout,
                           VkImageLayout newLayout,
                           uint32_t mipLevels);
void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
