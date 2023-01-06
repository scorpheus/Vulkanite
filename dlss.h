#pragma once
#include "VulkanBuffer.h"

void getExtensionsNeeded(unsigned int *OutInstanceExtCount, const char ***OutInstanceExts, unsigned int *OutDeviceExtCount, const char ***OutDeviceExts);
void initDLSS();
void RenderDLSS(VkCommandBuffer commandBuffer, uint32_t imageIndex, float sharpness);