#pragma once
#include "vulkan/vulkan.h"

bool initFSR2();
void RenderFSR2(VkCommandBuffer commandBuffer, uint32_t imageIndex, float sharpness);