#include "dlss.h"

#include "core_utils.h"
#include <nvsdk_ngx_helpers_vk.h>
#include <nvsdk_ngx_helpers.h>

#include <spdlog/spdlog.h>

#include "camera.h"
#include "scene.h"
#include "texture.h"

NVSDK_NGX_Parameter *paramsDLSS = nullptr;
NVSDK_NGX_Handle *dlssFeature = nullptr;

void getExtensionsNeeded(unsigned int *OutInstanceExtCount, const char ***OutInstanceExts, unsigned int *OutDeviceExtCount, const char ***OutDeviceExts) {
	auto result = NVSDK_NGX_VULKAN_RequiredExtensions(OutInstanceExtCount, OutInstanceExts, OutDeviceExtCount, OutDeviceExts);
}

static void NVSDK_CONV NgxLogCallback(const char *message, NVSDK_NGX_Logging_Level loggingLevel, NVSDK_NGX_Feature sourceComponent) {
	std::string s(message);
	s.pop_back();
	spdlog::info("NGX: {}", s);
}

bool initDLSS() {
	createStorageImage(scene.storageImagesDLSS, swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, {swapChainExtent.width, swapChainExtent.height, 1});
	
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
		return false;
	}


	result = NVSDK_NGX_VULKAN_GetCapabilityParameters(&paramsDLSS);

	// get optimal value
	uint32_t InUserSelectedWidth = WIDTH;
	uint32_t InUserSelectedHeight = HEIGHT;
	NVSDK_NGX_PerfQuality_Value InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_MaxQuality;
	uint32_t pOutRenderOptimalWidth;
	uint32_t pOutRenderOptimalHeight;
	uint32_t pOutRenderMaxWidth;
	uint32_t pOutRenderMaxHeight;
	uint32_t pOutRenderMinWidth;
	uint32_t pOutRenderMinHeight;
	float pOutSharpness;
	result = NGX_DLSS_GET_OPTIMAL_SETTINGS(paramsDLSS, InUserSelectedWidth, InUserSelectedHeight, InPerfQualityValue, &pOutRenderOptimalWidth, &pOutRenderOptimalHeight,
	                                       &pOutRenderMaxWidth, &pOutRenderMaxHeight, &pOutRenderMinWidth, &pOutRenderMinHeight, &pOutSharpness);

	// set optimal dlss // for now, max
	DLSS_SCALE = 1.f;// static_cast<float>(pOutRenderOptimalHeight) / static_cast<float>(InUserSelectedHeight);

	//
	unsigned int CreationNodeMask = 1;
	unsigned int VisibilityNodeMask = 1;

	int MotionVectorResolutionLow = 1; // we let the Snippet do the upsampling of the motion vector
	// Next create features
	int DlssCreateFeatureFlags = NVSDK_NGX_DLSS_Feature_Flags_None;
	DlssCreateFeatureFlags |= MotionVectorResolutionLow ? NVSDK_NGX_DLSS_Feature_Flags_MVLowRes : 0;
	DlssCreateFeatureFlags |= /*isContentHDR ? NVSDK_NGX_DLSS_Feature_Flags_IsHDR : */0;
	DlssCreateFeatureFlags |= 0 /*depthInverted ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0*/;
	DlssCreateFeatureFlags |= 0 /*enableSharpening ? NVSDK_NGX_DLSS_Feature_Flags_DoSharpening :  0*/;
	DlssCreateFeatureFlags |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure /* enableAutoExposure ? NVSDK_NGX_DLSS_Feature_Flags_AutoExposure : 0*/;
	DlssCreateFeatureFlags |= 0;//NVSDK_NGX_DLSS_Feature_Flags_MVJittered;

	NVSDK_NGX_DLSS_Create_Params DlssCreateParams;

	memset(&DlssCreateParams, 0, sizeof(DlssCreateParams));

	DlssCreateParams.Feature.InWidth = WIDTH * DLSS_SCALE;
	DlssCreateParams.Feature.InHeight = HEIGHT * DLSS_SCALE;
	DlssCreateParams.Feature.InTargetWidth = WIDTH;
	DlssCreateParams.Feature.InTargetHeight = HEIGHT;
	DlssCreateParams.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_MaxQuality;
	DlssCreateParams.InFeatureCreateFlags = DlssCreateFeatureFlags;


	VkCommandBuffer commandBuffer = beginSingleTimeCommands();
	result = NGX_VULKAN_CREATE_DLSS_EXT(commandBuffer, CreationNodeMask, VisibilityNodeMask, &dlssFeature, paramsDLSS, &DlssCreateParams);

	if (NVSDK_NGX_FAILED(result)) {
		spdlog::error(L"Failed to create DLSS Features = {}, info: {}", result, GetNGXResultAsString(result));
		return false;
	}
	endSingleTimeCommands(commandBuffer);
	return true;
}

void RenderDLSS(VkCommandBuffer commandBuffer, uint32_t imageIndex, float sharpness) {

	NVSDK_NGX_Resource_VK inColorResource = NVSDK_NGX_Create_ImageView_Resource_VK(scene.storageImagesRaytrace[imageIndex].view,
	                                                                               scene.storageImagesRaytrace[imageIndex].image, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
	                                                                               scene.storageImagesRaytrace[imageIndex].format, WIDTH, HEIGHT, true);
	NVSDK_NGX_Resource_VK outColorResource = NVSDK_NGX_Create_ImageView_Resource_VK(scene.storageImagesDLSS[imageIndex].view, scene.storageImagesDLSS[imageIndex].image,
	                                                                                {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}, scene.storageImagesDLSS[imageIndex].format, WIDTH,
	                                                                                HEIGHT, true);

	NVSDK_NGX_Resource_VK depthResource = NVSDK_NGX_Create_ImageView_Resource_VK(scene.storageImagesDepth[imageIndex].view, scene.storageImagesDepth[imageIndex].image,
	                                                                             {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1}, scene.storageImagesDepth[imageIndex].format, WIDTH,
	                                                                             HEIGHT, true);
	NVSDK_NGX_Resource_VK motionVectorResource = NVSDK_NGX_Create_ImageView_Resource_VK(scene.storageImagesMotionVector[imageIndex].view,
	                                                                                    scene.storageImagesMotionVector[imageIndex].image,
	                                                                                    {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
	                                                                                    scene.storageImagesMotionVector[imageIndex].format, WIDTH, HEIGHT, true);


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
	evalParams.pInExposureTexture = nullptr; //exposureResource;
	evalParams.InReset = 0; //resetHistory;
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
	setImageLayout(commandBuffer, depthResource.Resource.ImageViewInfo.Image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
	               subresourceRangeDepth);
	setImageLayout(commandBuffer, motionVectorResource.Resource.ImageViewInfo.Image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, subresourceRange);
}
