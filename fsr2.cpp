#include "fsr2.h"

#include "core_utils.h"
#ifdef ACTIVATE_FSR2
#include "FidelityFX/host/ffx_fsr2.h"
#include "FidelityFX/host/backends/vk/ffx_vk.h"
#endif

#include <spdlog/spdlog.h>

#include "rasterizer.h"
#include "camera.h"
#include "scene.h"
#include "texture.h"

#ifdef ACTIVATE_FSR2
FfxFsr2ContextDescription   initializationParameters = {};
FfxFsr2Context              context;
#endif

void FfxMsgCallback(FfxMsgType type, const wchar_t* message)
{
    if (type == FFX_MESSAGE_TYPE_ERROR)
    {
        spdlog::debug(L"FSR2_API_DEBUG_ERROR: {}", message);
    }
    else if (type == FFX_MESSAGE_TYPE_WARNING)
    {
        spdlog::debug(L"FSR2_API_DEBUG_WARNING: {}", message);
    }
}

bool initFSR2() {
#ifdef ACTIVATE_FSR2
  //  UPSCALE_SCALE = 1.5f;

	createStorageImage(scene.storageImagesFSR2, swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, {swapChainExtent.width, swapChainExtent.height, 1});
	createFramebuffers( scene.renderPass, scene.fsr2Framebuffers, scene.storageImagesFSR2, scene.storageImagesDepth );
	
    // Setup VK interface.
    const size_t scratchBufferSize = ffxGetScratchMemorySizeVK(physicalDevice, FFX_FSR2_CONTEXT_COUNT);
    void* scratchBuffer = malloc(scratchBufferSize);
    VkDeviceContext vkDeviceContext = { device, physicalDevice, vkGetDeviceProcAddr };
   FfxErrorCode errorCode = ffxGetInterfaceVK(&initializationParameters.backendInterface, ffxGetDeviceVK(&vkDeviceContext), scratchBuffer, scratchBufferSize, FFX_FSR2_CONTEXT_COUNT);
   // FFX_ASSERT(errorCode == FFX_OK);
   
    initializationParameters.maxRenderSize.width = swapChainExtent.width;
    initializationParameters.maxRenderSize.height = swapChainExtent.height;
    initializationParameters.displaySize.width = swapChainExtent.width;
    initializationParameters.displaySize.height = swapChainExtent.height;
    initializationParameters.flags = FFX_FSR2_ENABLE_DYNAMIC_RESOLUTION;

    //if (m_bInvertedDepth) {
    //    initializationParameters.flags |= FFX_FSR2_ENABLE_DEPTH_INVERTED;// | FFX_FSR2_ENABLE_DEPTH_INFINITE;
    //}

    //if (m_enableDebugCheck)
    //{
        initializationParameters.flags |= FFX_FSR2_ENABLE_DEBUG_CHECKING;
        initializationParameters.fpMessage = &FfxMsgCallback;
    //}
    
    ffxFsr2ContextCreate(&context, &initializationParameters);

	return true;
#else
    return false;
#endif
}

inline FfxSurfaceFormat GetFfxSurfaceFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8_UNORM:
            return FFX_SURFACE_FORMAT_R8_UNORM;
        case VK_FORMAT_R8G8B8A8_UNORM:
            return FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT;
        case VK_FORMAT_R32G32B32A32_SFLOAT:
            return FFX_SURFACE_FORMAT_R32G32B32A32_FLOAT;
        case VK_FORMAT_R32_UINT:
            return FFX_SURFACE_FORMAT_R32_UINT;
        case VK_FORMAT_R16_UINT:
            return FFX_SURFACE_FORMAT_R16_UINT;
        case VK_FORMAT_R8_UINT:
            return FFX_SURFACE_FORMAT_R8_UINT;
        case VK_FORMAT_D32_SFLOAT:
            return FFX_SURFACE_FORMAT_R32_FLOAT;  
        case VK_FORMAT_R32G32_SFLOAT:
            return FFX_SURFACE_FORMAT_R32G32_FLOAT;
        default:
            FFX_ASSERT_MESSAGE(false, "GetFfxSurfaceFormat: Unsupported VkFormat.");
            return FFX_SURFACE_FORMAT_UNKNOWN;
    }
}

