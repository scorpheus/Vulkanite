#include "texture.h"
#include "core_utils.h"

#include <string>
#include <stdexcept>
#include <cmath>
#include <fmt/core.h>
#include <filesystem>
namespace fs = std::filesystem;

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <spdlog/spdlog.h>

bool isDepth( const VkFormat& format ) {
	return ( format == VK_FORMAT_D16_UNORM || format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT );
}

bool generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) {
	// Check if image format supports linear blitting
	VkFormatProperties formatProperties;
	vkGetPhysicalDeviceFormatProperties(physicalDevice, imageFormat, &formatProperties);
	if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
		//throw std::runtime_error("texture image format does not support linear blitting!");
		spdlog::error("texture image format does not support linear blitting!");
		return false;
	}

	VkCommandBuffer commandBuffer = beginSingleTimeCommands();

	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.image = image;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;
	barrier.subresourceRange.levelCount = 1;

	int32_t mipWidth = texWidth;
	int32_t mipHeight = texHeight;

	for (uint32_t i = 1; i < mipLevels; i++) {
		barrier.subresourceRange.baseMipLevel = i - 1;
		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer, //
		                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, //
		                     0, nullptr, //
		                     0, nullptr, //
		                     1, &barrier);

		VkImageBlit blit{};
		blit.srcOffsets[0] = {0, 0, 0};
		blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
		blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.srcSubresource.mipLevel = i - 1;
		blit.srcSubresource.baseArrayLayer = 0;
		blit.srcSubresource.layerCount = 1;
		blit.dstOffsets[0] = {0, 0, 0};
		blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
		blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		blit.dstSubresource.mipLevel = i;
		blit.dstSubresource.baseArrayLayer = 0;
		blit.dstSubresource.layerCount = 1;

		vkCmdBlitImage(commandBuffer, //
		               image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, //
		               image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, //
		               1, &blit, //
		               VK_FILTER_LINEAR);

		barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
		barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(commandBuffer, //
		                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, //
		                     0, nullptr, //
		                     0, nullptr, //
		                     1, &barrier);

		if (mipWidth > 1)
			mipWidth /= 2;
		if (mipHeight > 1)
			mipHeight /= 2;
	}
	barrier.subresourceRange.baseMipLevel = mipLevels - 1;
	barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

	vkCmdPipelineBarrier(commandBuffer, //
	                     VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, //
	                     0, nullptr, //
	                     0, nullptr, //
	                     1, &barrier);

	endSingleTimeCommands(commandBuffer);
}

