#pragma once

#include <stdexcept>
#include <vulkan/vulkan.h>

std::string errorString(VkResult errorCode);

#define VK_CHECK_RESULT_S(f, s)			\
{										\
	VkResult res = (f);					\
	if (res != VK_SUCCESS)				\
		throw std::runtime_error(s);	\
}

#define VK_CHECK_RESULT(f)				\
{										\
	VkResult res = (f);					\
	if (res != VK_SUCCESS)				\
		throw std::runtime_error( fmt::format("Fatal : VkResult is \"{}\" in {} at line {}\n", errorString(res), __FILE__, __LINE__));	\
}


namespace vks {
struct Buffer;
}

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

extern VkFormat swapChainImageFormat;

extern VkRenderPass renderPass;
extern VkSampleCountFlagBits msaaSamples;

extern VkExtent2D swapChainExtent;

extern VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures;
extern VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingPipelineProperties;


uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

void createBuffer(VkDeviceSize size,
                  VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags properties,
                  VkBuffer &buffer,
                  VkDeviceMemory &bufferMemory);
VkResult createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, vks::Buffer *buffer, VkDeviceSize size, void *data = nullptr);
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
