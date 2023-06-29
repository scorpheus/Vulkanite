#pragma once

#include "loader.h"

namespace vulkanite_raytrace {

extern std::vector<VkAccelerationStructureInstanceKHR> instances;

void InitRaytrace();
void createBottomLevelAccelerationStructure(const objectVulkanite &obj);
void createTopLevelAccelerationStructureInstance(objectVulkanite &obj, const glm::mat4 &world, const bool &update);
void createTopLevelAccelerationStructure(bool update);
void createUniformBuffer();
void createShaderBindingTables();
void createDescriptorSets();
void createRayTracingPipeline();
void updateUniformBuffersRaytrace(uint32_t frameIndex);
void buildCommandBuffers(VkCommandBuffer commandBuffer, uint32_t imageIndex);

}
