#include "fsr2.h"

#include "core_utils.h"
#include "ffx_fsr2.h"
#include "vk/ffx_fsr2_vk.h"

#include <spdlog/spdlog.h>

#include "rasterizer.h"
#include "camera.h"
#include "scene.h"
#include "texture.h"

FfxFsr2ContextDescription   initializationParameters = {};
FfxFsr2Context              context;

bool initFSR2() {
  //  UPSCALE_SCALE = 1.5f;

	createStorageImage(scene.storageImagesFSR2, swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, {swapChainExtent.width, swapChainExtent.height, 1});
	createFramebuffers( scene.renderPass, scene.fsr2Framebuffers, scene.storageImagesFSR2, scene.storageImagesDepth );
	
    // Setup VK interface.
    const size_t scratchBufferSize = ffxFsr2GetScratchMemorySizeVK(physicalDevice);
    void* scratchBuffer = malloc(scratchBufferSize);
    FfxErrorCode errorCode = ffxFsr2GetInterfaceVK(&initializationParameters.callbacks, scratchBuffer, scratchBufferSize, physicalDevice, vkGetDeviceProcAddr);
   // FFX_ASSERT(errorCode == FFX_OK);
   
    initializationParameters.device = ffxGetDeviceVK(device);
    initializationParameters.maxRenderSize.width = WIDTH;
    initializationParameters.maxRenderSize.height = HEIGHT;
    initializationParameters.displaySize.width = WIDTH;
    initializationParameters.displaySize.height = HEIGHT;
    initializationParameters.flags = FFX_FSR2_ENABLE_AUTO_EXPOSURE;

    //if (m_bInvertedDepth) {
    //    initializationParameters.flags |= FFX_FSR2_ENABLE_DEPTH_INVERTED | FFX_FSR2_ENABLE_DEPTH_INFINITE;
    //}

    //if (m_enableDebugCheck)
    //{
    //    initializationParameters.flags |= FFX_FSR2_ENABLE_DEBUG_CHECKING;
    //    initializationParameters.fpMessage = &onFSR2Msg;
    //}
    
    ffxFsr2ContextCreate(&context, &initializationParameters);

	return true;
}