inline FfxResourceDescription GetFfxResourceDescription(/*const cauldron::GPUResource* pResource, */const float&width, const float& height,const VkFormat &format, const uint32_t& mipLevels, FfxResourceUsage additionalUsages=FFX_RESOURCE_USAGE_READ_ONLY, bool usage_uav=false)
{
    FfxResourceDescription resourceDescription = { };

    // This is valid
    //if (!pResource)
    //    return resourceDescription;

    //if (pResource->IsBuffer())
    //{
    //    const cauldron::BufferDesc& bufDesc = pResource->GetBufferResource()->GetDesc();

    //    resourceDescription.flags = FFX_RESOURCE_FLAGS_NONE;
    //    resourceDescription.usage  = FFX_RESOURCE_USAGE_UAV;
    //    resourceDescription.width = bufDesc.Size;
    //    resourceDescription.height = bufDesc.Stride;
    //    resourceDescription.format = GetFfxSurfaceFormat(cauldron::ResourceFormat::Unknown);

    //    // Does not apply to buffers
    //    resourceDescription.depth = 0;
    //    resourceDescription.mipCount = 0;

    //    // Set the type
    //    resourceDescription.type = FFX_RESOURCE_TYPE_BUFFER;
    //}
    //else
    //{
    //    const cauldron::TextureDesc& texDesc = pResource->GetTextureResource()->GetDesc();

        // Set flags properly for resource registration
        resourceDescription.flags = FFX_RESOURCE_FLAGS_NONE;
		resourceDescription.usage = isDepth(format) ? FFX_RESOURCE_USAGE_DEPTHTARGET : FFX_RESOURCE_USAGE_READ_ONLY;
        if (usage_uav)
            resourceDescription.usage = (FfxResourceUsage)(resourceDescription.usage | FFX_RESOURCE_USAGE_UAV);
         
        resourceDescription.width = width;
        resourceDescription.height = height;
        resourceDescription.depth = 1;
        resourceDescription.mipCount = mipLevels;
        resourceDescription.format = GetFfxSurfaceFormat(format);

        resourceDescription.usage = (FfxResourceUsage)(resourceDescription.usage | additionalUsages);


         resourceDescription.type = FFX_RESOURCE_TYPE_TEXTURE2D;
   // }

    return resourceDescription;
}

