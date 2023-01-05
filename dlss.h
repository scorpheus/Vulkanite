#pragma once
#include "VulkanBuffer.h"

namespace vulkanite_raytrace {
struct StorageImage;
}

void getExtensionsNeeded(unsigned int *OutInstanceExtCount, const char ***OutInstanceExts, unsigned int *OutDeviceExtCount, const char ***OutDeviceExts);
void initDLSS();
void RenderDLSS(VkCommandBuffer commandBuffer, uint32_t imageIndex, float sharpness, bool gbufferWasRasterized, bool resetHistory);