#include "dlss.h"

#include "core_utils.h"
#include <nvsdk_ngx_helpers_vk.h>

#include <spdlog/spdlog.h>

#include "camera.h"
#include "scene.h"
#include "texture.h"

NVSDK_NGX_Parameter *paramsDLSS = nullptr;
NVSDK_NGX_Handle *dlssFeature = nullptr;

void getExtensionsNeeded(unsigned int *OutInstanceExtCount, const char ***OutInstanceExts, unsigned int *OutDeviceExtCount, const char ***OutDeviceExts) {
	auto result = NVSDK_NGX_VULKAN_RequiredExtensions(OutInstanceExtCount, OutInstanceExts, OutDeviceExtCount, OutDeviceExts);
}

void createStorageImage(VkFormat format, VkExtent3D extent) {
	sceneGLTF.storageImagesDLSS.resize(MAX_FRAMES_IN_FLIGHT);
	for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
		// Release ressources if image is to be recreated
		if (sceneGLTF.storageImagesDLSS[i].image != VK_NULL_HANDLE) {
			vkDestroyImageView(device, sceneGLTF.storageImagesDLSS[i].view, nullptr);
			vkDestroyImage(device, sceneGLTF.storageImagesDLSS[i].image, nullptr);
			vkFreeMemory(device, sceneGLTF.storageImagesDLSS[i].memory, nullptr);
			sceneGLTF.storageImagesDLSS[i] = {};
		}

		VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = format;
		image.extent = extent;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		image.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &sceneGLTF.storageImagesDLSS[i].image));

		VkMemoryRequirements memReqs;
		vkGetImageMemoryRequirements(device, sceneGLTF.storageImagesDLSS[i].image, &memReqs);
		VkMemoryAllocateInfo memoryAllocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		memoryAllocateInfo.allocationSize = memReqs.size;
		memoryAllocateInfo.memoryTypeIndex = findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memoryAllocateInfo, nullptr, &sceneGLTF.storageImagesDLSS[i].memory));
		VK_CHECK_RESULT(vkBindImageMemory(device, sceneGLTF.storageImagesDLSS[i].image, sceneGLTF.storageImagesDLSS[i].memory, 0));

		VkImageViewCreateInfo colorImageView{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorImageView.format = format;
		colorImageView.subresourceRange = {};
		colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;
		colorImageView.image = sceneGLTF.storageImagesDLSS[i].image;
		VK_CHECK_RESULT(vkCreateImageView(device, &colorImageView, nullptr, &sceneGLTF.storageImagesDLSS[i].view));

		transitionImageLayout(sceneGLTF.storageImagesDLSS[i].image, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, 1);
	}
}
static void NVSDK_CONV NgxLogCallback(const char *message, NVSDK_NGX_Logging_Level loggingLevel, NVSDK_NGX_Feature sourceComponent) { std::string s(message); s.pop_back(); spdlog::info("NGX: {}", s); }

void initDLSS() {
	createStorageImage(swapChainImageFormat, {swapChainExtent.width, swapChainExtent.height, 1});


	NVSDK_NGX_FeatureCommonInfo featureCommonInfo = {};
	featureCommonInfo.LoggingInfo.LoggingCallback = NgxLogCallback;
	featureCommonInfo.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_VERBOSE;
	featureCommonInfo.LoggingInfo.DisableOtherLoggingSinks = true;

	auto result = NVSDK_NGX_VULKAN_Init(1, L".", instance, physicalDevice, device, &featureCommonInfo);
	
	if (NVSDK_NGX_FAILED(result)) {
		if (result == NVSDK_NGX_Result_FAIL_FeatureNotSupported || result == NVSDK_NGX_Result_FAIL_PlatformError)
			spdlog::info(L"NVIDIA NGX not available on this hardware/platform., code = {}, info: {}", result, GetNGXResultAsString(result));
		else
			spdlog::error(L"Failed to initialize NGX, error code = {}, info: {}", result, GetNGXResultAsString(result));
		return;
	}
		

	result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&paramsDLSS);
	
	//
    unsigned int CreationNodeMask = 1;
	unsigned int VisibilityNodeMask = 1;

	int MotionVectorResolutionLow = 1; // we let the Snippet do the upsampling of the motion vector
	// Next create features
	int DlssCreateFeatureFlags = NVSDK_NGX_DLSS_Feature_Flags_None;
	DlssCreateFeatureFlags |= MotionVectorResolutionLow ? NVSDK_NGX_DLSS_Feature_Flags_MVLowRes : 0;
	DlssCreateFeatureFlags |= /*isContentHDR ? NVSDK_NGX_DLSS_Feature_Flags_IsHDR : */0;
	DlssCreateFeatureFlags |= /*depthInverted ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted :*/ 0;
	DlssCreateFeatureFlags |= 0 /*enableSharpening ? NVSDK_NGX_DLSS_Feature_Flags_DoSharpening :  0*/;
	DlssCreateFeatureFlags |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure /* enableAutoExposure ? NVSDK_NGX_DLSS_Feature_Flags_AutoExposure : 0*/;

	NVSDK_NGX_DLSS_Create_Params DlssCreateParams;

	memset(&DlssCreateParams, 0, sizeof(DlssCreateParams));

	DlssCreateParams.Feature.InWidth = WIDTH*DLSS_SCALE;
	DlssCreateParams.Feature.InHeight = HEIGHT*DLSS_SCALE;
	DlssCreateParams.Feature.InTargetWidth = WIDTH;
	DlssCreateParams.Feature.InTargetHeight = HEIGHT;
	DlssCreateParams.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_MaxQuality;
	DlssCreateParams.InFeatureCreateFlags = DlssCreateFeatureFlags;


	VkCommandBuffer commandBuffer = beginSingleTimeCommands();
	result = NGX_VULKAN_CREATE_DLSS_EXT(commandBuffer, CreationNodeMask, VisibilityNodeMask, &dlssFeature, paramsDLSS, &DlssCreateParams);
	
	if (NVSDK_NGX_FAILED(result)) {
		spdlog::error(L"Failed to create DLSS Features = {}, info: {}", result, GetNGXResultAsString(result));
		return;
	}
	endSingleTimeCommands(commandBuffer);
	
}