void RenderFSR2(VkCommandBuffer commandBuffer, uint32_t imageIndex, float sharpness) {

#ifdef ACTIVATE_FSR2
    float farPlane = scene.cameraFar;
    float nearPlane = scene.cameraNear;

    //if (m_bInvertedDepth)
    //{
    //    // Cauldron1.0 can have planes inverted. Adjust before providing to FSR2.
    //    std::swap(farPlane, nearPlane);
    //}

    wchar_t inputColor[] = L"FSR2_InputColor";
    wchar_t inputDepth[] = L"FSR2_InputDepth";
    wchar_t inputMotionVectors[] = L"FSR2_InputMotionVectors";
    wchar_t inputExposure[] = L"FSR2_InputExposure";
    wchar_t inputEmptyInputReactiveMap[] = L"FSR2_EmptyInputReactiveMap";
    wchar_t inputEmptyTransparencyAndCompositionMap[] = L"FSR2_EmptyTransparencyAndCompositionMap";
    wchar_t outputUpscaledColor[] = L"FSR2_OutputUpscaledColor";

	FfxFsr2DispatchDescription dispatchParameters = {};
	dispatchParameters.commandList = ffxGetCommandListVK( commandBuffer );

	if( scene.DRAW_RASTERIZE )
        dispatchParameters.color = ffxGetResourceVK(  scene.storageImagesRasterize[imageIndex].image, GetFfxResourceDescription(static_cast< float >( swapChainExtent.width * UPSCALE_SCALE ), static_cast< float >( swapChainExtent.height * UPSCALE_SCALE ), scene.storageImagesRasterize[imageIndex].format, scene.storageImagesRasterize[imageIndex].mipLevels), inputColor, FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ );
	else
        dispatchParameters.color = ffxGetResourceVK(  scene.storageImagesRaytrace[imageIndex].image, GetFfxResourceDescription(static_cast< float >( swapChainExtent.width * UPSCALE_SCALE ), static_cast< float >( swapChainExtent.height * UPSCALE_SCALE ), scene.storageImagesRaytrace[imageIndex].format, scene.storageImagesRaytrace[imageIndex].mipLevels), inputColor, FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ );

    dispatchParameters.depth = ffxGetResourceVK(  scene.storageImagesDepth[imageIndex].image, GetFfxResourceDescription(static_cast< float >( swapChainExtent.width * UPSCALE_SCALE ), static_cast< float >( swapChainExtent.height * UPSCALE_SCALE ), scene.storageImagesDepth[imageIndex].format, scene.storageImagesDepth[imageIndex].mipLevels), inputDepth, FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ );
    
    dispatchParameters.motionVectors = ffxGetResourceVK(  scene.storageImagesMotionVector[imageIndex].image, GetFfxResourceDescription(static_cast< float >( swapChainExtent.width * UPSCALE_SCALE ), static_cast< float >( swapChainExtent.height * UPSCALE_SCALE ), scene.storageImagesMotionVector[imageIndex].format, scene.storageImagesMotionVector[imageIndex].mipLevels), inputMotionVectors, FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ );		
    dispatchParameters.exposure      = ffxGetResourceVK(nullptr, {}, inputExposure, FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);

	//if ((pState->nReactiveMaskMode == ReactiveMaskMode::REACTIVE_MASK_MODE_ON)
	//    || (pState->nReactiveMaskMode == ReactiveMaskMode::REACTIVE_MASK_MODE_AUTOGEN))
	//{
	//    dispatchParameters.reactive = ffxGetTextureResourceVK(&context, cameraSetup.reactiveMapResource->Resource(), cameraSetup.reactiveMapResourceView, cameraSetup.reactiveMapResource->GetWidth(), cameraSetup.reactiveMapResource->GetHeight(), cameraSetup.reactiveMapResource->GetFormat(), L"FSR2_InputReactiveMap");
    //}
    //else
    {
        dispatchParameters.reactive = ffxGetResourceVK( nullptr, {}, inputEmptyInputReactiveMap, FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    }

    //if (pState->bCompositionMask == true)
    //{
    //    dispatchParameters.transparencyAndComposition = ffxGetTextureResourceVK(&context, cameraSetup.transparencyAndCompositionResource->Resource(), cameraSetup.transparencyAndCompositionResourceView, cameraSetup.transparencyAndCompositionResource->GetWidth(), cameraSetup.transparencyAndCompositionResource->GetHeight(), cameraSetup.transparencyAndCompositionResource->GetFormat(), L"FSR2_TransparencyAndCompositionMap");
    //}
    //else
    {
        dispatchParameters.transparencyAndComposition = ffxGetResourceVK( nullptr, {}, inputEmptyTransparencyAndCompositionMap, FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ);
    }
    
    dispatchParameters.output = ffxGetResourceVK(  scene.storageImagesFSR2[imageIndex].image, GetFfxResourceDescription(static_cast< float >( swapChainExtent.width ), static_cast< float >( swapChainExtent.height ), scene.storageImagesFSR2[imageIndex].format, scene.storageImagesFSR2[imageIndex].mipLevels,FFX_RESOURCE_USAGE_READ_ONLY,  true), outputUpscaledColor, FFX_RESOURCE_STATE_UNORDERED_ACCESS );    
    dispatchParameters.jitterOffset.x = jitterCam.x;
    dispatchParameters.jitterOffset.y = jitterCam.y;
    dispatchParameters.motionVectorScale.x = swapChainExtent.width;
    dispatchParameters.motionVectorScale.y = swapChainExtent.height;
    dispatchParameters.reset = false;//pState->bReset;
    dispatchParameters.enableSharpening = false; //pState->bUseRcas;
    dispatchParameters.sharpness = 0.f;//pState->sharpening;
    dispatchParameters.frameTimeDelta = (float)scene.deltaTime * 1000.f;
    dispatchParameters.preExposure = 1.f;
    dispatchParameters.renderSize.width = static_cast< float >( swapChainExtent.width * UPSCALE_SCALE );
    dispatchParameters.renderSize.height = static_cast< float >( swapChainExtent.height * UPSCALE_SCALE );
    dispatchParameters.cameraFar = farPlane;
    dispatchParameters.cameraNear = nearPlane;
    dispatchParameters.cameraFovAngleVertical = scene.cameraFov;
    //pState->bReset = false;

    FfxErrorCode errorCode = ffxFsr2ContextDispatch(&context, &dispatchParameters);
   // FFX_ASSERT(errorCode == FFX_OK);
#endif
}
