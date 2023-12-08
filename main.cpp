/*
 *  Copyright Vulkanite - 2022  - Thomas Simonnet
 */
#include "core_utils.h"

#include <algorithm> // Necessary for std::clamp
#include <array>
#include <cstdint> // Necessary for uint32_t
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits> // Necessary for std::numeric_limits
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>
#include <chrono>

#ifdef ACTIVATE_IMGUI
#include <imgui.h>
#include <implot.h>
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#endif

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <spdlog/spdlog.h>

#include "materialx.h"
#include "camera.h"
#include "dlss.h"
#include "rasterizer.h"
#include "raytrace.h"
#include "scene.h"
#include "imgui_widget.h"
#include "fsr2.h"

const std::vector<const char*> validationLayers = { "VK_LAYER_KHRONOS_validation" };
std::vector<const char*> deviceExtensions = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
	VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
	VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME,
	VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
	VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
	VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME,
	VK_KHR_SPIRV_1_4_EXTENSION_NAME,
};

struct QueueFamilyIndices {
	std::optional<uint32_t> graphicsFamily;
	std::optional<uint32_t> presentFamily;
	bool isComplete() { return graphicsFamily.has_value() && presentFamily.has_value(); }
};

struct SwapChainSupportDetails {
	VkSurfaceCapabilitiesKHR capabilities;
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> presentModes;
};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

VkResult CreateDebugUtilsMessengerEXT( VkInstance instance,
	const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
	const VkAllocationCallbacks* pAllocator,
	VkDebugUtilsMessengerEXT* pDebugMessenger ) {
	auto func = ( PFN_vkCreateDebugUtilsMessengerEXT )vkGetInstanceProcAddr( instance, "vkCreateDebugUtilsMessengerEXT" );
	if( func != nullptr ) {
		return func( instance, pCreateInfo, pAllocator, pDebugMessenger );
	} else {
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}
}

void DestroyDebugUtilsMessengerEXT( VkInstance instance,
	VkDebugUtilsMessengerEXT debugMessenger,
	const VkAllocationCallbacks* pAllocator ) {
	auto func = ( PFN_vkDestroyDebugUtilsMessengerEXT )vkGetInstanceProcAddr( instance, "vkDestroyDebugUtilsMessengerEXT" );
	if( func != nullptr ) {
		func( instance, debugMessenger, pAllocator );
	}
}

class VulkaniteApplication {
public:
	void run() {
		initWindow();
		initVulkan();
		mainLoop();
		cleanup();
	}

private:
	double lastTimeFrame = glfwGetTime(), currentTimeFrame;
	GLFWwindow* window;
	VkSurfaceKHR surface;
	VkSwapchainKHR swapChain;
	std::vector<VkImageView> swapChainImageViews;
	std::vector<VkCommandBuffer> commandBuffers;

	std::vector<VkSemaphore> imageAvailableSemaphores;
	std::vector<VkSemaphore> renderFinishedSemaphores;
	std::vector<VkFence> inFlightFences;
	
#ifdef ACTIVATE_IMGUI
	VkDescriptorPool         imguiDescriptorPool{ VK_NULL_HANDLE };
	ImGui_ImplVulkanH_Window imguiMainWindowData;
#endif

	bool framebufferResized = false;

	uint32_t currentFrame = 0, frameIndex = 0;

	void initWindow() {
		glfwInit();

		glfwWindowHint( GLFW_CLIENT_API, GLFW_NO_API );
		glfwWindowHint( GLFW_RESIZABLE, GLFW_TRUE );

		window = glfwCreateWindow( WIDTH, HEIGHT, "Vulkanite", nullptr, nullptr );
		glfwSetWindowUserPointer( window, this );
		glfwSetFramebufferSizeCallback( window, framebufferResizeCallback );
	}

	static void framebufferResizeCallback( GLFWwindow* window, int width, int height ) {
		auto app = reinterpret_cast< VulkaniteApplication* >( glfwGetWindowUserPointer( window ) );
		app->framebufferResized = true;
	}

