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
	std::vector<StorageImage> storageImagesDLSS;

	std::vector<VkFramebuffer> rasterizerFramebuffers, imguiFramebuffers, MotionVectorFramebuffers;
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

void loadScene();
void initScene();
void updateScene( float deltaTime );
void drawScene( VkCommandBuffer commandBuffer, uint32_t currentFrame );
void recordCommandBuffer( VkCommandBuffer commandBuffer, uint32_t imageIndex, ImDrawData* draw_data );
void destroyScene();
void deleteModel();