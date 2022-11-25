#pragma once

#include <vulkan/vulkan_core.h>

void loadSceneObj(const VkRenderPass &renderPass, const VkSampleCountFlagBits &msaaSamples);
void drawModel(VkCommandBuffer commandBuffer, uint32_t currentFrame);
void deleteModel();