	void initVulkan() {
		createInstance();
		setupDebugMessenger();
		createSurface();
		pickPhysicalDevice();
		createLogicalDevice();
		createSwapChain();
		createImageViews();

		createCommandPool();
		createColorResources();

		createCommandBuffer();
		createSyncObjects();

		initScene();
		
#ifdef ACTIVATE_IMGUI
		initImgui();
#endif
	}

	void mainLoop() {
		while( !glfwWindowShouldClose( window ) && glfwGetKey( window, GLFW_KEY_ESCAPE ) != GLFW_PRESS ) {
			glfwPollEvents();
			drawFrame();
		}
		vkDeviceWaitIdle( device );
	}

	void cleanupSwapChain() {
		vkDestroyImageView( device, colorImageView, nullptr );
		vkDestroyImage( device, colorImage, nullptr );
		vkFreeMemory( device, colorImageMemory, nullptr );

		for( auto imageView : swapChainImageViews ) {
			vkDestroyImageView( device, imageView, nullptr );
		}

		vkDestroySwapchainKHR( device, swapChain, nullptr );
	}

	void cleanup() {
		cleanupSwapChain();

		deleteModel();

		for( size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++ ) {
			vkDestroySemaphore( device, renderFinishedSemaphores[i], nullptr );
			vkDestroySemaphore( device, imageAvailableSemaphores[i], nullptr );
			vkDestroyFence( device, inFlightFences[i], nullptr );
		}

		vkDestroyCommandPool( device, commandPool, nullptr );

		vkDestroyDevice( device, nullptr );

		if( enableValidationLayers ) {
			DestroyDebugUtilsMessengerEXT( instance, debugMessenger, nullptr );
		}

		vkDestroySurfaceKHR( instance, surface, nullptr );
		vkDestroyInstance( instance, nullptr );

		glfwDestroyWindow( window );

		glfwTerminate();
	}

	void createInstance() {
		if( enableValidationLayers && !checkValidationLayerSupport() ) {
			throw std::runtime_error( "validation layers requested, but not available!" );
		}

		VkApplicationInfo appInfo{};
		appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		appInfo.pApplicationName = "Vulkanite";
		appInfo.applicationVersion = VK_MAKE_VERSION( 1, 0, 0 );
		appInfo.pEngineName = "Vulkanite";
		appInfo.engineVersion = VK_MAKE_VERSION( 1, 0, 0 );
		appInfo.apiVersion = VK_API_VERSION_1_3;

		VkInstanceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		createInfo.pApplicationInfo = &appInfo;

		auto extensions = getRequiredExtensions();
		createInfo.enabledExtensionCount = static_cast< uint32_t >( extensions.size() );
		createInfo.ppEnabledExtensionNames = extensions.data();

		VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
		if( enableValidationLayers ) {
			createInfo.enabledLayerCount = static_cast< uint32_t >( validationLayers.size() );
			createInfo.ppEnabledLayerNames = validationLayers.data();

			populateDebugMessengerCreateInfo( debugCreateInfo );
			createInfo.pNext = ( VkDebugUtilsMessengerCreateInfoEXT* )&debugCreateInfo;
		} else {
			createInfo.enabledLayerCount = 0;

			createInfo.pNext = nullptr;
		}

		if( vkCreateInstance( &createInfo, nullptr, &instance ) != VK_SUCCESS ) {
			throw std::runtime_error( "failed to create instance!" );
		}
	}

	void populateDebugMessengerCreateInfo( VkDebugUtilsMessengerCreateInfoEXT& createInfo ) {
		createInfo = {};
		createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		createInfo.pfnUserCallback = debugCallback;
	}

	void setupDebugMessenger() {
		if( !enableValidationLayers )
			return;

		VkDebugUtilsMessengerCreateInfoEXT createInfo;
		populateDebugMessengerCreateInfo( createInfo );

		if( CreateDebugUtilsMessengerEXT( instance, &createInfo, nullptr, &debugMessenger ) != VK_SUCCESS ) {
			throw std::runtime_error( "failed to set up debug messenger!" );
		}
	}

