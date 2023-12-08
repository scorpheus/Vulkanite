#pragma once

#include <map>

#include "loader.h"
#include "VulkanBuffer.h"

struct ImDrawData;

struct StorageImage;

struct UniformBufferObject {
	glm::mat4 model;
	glm::mat4 view;
	glm::mat4 invView;
	glm::mat4 proj;
};

struct UBOParams {
	glm::vec4 lightDir;
	float envRot;
	float exposure;
	glm::mat4 SHRed;
	glm::mat4 SHGreen;
	glm::mat4 SHBlue;
};

struct UniformBufferObjectMotionVector {
	glm::mat4 modelViewProjectionMat, prevModelViewProjectionMat, jitterMat;
};

struct SceneVulkanite {
	float cameraNear, cameraFar, cameraFov;
	double deltaTime;

	textureVulkanite envMap;
		
	bool DRAW_RASTERIZE;

	std::vector<objectVulkanite> roots;

	VkBuffer allVerticesBuffer;
	VkBuffer allIndicesBuffer;
	Buffer offsetPrimsBuffer;
	Buffer materialsCacheBuffer;

	std::map<uint32_t, std::shared_ptr<textureVulkanite>> textureCache;
	std::vector<std::shared_ptr<textureVulkanite>> textureCacheSequential;
	std::map<uint32_t, std::shared_ptr<primMeshVulkanite>> primsMeshCache;
	std::map<uint32_t, std::shared_ptr<matVulkanite>> materialsCache;

	std::vector<StorageImage> storageImagesRaytrace, storageImagesMotionVector;
	std::vector<StorageImage> storageImagesDepth, storageImagesDepthMotionVector;
	std::vector<StorageImage> storageImagesDLSS, storageImagesFSR2;

	std::vector<VkFramebuffer> rasterizerFramebuffers, raytraceFramebuffers, fsr2Framebuffers, MotionVectorFramebuffers;
	VkRenderPass renderPass, renderPassMotionVector;

	VkDescriptorSetLayout descriptorSetLayout;
	VkPipelineLayout pipelineLayout, pipelineLayoutAlpha;
	VkPipeline graphicsPipeline, graphicsPipelineAlpha;

	std::vector<StorageImage> storageImagesRasterize;
	UBOParams uboParams;
	VkBuffer uboParamsBuffer;

	std::vector<VkBuffer> uniformParamsBuffers;
	std::vector<VkDeviceMemory> uniformParamsBuffersMemory;
	std::vector<void*> uniformParamsBuffersMapped;
};

extern SceneVulkanite scene;
extern bool USE_DLSS;
extern bool USE_FSR2;

void loadScene();
void initScene();
void createSceneFramebuffer();
void updateScene( float deltaTime );
void drawScene( VkCommandBuffer commandBuffer, uint32_t currentFrame );
#ifdef ACTIVATE_IMGUI
void recordCommandBuffer( VkCommandBuffer commandBuffer, uint32_t imageIndex, ImDrawData* draw_data );
#else
void recordCommandBuffer( VkCommandBuffer commandBuffer, uint32_t imageIndex );
#endif
void destroyScene();
void deleteModel();