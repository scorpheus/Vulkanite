#pragma once

#include "loaderGltf.h"

namespace vulkanite_raytrace {
void InitRaytrace();
void createBottomLevelAccelerationStructure(const objectGLTF &obj);
void createTopLevelAccelerationStructureInstance(const objectGLTF &obj, const glm::mat4 &world);
void createTopLevelAccelerationStructure();
void createStorageImage(VkFormat format, VkExtent3D extent);
void createUniformBuffer();
void createShaderBindingTables();
void createDescriptorSets();
void createRayTracingPipeline();
void updateUniformBuffersRaytrace(uint32_t frameIndex);
void buildCommandBuffers(VkCommandBuffer commandBuffer, uint32_t imageIndex);

}