	std::vector<const char*> getRequiredExtensions() {
		uint32_t glfwExtensionCount = 0;
		const char** glfwExtensions;
		glfwExtensions = glfwGetRequiredInstanceExtensions( &glfwExtensionCount );

		std::vector<const char*> extensions( glfwExtensions, glfwExtensions + glfwExtensionCount );

		if( enableValidationLayers ) {
			extensions.push_back( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
		}
				
		// DLSS get extensions needed
		if( USE_DLSS ) {
			unsigned int OutInstanceExtCount;
			const char** OutInstanceExts;
			unsigned int OutDeviceExtCount;
			const char** OutDeviceExts;
			getExtensionsNeeded( &OutInstanceExtCount, &OutInstanceExts, &OutDeviceExtCount, &OutDeviceExts );
			std::vector<const char*> extensionsInstanceDLSS( OutInstanceExts, OutInstanceExts + OutInstanceExtCount );
			std::vector<const char*> extensionsDeviceDLSS( OutDeviceExts, OutDeviceExts + OutDeviceExtCount );
			// be sure to remove "VK_EXT_buffer_device_address" because we use "VK_KHR_buffer_device_address"
			std::erase_if( extensionsDeviceDLSS, []( const char* a ) { return std::strcmp( a, "VK_EXT_buffer_device_address" ) == 0; } );

			extensions.insert( extensions.end(), extensionsInstanceDLSS.begin(), extensionsInstanceDLSS.end() );
			deviceExtensions.insert( deviceExtensions.end(), extensionsDeviceDLSS.begin(), extensionsDeviceDLSS.end() );
		}
		return extensions;
	}

	void createSurface() {
		if( glfwCreateWindowSurface( instance, window, nullptr, &surface ) != VK_SUCCESS ) {
			throw std::runtime_error( "failed to create window surface!" );
		}
	}

	bool checkValidationLayerSupport() {
		uint32_t layerCount;
		vkEnumerateInstanceLayerProperties( &layerCount, nullptr );

		std::vector<VkLayerProperties> availableLayers( layerCount );
		vkEnumerateInstanceLayerProperties( &layerCount, availableLayers.data() );

		for( const char* layerName : validationLayers ) {
			bool layerFound = false;

			for( const auto& layerProperties : availableLayers ) {
				if( strcmp( layerName, layerProperties.layerName ) == 0 ) {
					layerFound = true;
					break;
				}
			}

			if( !layerFound ) {
				return false;
			}
		}

		return true;
	}

	bool isDeviceSuitable( VkPhysicalDevice device ) {
		QueueFamilyIndices indices = findQueueFamilies( device );

		bool extensionsSupported = checkDeviceExtensionSupport( device );

		bool swapChainAdequate = false;
		if( extensionsSupported ) {
			SwapChainSupportDetails swapChainSupport = querySwapChainSupport( device );
			swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
		}

		VkPhysicalDeviceFeatures supportedFeatures;
		vkGetPhysicalDeviceFeatures( device, &supportedFeatures );

		return indices.isComplete() && extensionsSupported && swapChainAdequate && supportedFeatures.samplerAnisotropy;
	}

	bool checkDeviceExtensionSupport( VkPhysicalDevice device ) {
		uint32_t extensionCount;
		vkEnumerateDeviceExtensionProperties( device, nullptr, &extensionCount, nullptr );

		std::vector<VkExtensionProperties> availableExtensions( extensionCount );
		vkEnumerateDeviceExtensionProperties( device, nullptr, &extensionCount, availableExtensions.data() );

		std::set<std::string> requiredExtensions( deviceExtensions.begin(), deviceExtensions.end() );

		for( const auto& extension : availableExtensions ) {
			requiredExtensions.erase( extension.extensionName );
		}

		return requiredExtensions.empty();
	}

	VkSampleCountFlagBits getMaxUsableSampleCount() {
		VkPhysicalDeviceProperties physicalDeviceProperties;
		vkGetPhysicalDeviceProperties( physicalDevice, &physicalDeviceProperties );

		VkSampleCountFlags counts = physicalDeviceProperties.limits.framebufferColorSampleCounts &
			physicalDeviceProperties.limits.framebufferDepthSampleCounts;
		//if (counts & VK_SAMPLE_COUNT_64_BIT) { return VK_SAMPLE_COUNT_64_BIT; }
		//if (counts & VK_SAMPLE_COUNT_32_BIT) { return VK_SAMPLE_COUNT_32_BIT; }
		//if (counts & VK_SAMPLE_COUNT_16_BIT) { return VK_SAMPLE_COUNT_16_BIT; }
		//if (counts & VK_SAMPLE_COUNT_8_BIT) { return VK_SAMPLE_COUNT_8_BIT; }
		//if (counts & VK_SAMPLE_COUNT_4_BIT) { return VK_SAMPLE_COUNT_4_BIT; }
		//if (counts & VK_SAMPLE_COUNT_2_BIT) { return VK_SAMPLE_COUNT_2_BIT; }

		return VK_SAMPLE_COUNT_1_BIT;
	}

	void pickPhysicalDevice() {
		uint32_t deviceCount = 0;
		vkEnumeratePhysicalDevices( instance, &deviceCount, nullptr );

		if( deviceCount == 0 ) {
			throw std::runtime_error( "failed to find GPUs with Vulkan support!" );
		}

		std::vector<VkPhysicalDevice> devices( deviceCount );
		vkEnumeratePhysicalDevices( instance, &deviceCount, devices.data() );

		for( const auto& device : devices ) {
			if( isDeviceSuitable( device ) ) {
				physicalDevice = device;
				msaaSamples = getMaxUsableSampleCount();
				break;
			}
		}

		if( physicalDevice == VK_NULL_HANDLE ) {
			throw std::runtime_error( "failed to find a suitable GPU!" );
		}
	}

	QueueFamilyIndices findQueueFamilies( VkPhysicalDevice device ) {
		QueueFamilyIndices indices;

		uint32_t queueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, nullptr );

		std::vector<VkQueueFamilyProperties> queueFamilies( queueFamilyCount );
		vkGetPhysicalDeviceQueueFamilyProperties( device, &queueFamilyCount, queueFamilies.data() );

		int i = 0;
		for( const auto& queueFamily : queueFamilies ) {
			if( queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT ) {
				indices.graphicsFamily = i;
			}

			VkBool32 presentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR( device, i, surface, &presentSupport );
			if( presentSupport ) {
				indices.presentFamily = i;
			}

			if( indices.isComplete() ) {
				break;
			}
			i++;
		}

		return indices;
	}