bool createTextureImage(const unsigned char *bytes, int size, VkImage &textureImage, VkDeviceMemory &textureImageMemory, uint32_t &mipLevels, bool useFloat) {
	int texWidth, texHeight, texChannels;
	void *pixels;
	if (useFloat)
		pixels = stbi_loadf_from_memory(bytes, size, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	else
		pixels = stbi_load_from_memory(bytes, size, &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

	if (!pixels) {
		//throw std::runtime_error("failed to load texture image!");
		spdlog::error("failed to load texture image!");
		return false;
	}
	if (useFloat)
		createTextureImage(pixels, texWidth, texHeight, texWidth * texHeight * 4 * sizeof(float), textureImage, textureImageMemory, mipLevels, VK_FORMAT_R32G32B32A32_SFLOAT);
	else
		createTextureImage(pixels, texWidth, texHeight, texWidth * texHeight * 4 * sizeof(unsigned char), textureImage, textureImageMemory, mipLevels, VK_FORMAT_R8G8B8A8_UNORM);

	stbi_image_free(pixels);
	return true;
}

bool createTextureImage(const std::string &texturePath, VkImage &textureImage, VkDeviceMemory &textureImageMemory, uint32_t &mipLevels) {
	int texWidth, texHeight, texChannels;
	void *pixels;
	if (fs::path(texturePath).extension() == ".hdr")
		pixels = stbi_loadf(texturePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
	else
		pixels = stbi_load(texturePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);

	if (!pixels) {
	//	throw std::runtime_error("failed to load texture image!");
		spdlog::error("failed to load texture image!");
		return false;
	}
	if (fs::path(texturePath).extension() == ".hdr")
		createTextureImage(pixels, texWidth, texHeight, texWidth * texHeight * 4 * sizeof(float), textureImage, textureImageMemory, mipLevels, VK_FORMAT_R32G32B32A32_SFLOAT);
	else
		createTextureImage(pixels, texWidth, texHeight, texWidth * texHeight * 4 * sizeof(unsigned char), textureImage, textureImageMemory, mipLevels, VK_FORMAT_R8G8B8A8_UNORM);

	stbi_image_free(pixels);
}

void createTextureImage(void *pixels,
                        const int &texWidth,
                        const int &texHeight,
                        const VkDeviceSize &imageSize,
                        VkImage &textureImage,
                        VkDeviceMemory &textureImageMemory,
                        uint32_t &mipLevels,
                        VkFormat format) {
	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;
	createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
	             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer,
	             stagingBufferMemory);

	void *data;
	vkMapMemory(device, stagingBufferMemory, 0, imageSize, 0, &data);
	memcpy(data, pixels, static_cast<size_t>(imageSize));
	vkUnmapMemory(device, stagingBufferMemory);

	mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

	createImage(texWidth, texHeight, mipLevels, VK_SAMPLE_COUNT_1_BIT, format, VK_IMAGE_TILING_OPTIMAL,
	            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
	            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, textureImage, textureImageMemory);

	transitionImageLayout(textureImage, format, VK_IMAGE_LAYOUT_UNDEFINED,
	                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels);

	copyBufferToImage(stagingBuffer, textureImage, static_cast<uint32_t>(texWidth),
	                  static_cast<uint32_t>(texHeight));

	generateMipmaps(textureImage, format, texWidth, texHeight, mipLevels);

	vkDestroyBuffer(device, stagingBuffer, nullptr);
	vkFreeMemory(device, stagingBufferMemory, nullptr);
}

VkImageView createTextureImageView(const VkImage textureImage, const uint32_t mipLevels, VkFormat format) {
	return createImageView(textureImage, format, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels);
}

bool createTextureSampler(VkSampler &textureSampler, const uint32_t mipLevels) {
	VkPhysicalDeviceProperties properties{};
	vkGetPhysicalDeviceProperties(physicalDevice, &properties);

	VkSamplerCreateInfo samplerInfo{};
	samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	samplerInfo.magFilter = VK_FILTER_LINEAR;
	samplerInfo.minFilter = VK_FILTER_LINEAR;

	samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

	samplerInfo.anisotropyEnable = VK_TRUE;
	samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;

	samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
	samplerInfo.unnormalizedCoordinates = VK_FALSE;

	samplerInfo.compareEnable = VK_TRUE;
	samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

	samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
	samplerInfo.minLod = 0.0f; // Optional
	samplerInfo.maxLod = static_cast<float>(mipLevels);
	samplerInfo.mipLodBias = 0.0f; // Optional

	if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
	//	throw std::runtime_error("failed to create texture sampler!");
		spdlog::error("failed to load texture image!");
		return false;
	}
	return true;
}

void createStorageImage(std::vector<StorageImage> &storageImages, VkFormat format, VkImageAspectFlags aspect, VkExtent3D extent) {
	storageImages.resize(MAX_FRAMES_IN_FLIGHT);
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		// Release resources if image is to be recreated
		if (storageImages[i].image != VK_NULL_HANDLE) {
			vkDestroyImageView(device, storageImages[i].view, nullptr);
			vkDestroyImage(device, storageImages[i].image, nullptr);
			vkFreeMemory(device, storageImages[i].memory, nullptr);
			storageImages[i] = {};
		}

		VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = storageImages[i].format = format;
		image.extent = extent;
		image.mipLevels = storageImages[i].mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = aspect == VK_IMAGE_ASPECT_DEPTH_BIT
			              ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
			              : (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
			                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
		image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &storageImages[i].image));

		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, storageImages[i].image, &memReqs);
		VkMemoryAllocateInfo memoryAllocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		memoryAllocateInfo.allocationSize = memReqs.size;
		memoryAllocateInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memoryAllocateInfo, nullptr, &storageImages[i].memory));
		VK_CHECK_RESULT(vkBindImageMemory(device, storageImages[i].image, storageImages[i].memory, 0));

		VkImageViewCreateInfo colorImageView{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorImageView.format = format;
		colorImageView.subresourceRange = {};
		colorImageView.subresourceRange.aspectMask = aspect;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;
		colorImageView.image = storageImages[i].image;
		VK_CHECK_RESULT(vkCreateImageView(device, &colorImageView, nullptr, &storageImages[i].view));

		transitionImageLayout(storageImages[i].image, format, VK_IMAGE_LAYOUT_UNDEFINED,
		                      aspect == VK_IMAGE_ASPECT_DEPTH_BIT ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL, 1);
	}
}

void deleteStorageImage(std::vector<StorageImage> &storageImages) {
	for (auto &storageImage : storageImages) {
		vkDestroyImageView(device, storageImage.view, nullptr);
		vkDestroyImage(device, storageImage.image, nullptr);
		vkFreeMemory(device, storageImage.memory, nullptr);
	}
}
