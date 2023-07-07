#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan_core.h>

// Holds information for a storage image that the shaders output to
struct StorageImage {
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImage image = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkFormat format;
};

bool generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);
bool createTextureImage(const unsigned char *bytes, int size, VkImage &textureImage, VkDeviceMemory &textureImageMemory, uint32_t &mipLevels, bool useFloat=false);
bool createTextureImage(const std::string &texturePath, VkImage &textureImage, VkDeviceMemory &textureImageMemory, uint32_t &mipLevels);
void createTextureImage(void *pixels,
                        const int &texWidth,
                        const int &texHeight,
                        const VkDeviceSize &imageSize,
                        VkImage &textureImage,
                        VkDeviceMemory &textureImageMemory,
                        uint32_t &mipLevels,
                        VkFormat format);
VkImageView createTextureImageView(const VkImage textureImage, const uint32_t mipLevels, VkFormat format);
bool createTextureSampler(VkSampler &textureSampler, const uint32_t mipLevels);

void createStorageImage(std::vector<StorageImage> &storageImages, VkFormat format, VkImageAspectFlags aspect, VkExtent3D extent);
void deleteStorageImage(std::vector<StorageImage> &storageImages);