	void createLogicalDevice() {
		QueueFamilyIndices indices = findQueueFamilies( physicalDevice );

		std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
		std::set<uint32_t> uniqueQueueFamilies = { indices.graphicsFamily.value(), indices.presentFamily.value() };

		float queuePriority = 1.0f;
		for( uint32_t queueFamily : uniqueQueueFamilies ) {
			VkDeviceQueueCreateInfo queueCreateInfo{};
			queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			queueCreateInfo.queueFamilyIndex = queueFamily;
			queueCreateInfo.queueCount = 1;
			queueCreateInfo.pQueuePriorities = &queuePriority;
			queueCreateInfos.push_back( queueCreateInfo );
		}

		VkPhysicalDeviceFeatures deviceFeatures{};
		deviceFeatures.samplerAnisotropy = VK_TRUE;
		deviceFeatures.sampleRateShading = VK_TRUE; // enable sample shading feature for the device

		VkDeviceCreateInfo createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

		createInfo.queueCreateInfoCount = static_cast< uint32_t >( queueCreateInfos.size() );
		createInfo.pQueueCreateInfos = queueCreateInfos.data();

		VkPhysicalDeviceDescriptorIndexingFeatures enableDescriptorIndexingFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES };
		enableDescriptorIndexingFeatures.runtimeDescriptorArray = true;