void RenderFSR2(VkCommandBuffer commandBuffer, uint32_t imageIndex, float sharpness) {

    float farPlane = scene.cameraNear;
    float nearPlane = scene.cameraFar;

    //if (m_bInvertedDepth)
    //{
    //    // Cauldron1.0 can have planes inverted. Adjust before providing to FSR2.
    //    std::swap(farPlane, nearPlane);
    //}

	FfxFsr2DispatchDescription dispatchParameters = {};
	dispatchParameters.commandList = ffxGetCommandListVK( commandBuffer );

	if( scene.DRAW_RASTERIZE )
		dispatchParameters.color = ffxGetTextureResourceVK( &context, scene.storageImagesRasterize[imageIndex].image, scene.storageImagesRasterize[imageIndex].view, static_cast< float >( swapChainExtent.width * UPSCALE_SCALE ), static_cast< float >( swapChainExtent.height * UPSCALE_SCALE ), scene.storageImagesRasterize[imageIndex].format, L"FSR2_InputColor" );
	else
		dispatchParameters.color = ffxGetTextureResourceVK( &context, scene.storageImagesRaytrace[imageIndex].image, scene.storageImagesRaytrace[imageIndex].view, static_cast< float >( swapChainExtent.width * UPSCALE_SCALE ), static_cast< float >( swapChainExtent.height * UPSCALE_SCALE ), scene.storageImagesRaytrace[imageIndex].format, L"FSR2_InputColor" );

	dispatchParameters.depth = ffxGetTextureResourceVK( &context, scene.storageImagesDepth[imageIndex].image, scene.storageImagesDepth[imageIndex].view, static_cast< float >( swapChainExtent.width * UPSCALE_SCALE ), static_cast< float >( swapChainExtent.height * UPSCALE_SCALE ), scene.storageImagesDepth[imageIndex].format, L"FSR2_InputDepth" );

	dispatchParameters.motionVectors = ffxGetTextureResourceVK( &context, scene.storageImagesMotionVector[imageIndex].image, scene.storageImagesMotionVector[imageIndex].view, static_cast< float >( swapChainExtent.width * UPSCALE_SCALE ), static_cast< float >( swapChainExtent.height * UPSCALE_SCALE ), scene.storageImagesMotionVector[imageIndex].format, L"FSR2_InputMotionVectors" );
	dispatchParameters.exposure = ffxGetTextureResourceVK( &context, nullptr, nullptr, 1, 1, VK_FORMAT_UNDEFINED, L"FSR2_InputExposure" );

	//if ((pState->nReactiveMaskMode == ReactiveMaskMode::REACTIVE_MASK_MODE_ON)
	//    || (pState->nReactiveMaskMode == ReactiveMaskMode::REACTIVE_MASK_MODE_AUTOGEN))
	//{
	//    dispatchParameters.reactive = ffxGetTextureResourceVK(&context, cameraSetup.reactiveMapResource->Resource(), cameraSetup.reactiveMapResourceView, cameraSetup.reactiveMapResource->GetWidth(), cameraSetup.reactiveMapResource->GetHeight(), cameraSetup.reactiveMapResource->GetFormat(), L"FSR2_InputReactiveMap");
    //}
    //else
    {
        dispatchParameters.reactive = ffxGetTextureResourceVK(&context, nullptr, nullptr, 1, 1, VK_FORMAT_UNDEFINED, L"FSR2_EmptyInputReactiveMap");
    }

    //if (pState->bCompositionMask == true)
    //{
    //    dispatchParameters.transparencyAndComposition = ffxGetTextureResourceVK(&context, cameraSetup.transparencyAndCompositionResource->Resource(), cameraSetup.transparencyAndCompositionResourceView, cameraSetup.transparencyAndCompositionResource->GetWidth(), cameraSetup.transparencyAndCompositionResource->GetHeight(), cameraSetup.transparencyAndCompositionResource->GetFormat(), L"FSR2_TransparencyAndCompositionMap");
    //}
    //else
    {
        dispatchParameters.transparencyAndComposition = ffxGetTextureResourceVK(&context, nullptr, nullptr, 1, 1, VK_FORMAT_UNDEFINED, L"FSR2_EmptyTransparencyAndCompositionMap");
    }

    dispatchParameters.output = ffxGetTextureResourceVK(&context, scene.storageImagesFSR2[imageIndex].image, scene.storageImagesFSR2[imageIndex].view, WIDTH, HEIGHT, scene.storageImagesFSR2[imageIndex].format, L"FSR2_OutputUpscaledColor", FFX_RESOURCE_STATE_UNORDERED_ACCESS);
    dispatchParameters.jitterOffset.x = jitterCam.x;
    dispatchParameters.jitterOffset.y = jitterCam.y;
    dispatchParameters.motionVectorScale.x = swapChainExtent.width;
    dispatchParameters.motionVectorScale.y = swapChainExtent.height;
    dispatchParameters.reset = false;//pState->bReset;
    dispatchParameters.enableSharpening = false; //pState->bUseRcas;
    dispatchParameters.sharpness = 0.f;//pState->sharpening;
    dispatchParameters.frameTimeDelta = (float)scene.deltaTime;
    dispatchParameters.preExposure = 1.0f;
    dispatchParameters.renderSize.width = static_cast< float >( swapChainExtent.width * UPSCALE_SCALE );
    dispatchParameters.renderSize.height = static_cast< float >( swapChainExtent.height * UPSCALE_SCALE );
    dispatchParameters.cameraFar = farPlane;
    dispatchParameters.cameraNear = nearPlane;
    dispatchParameters.cameraFovAngleVertical = scene.cameraFov;
    //pState->bReset = false;

    FfxErrorCode errorCode = ffxFsr2ContextDispatch(&context, &dispatchParameters);
   // FFX_ASSERT(errorCode == FFX_OK);
}