void RenderDLSS(VkCommandBuffer commandBuffer, uint32_t imageIndex, float sharpness) {
	
	//nvrhi::ITexture *depthTexture = gbufferWasRasterized ? renderTargets.DeviceDepth : renderTargets.DeviceDepthUAV;

	NVSDK_NGX_Resource_VK inColorResource = NVSDK_NGX_Create_ImageView_Resource_VK(sceneGLTF.storageImagesRaytrace[imageIndex].view, sceneGLTF.storageImagesRaytrace[imageIndex].image, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, sceneGLTF.storageImagesRaytrace[imageIndex].format, WIDTH, HEIGHT, true);
	NVSDK_NGX_Resource_VK outColorResource = NVSDK_NGX_Create_ImageView_Resource_VK(sceneGLTF.storageImagesDLSS[imageIndex].view, sceneGLTF.storageImagesDLSS[imageIndex].image, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, sceneGLTF.storageImagesDLSS[imageIndex].format, WIDTH, HEIGHT, true);

	NVSDK_NGX_Resource_VK depthResource = NVSDK_NGX_Create_ImageView_Resource_VK(sceneGLTF.storageImagesDepth[imageIndex].view, sceneGLTF.storageImagesDepth[imageIndex].image, {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}, sceneGLTF.storageImagesDepth[imageIndex].format, WIDTH, HEIGHT, true);
	NVSDK_NGX_Resource_VK motionVectorResource = NVSDK_NGX_Create_ImageView_Resource_VK(sceneGLTF.storageImagesMotionVector[imageIndex].view, sceneGLTF.storageImagesMotionVector[imageIndex].image, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, sceneGLTF.storageImagesMotionVector[imageIndex].format, WIDTH, HEIGHT, true);
	//NVSDK_NGX_Resource_VK motionVectorResource = NVSDK_NGX_Create_ImageView_Resource_VK(colorImageView, colorImage, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, swapChainImageFormat, WIDTH, HEIGHT, true);
	

	VkImageSubresourceRange subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	VkImageSubresourceRange subresourceRangeDepth = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
	setImageLayout(commandBuffer, inColorResource.Resource.ImageViewInfo.Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);
	setImageLayout(commandBuffer, outColorResource.Resource.ImageViewInfo.Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, subresourceRange);
	setImageLayout(commandBuffer, depthResource.Resource.ImageViewInfo.Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRangeDepth);
	setImageLayout(commandBuffer, motionVectorResource.Resource.ImageViewInfo.Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, subresourceRange);

	NVSDK_NGX_VK_DLSS_Eval_Params evalParams = {};
	evalParams.Feature.pInColor = &inColorResource;
	evalParams.Feature.pInOutput = &outColorResource;
	evalParams.Feature.InSharpness = sharpness;
	evalParams.pInDepth = &depthResource;
	evalParams.pInMotionVectors = &motionVectorResource;
	evalParams.pInExposureTexture = nullptr;//exposureResource;
	evalParams.InReset = 0;//resetHistory;
	evalParams.InJitterOffsetX = jitterCam.x;
	evalParams.InJitterOffsetY = jitterCam.y;
	evalParams.InRenderSubrectDimensions.Width = static_cast<unsigned int>(WIDTH * DLSS_SCALE);
	evalParams.InRenderSubrectDimensions.Height = static_cast<unsigned int>(HEIGHT * DLSS_SCALE);

	NVSDK_NGX_Result result = NGX_VULKAN_EVALUATE_DLSS_EXT(commandBuffer, dlssFeature, paramsDLSS, &evalParams);
	
	if (result != NVSDK_NGX_Result_Success) {
		spdlog::warn(L"Failed to evaluate DLSS feature: {}, info: {}", result, GetNGXResultAsString(result));
		return;
	}

	setImageLayout(commandBuffer, inColorResource.Resource.ImageViewInfo.Image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, subresourceRange);
	setImageLayout(commandBuffer, outColorResource.Resource.ImageViewInfo.Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, subresourceRange);
	setImageLayout(commandBuffer, depthResource.Resource.ImageViewInfo.Image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, subresourceRangeDepth);
	setImageLayout(commandBuffer, motionVectorResource.Resource.ImageViewInfo.Image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, subresourceRange);
}