		// Enable features required for ray tracing using feature chaining via pNext
		VkPhysicalDeviceBufferDeviceAddressFeatures enabledBufferDeviceAddresFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES };
		enabledBufferDeviceAddresFeatures.bufferDeviceAddress = VK_TRUE;
		enabledBufferDeviceAddresFeatures.pNext = &enableDescriptorIndexingFeatures;

		VkPhysicalDeviceRayTracingPipelineFeaturesKHR enabledRayTracingPipelineFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR };
		enabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
		enabledRayTracingPipelineFeatures.pNext = &enabledBufferDeviceAddresFeatures;

		VkPhysicalDeviceAccelerationStructureFeaturesKHR enabledAccelerationStructureFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
		enabledAccelerationStructureFeatures.accelerationStructure = VK_TRUE;
		enabledAccelerationStructureFeatures.pNext = &enabledRayTracingPipelineFeatures;

		VkPhysicalDeviceFeatures2 physicalDeviceFeatures2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		physicalDeviceFeatures2.features = deviceFeatures;
		physicalDeviceFeatures2.pNext = &enabledAccelerationStructureFeatures;
		createInfo.pEnabledFeatures = nullptr;
		createInfo.pNext = &physicalDeviceFeatures2;

		createInfo.enabledExtensionCount = static_cast< uint32_t >( deviceExtensions.size() );
		createInfo.ppEnabledExtensionNames = deviceExtensions.data();

		if( enableValidationLayers ) {
			createInfo.enabledLayerCount = static_cast< uint32_t >( validationLayers.size() );
			createInfo.ppEnabledLayerNames = validationLayers.data();
		} else {
			createInfo.enabledLayerCount = 0;
		}

		if( vkCreateDevice( physicalDevice, &createInfo, nullptr, &device ) != VK_SUCCESS ) {
			throw std::runtime_error( "failed to create logical device!" );
		}
		vkGetDeviceQueue( device, indices.graphicsFamily.value(), 0, &graphicsQueue );
		vkGetDeviceQueue( device, indices.presentFamily.value(), 0, &presentQueue );

		// raytrace
		VkPhysicalDeviceProperties2 deviceProperties2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
		deviceProperties2.pNext = &rayTracingPipelineProperties;
		vkGetPhysicalDeviceProperties2( physicalDevice, &deviceProperties2 );
		VkPhysicalDeviceFeatures2 deviceFeatures2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
		deviceFeatures2.pNext = &accelerationStructureFeatures;
		vkGetPhysicalDeviceFeatures2( physicalDevice, &deviceFeatures2 );
	}

	SwapChainSupportDetails querySwapChainSupport( VkPhysicalDevice device ) {
		SwapChainSupportDetails details;

		vkGetPhysicalDeviceSurfaceCapabilitiesKHR( device, surface, &details.capabilities );

		uint32_t formatCount;
		vkGetPhysicalDeviceSurfaceFormatsKHR( device, surface, &formatCount, nullptr );

		if( formatCount != 0 ) {
			details.formats.resize( formatCount );
			vkGetPhysicalDeviceSurfaceFormatsKHR( device, surface, &formatCount, details.formats.data() );
		}

		uint32_t presentModeCount;
		vkGetPhysicalDeviceSurfacePresentModesKHR( device, surface, &presentModeCount, nullptr );

		if( presentModeCount != 0 ) {
			details.presentModes.resize( presentModeCount );
			vkGetPhysicalDeviceSurfacePresentModesKHR( device, surface, &presentModeCount, details.presentModes.data() );
		}

		return details;
	}

	void recreateSwapChain() {
		int width = 0, height = 0;
		glfwGetFramebufferSize( window, &width, &height );
		while( width == 0 || height == 0 ) {
			glfwGetFramebufferSize( window, &width, &height );
			glfwWaitEvents();
		}

		vkDeviceWaitIdle( device );

		cleanupSwapChain();

		createSwapChain();
		createImageViews();
		createColorResources();
		
		createSceneFramebuffer();
		vulkanite_raytrace::createDescriptorSets();
		
		initFSR2();
	}

	void createSwapChain() {
		SwapChainSupportDetails swapChainSupport = querySwapChainSupport( physicalDevice );

		VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat( swapChainSupport.formats );
		VkPresentModeKHR presentMode = chooseSwapPresentMode( swapChainSupport.presentModes );
		VkExtent2D extent = chooseSwapExtent( swapChainSupport.capabilities );

		MAX_FRAMES_IN_FLIGHT = swapChainSupport.capabilities.minImageCount + 1;
		if( swapChainSupport.capabilities.maxImageCount > 0 && MAX_FRAMES_IN_FLIGHT > swapChainSupport.capabilities.
			maxImageCount ) {
			MAX_FRAMES_IN_FLIGHT = swapChainSupport.capabilities.maxImageCount;
		}

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = surface;

		createInfo.minImageCount = MAX_FRAMES_IN_FLIGHT;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		QueueFamilyIndices indices = findQueueFamilies( physicalDevice );
		uint32_t queueFamilyIndices[] = { indices.graphicsFamily.value(), indices.presentFamily.value() };

		if( indices.graphicsFamily != indices.presentFamily ) {
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
		} else {
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			createInfo.queueFamilyIndexCount = 0; // Optional
			createInfo.pQueueFamilyIndices = nullptr; // Optional
		}

		createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = VK_NULL_HANDLE;

		if( vkCreateSwapchainKHR( device, &createInfo, nullptr, &swapChain ) != VK_SUCCESS ) {
			throw std::runtime_error( "failed to create swap chain!" );
		}

		vkGetSwapchainImagesKHR( device, swapChain, &MAX_FRAMES_IN_FLIGHT, nullptr );
		swapChainImages.resize( MAX_FRAMES_IN_FLIGHT );
		vkGetSwapchainImagesKHR( device, swapChain, &MAX_FRAMES_IN_FLIGHT, swapChainImages.data() );

		swapChainImageFormat = surfaceFormat.format;
		swapChainExtent = extent;
	}

	VkSurfaceFormatKHR chooseSwapSurfaceFormat( const std::vector<VkSurfaceFormatKHR>& availableFormats ) {
		for( const auto& availableFormat : availableFormats ) {
			if( availableFormat.format == VK_FORMAT_R8G8B8A8_UNORM && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ) {
				return availableFormat;
			}
		}
		return availableFormats[0];
	}

	VkPresentModeKHR chooseSwapPresentMode( const std::vector<VkPresentModeKHR>& availablePresentModes ) {
		//for (const auto &availablePresentMode : availablePresentModes) {
		//	if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
		//		return availablePresentMode;
		//	}
		//}
		// setup strict vsync
		return VK_PRESENT_MODE_FIFO_KHR;
	}

	VkExtent2D chooseSwapExtent( const VkSurfaceCapabilitiesKHR& capabilities ) {
		if( capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max() ) {
			return capabilities.currentExtent;
		} else {
			int width, height;
			glfwGetFramebufferSize( window, &width, &height );

			VkExtent2D actualExtent = { static_cast< uint32_t >( width ), static_cast< uint32_t >( height ) };

			actualExtent.width = std::clamp( actualExtent.width, capabilities.minImageExtent.width,
				capabilities.maxImageExtent.width );
			actualExtent.height = std::clamp( actualExtent.height, capabilities.minImageExtent.height,
				capabilities.maxImageExtent.height );

			return actualExtent;
		}
	}


	void createImageViews() {
		swapChainImageViews.resize( swapChainImages.size() );

		for( uint32_t i = 0; i < swapChainImages.size(); i++ ) {
			swapChainImageViews[i] = createImageView( swapChainImages[i], swapChainImageFormat,
				VK_IMAGE_ASPECT_COLOR_BIT, 1 );
		}
	}

	static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback( VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
		VkDebugUtilsMessageTypeFlagsEXT messageType,
		const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
		void* pUserData ) {
		std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

		return VK_FALSE;
	}

	void createCommandPool() {
		QueueFamilyIndices queueFamilyIndices = findQueueFamilies( physicalDevice );

		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

		if( vkCreateCommandPool( device, &poolInfo, nullptr, &commandPool ) != VK_SUCCESS ) {
			throw std::runtime_error( "failed to create command pool!" );
		}
	}

	void createColorResources() {
		VkFormat colorFormat = swapChainImageFormat;

		createImage( swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorImage, colorImageMemory );
		colorImageView = createImageView( colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1 );
	}


	void createCommandBuffer() {
		commandBuffers.resize( MAX_FRAMES_IN_FLIGHT );

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = ( uint32_t )commandBuffers.size();

		if( vkAllocateCommandBuffers( device, &allocInfo, commandBuffers.data() ) != VK_SUCCESS ) {
			throw std::runtime_error( "failed to allocate command buffers!" );
		}
	}

	void createSyncObjects() {
		imageAvailableSemaphores.resize( MAX_FRAMES_IN_FLIGHT );
		renderFinishedSemaphores.resize( MAX_FRAMES_IN_FLIGHT );
		inFlightFences.resize( MAX_FRAMES_IN_FLIGHT );

		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for( size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++ ) {
			if( vkCreateSemaphore( device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i] ) != VK_SUCCESS || //
				vkCreateSemaphore( device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i] ) != VK_SUCCESS || //
				vkCreateFence( device, &fenceInfo, nullptr, &inFlightFences[i] ) != VK_SUCCESS ) {
				throw std::runtime_error( "failed to create synchronization objects for a frame!" );
			}
		}
	}

