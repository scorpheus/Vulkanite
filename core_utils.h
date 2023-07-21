#pragma once

#include <stdexcept>
#include <vector>
#include <vulkan/vulkan.h>

struct Buffer;
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

const uint32_t WIDTH = 1024;
const uint32_t HEIGHT = 768;
extern float UPSCALE_SCALE;

extern uint32_t MAX_FRAMES_IN_FLIGHT;

extern VkInstance instance;
extern VkDebugUtilsMessengerEXT debugMessenger;
extern VkPhysicalDevice physicalDevice;
extern VkDevice device;
extern VkQueue graphicsQueue;
extern VkQueue presentQueue;
extern VkCommandPool commandPool;

extern VkImage colorImage;
extern VkDeviceMemory colorImageMemory;
extern VkImageView colorImageView;

extern std::vector<VkImage> swapChainImages;
extern VkFormat swapChainImageFormat;

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
VkResult createBuffer(VkBufferUsageFlags usageFlags, VkMemoryPropertyFlags memoryPropertyFlags, Buffer *buffer, VkDeviceSize size, void *data = nullptr);
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
void setImageLayout(VkCommandBuffer cmdbuffer,
                    VkImage image,
                    VkImageLayout oldImageLayout,
                    VkImageLayout newImageLayout,
                    VkImageSubresourceRange subresourceRange,
                    VkPipelineStageFlags srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                    VkPipelineStageFlags dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
void copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width, uint32_t height);
