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
void updateUniformBuffersRaytrace();
void buildCommandBuffers(VkCommandBuffer commandBuffer, uint32_t imageIndex);

// Holds information for a storage image that the ray tracing shaders output to
struct StorageImage {
	VkDeviceMemory memory = VK_NULL_HANDLE;
	VkImage image = VK_NULL_HANDLE;
	VkImageView view = VK_NULL_HANDLE;
	VkFormat format;
};
extern std::vector<StorageImage> storageImagesRaytrace, storageImagesRaytraceDepth, storageImagesRaytraceMotionVector, storageImagesRaytraceExposure;

}