#ifdef ACTIVATE_IMGUI
	void initImgui() {
		// Create Framebuffers
		int w, h;
		glfwGetFramebufferSize( window, &w, &h );
		ImGui_ImplVulkanH_Window* wd = &imguiMainWindowData;
		wd->Surface = surface;
		SwapChainSupportDetails swapChainSupport = querySwapChainSupport( physicalDevice );
		wd->SurfaceFormat = chooseSwapSurfaceFormat( swapChainSupport.formats );
		wd->PresentMode = chooseSwapPresentMode( swapChainSupport.presentModes );

		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImPlot::CreateContext();
		ImGuiIO& io = ImGui::GetIO(); ( void )io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

		// Setup Dear ImGui style
		ImGui::StyleColorsDark();
		//ImGui::StyleColorsLight();

		// Create Descriptor Pool
		{
			VkDescriptorPoolSize pool_sizes[] =
			{
				{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
				{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
				{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
				{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
				{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
				{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
				{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
				{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
				{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
			};
			VkDescriptorPoolCreateInfo pool_info = {};
			pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
			pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
			pool_info.maxSets = 1000 * IM_ARRAYSIZE( pool_sizes );
			pool_info.poolSizeCount = ( uint32_t )IM_ARRAYSIZE( pool_sizes );
			pool_info.pPoolSizes = pool_sizes;
			/*err = */vkCreateDescriptorPool( device, &pool_info, nullptr, &imguiDescriptorPool );
			/*check_vk_result(err);*/
		}

		QueueFamilyIndices indices = findQueueFamilies( physicalDevice );
		// Setup Platform/Renderer backends
		ImGui_ImplGlfw_InitForVulkan( window, true );
		ImGui_ImplVulkan_InitInfo init_info = {};
		init_info.Instance = instance;
		init_info.PhysicalDevice = physicalDevice;
		init_info.Device = device;
		init_info.QueueFamily = indices.graphicsFamily.value();
		init_info.Queue = graphicsQueue;
		init_info.PipelineCache = VK_NULL_HANDLE;
		init_info.DescriptorPool = imguiDescriptorPool;
		init_info.Subpass = 0;
		init_info.MinImageCount = MAX_FRAMES_IN_FLIGHT;
		init_info.ImageCount = MAX_FRAMES_IN_FLIGHT;
		init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		init_info.Allocator = nullptr;
		init_info.CheckVkResultFn = nullptr;//check_vk_result;
		ImGui_ImplVulkan_Init( &init_info, scene.renderPass );

		// Upload Fonts
		{
			// Use any command queue
			VkCommandBuffer command_buffer = commandBuffers[currentFrame];

			/*err =*/ vkResetCommandPool( device, commandPool, 0 );
			//check_vk_result( err );
			VkCommandBufferBeginInfo begin_info = {};
			begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			begin_info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			/*err =*/ vkBeginCommandBuffer( command_buffer, &begin_info );
			//check_vk_result( err );

			ImGui_ImplVulkan_CreateFontsTexture( command_buffer );

			VkSubmitInfo end_info = {};
			end_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			end_info.commandBufferCount = 1;
			end_info.pCommandBuffers = &command_buffer;
			/*err = */vkEndCommandBuffer( command_buffer );
			//check_vk_result( err );
			/*err =*/ vkQueueSubmit( graphicsQueue, 1, &end_info, VK_NULL_HANDLE );
			//check_vk_result( err );

			/*err = */vkDeviceWaitIdle( device );
			//check_vk_result( err );
			ImGui_ImplVulkan_DestroyFontUploadObjects();
		}
	}
#endif
#pragma optimize("", off)
	void drawFrame() {
		currentTimeFrame = glfwGetTime();
		scene.deltaTime = currentTimeFrame - lastTimeFrame;
		lastTimeFrame = currentTimeFrame;
		
#ifdef ACTIVATE_IMGUI
		// 
		// Start the Dear ImGui frame
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		ImGui::Begin( "Vulkanite" );
#endif
		// draw fps plot
		ShowFPS();

		// update element
		updateCamera( window, scene.deltaTime );
		updateJitter( jitterCam, frameIndex );
		updateScene( scene.deltaTime );
		
#ifdef ACTIVATE_IMGUI
		// Imgui end the window
		ImGui::End();
#endif

		vkWaitForFences( device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX );

		uint32_t imageIndex;
		VkResult result = vkAcquireNextImageKHR( device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex );

		if( result == VK_ERROR_OUT_OF_DATE_KHR ) {
			recreateSwapChain();
			return;
		} else if( result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR ) {
			throw std::runtime_error( "failed to acquire swap chain image!" );
		}

		vkResetFences( device, 1, &inFlightFences[currentFrame] );

		//// record draw 
		vkResetCommandBuffer( commandBuffers[currentFrame], 0 );

		if(!scene.DRAW_RASTERIZE)
			vulkanite_raytrace::updateUniformBuffersRaytrace( frameIndex );
			
#ifdef ACTIVATE_IMGUI
		// Imgui Rendering
		ImGui::Render();
		ImDrawData* draw_data = ImGui::GetDrawData();
#endif

		// scene rendring
#ifdef ACTIVATE_IMGUI
		recordCommandBuffer( commandBuffers[currentFrame], currentFrame, draw_data );
#else
		recordCommandBuffer( commandBuffers[currentFrame], currentFrame );
#endif

		// Submit
		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		// submit the record
		VkSemaphore waitSemaphores[] = { imageAvailableSemaphores[currentFrame] };
		VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
		submitInfo.waitSemaphoreCount = 1;
		submitInfo.pWaitSemaphores = waitSemaphores;
		submitInfo.pWaitDstStageMask = waitStages;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

		VkSemaphore signalSemaphores[] = { renderFinishedSemaphores[currentFrame] };
		submitInfo.signalSemaphoreCount = 1;
		submitInfo.pSignalSemaphores = signalSemaphores;

		if( vkQueueSubmit( graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame] ) != VK_SUCCESS ) {
			throw std::runtime_error( "failed to submit draw command buffer!" );
		}

		// present
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = signalSemaphores;

		VkSwapchainKHR swapChains[] = { swapChain };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapChains;
		presentInfo.pImageIndices = &imageIndex;
		presentInfo.pResults = nullptr; // Optional

		result = vkQueuePresentKHR( presentQueue, &presentInfo );

		if( result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized ) {
			framebufferResized = false;
			recreateSwapChain();
		} else if( result != VK_SUCCESS ) {
			throw std::runtime_error( "failed to present swap chain image!" );
		}

		currentFrame = ( currentFrame + 1 ) % MAX_FRAMES_IN_FLIGHT;
		frameIndex++;
	}
};
int main() {
	//#ifdef _DEBUG
	spdlog::set_level( spdlog::level::debug );
	//#else
	//	spdlog::set_level(spdlog::level::info);
	//#endif
	spdlog::info( "Welcome to Vulkanite!" );

	VulkaniteApplication app;


	//generateGlsl("F:\\Projets_Perso\\Vulkanite\\Vulkanite\\models\\standard_surface_chess_set.mtlx", "F:\\Projets_Perso\\Vulkanite\\Vulkanite\\models\\standard_surface_chess_set\\");


	//	try {
	app.run();
	/*	}
		catch (const std::exception &e) {
			std::cerr << e.what() << std::endl;
			return EXIT_FAILURE;
		}*/

	return EXIT_SUCCESS;
}
