#pragma once

#include "loaderGltf.h"

namespace vulkanite_raytrace {
void InitRaytrace();
void createBottomLevelAccelerationStructure(std::vector<objectGLTF> &sceneGLTF);
void createTopLevelAccelerationStructure();
void createStorageImage(VkFormat format, VkExtent3D extent);
void createUniformBuffer();
void createShaderBindingTables();
void createDescriptorSets(std::vector<objectGLTF> &sceneGLTF);
void createRayTracingPipeline();
void buildCommandBuffers(VkCommandBuffer commandBuffer);
}
