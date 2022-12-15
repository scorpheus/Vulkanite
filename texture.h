#pragma once

#include <string>
#include <vulkan/vulkan_core.h>

void generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);
void createTextureImage(const unsigned char *bytes, int size, VkImage &textureImage, VkDeviceMemory &textureImageMemory, uint32_t &mipLevels, bool useFloat=false);
void createTextureImage(const std::string &texturePath, VkImage &textureImage, VkDeviceMemory &textureImageMemory, uint32_t &mipLevels);
void createTextureImage(void *pixels,
                        const int &texWidth,
                        const int &texHeight,
                        const VkDeviceSize &imageSize,
                        VkImage &textureImage,
                        VkDeviceMemory &textureImageMemory,
                        uint32_t &mipLevels,
                        VkFormat format);
VkImageView createTextureImageView(const VkImage textureImage, const uint32_t mipLevels, VkFormat format);
void createTextureSampler(VkSampler &textureSampler, const uint32_t mipLevels);
