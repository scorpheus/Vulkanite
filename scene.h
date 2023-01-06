#pragma once

#include <map>

#include "loaderGltf.h"
#include "VulkanBuffer.h"

//#define DRAW_RASTERIZE

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
	glm::mat4 modelViewProjectionMat, prevModelViewProjectionMat;
};

struct SceneVulkanite {
	textureGLTF envMap;

	std::vector<objectGLTF> roots;

	VkBuffer allVerticesBuffer;
	VkBuffer allIndicesBuffer;
	Buffer offsetPrimsBuffer;
	Buffer materialsCacheBuffer;

	std::map<uint32_t, std::shared_ptr<textureGLTF>> textureCache;
	std::map<uint32_t, std::shared_ptr<primMeshGLTF>> primsMeshCache;
	std::vector<matGLTF> materialsCache;
	
	std::vector<StorageImage> storageImagesRaytrace;
	std::vector<StorageImage> storageImagesMotionVector, storageImagesDepth;
	std::vector<StorageImage> storageImagesDLSS;
		
	std::vector<VkFramebuffer> rasterizerFramebuffers;
	VkRenderPass renderPass;

	VkDescriptorSetLayout descriptorSetLayout;
	VkPipelineLayout pipelineLayout, pipelineLayoutAlpha;
	VkPipeline graphicsPipeline, graphicsPipelineAlpha;
	
#ifdef DRAW_RASTERIZE
	std::vector<StorageImage> storageImagesRasterize;
	UBOParams uboParams;
	VkBuffer uboParamsBuffer;

	std::vector<VkBuffer> uniformParamsBuffers;
	std::vector<VkDeviceMemory> uniformParamsBuffersMemory;
	std::vector<void *> uniformParamsBuffersMapped;

#endif

};

extern SceneVulkanite sceneGLTF;

void loadSceneGLTF();
void initSceneGLTF();
void updateSceneGLTF(float deltaTime);
void drawSceneGLTF(VkCommandBuffer commandBuffer, uint32_t currentFrame);
void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);
void destroyScene();
void deleteModel();