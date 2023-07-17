#include "texture.h"
#include "scene.h"

#include <functional>
#include <array>

#include <imgui.h>
#include "imgui_impl_vulkan.h"

#include "dlss.h"
#include "rasterizer.h"
#include "raytrace.h"

#include <filesystem>
namespace fs = std::filesystem;


#include <cmrc/cmrc.hpp>
#include <glm/ext/matrix_transform.hpp>
CMRC_DECLARE( gltf_rc );

SceneVulkanite scene;
bool USE_DLSS = true;

void loadScene() {
	scene.envMap.name = "envMap";
	auto cmrcFS = cmrc::gltf_rc::get_filesystem();
	auto envmapRC = cmrcFS.open( ENVMAP );

	createTextureImage( reinterpret_cast< const unsigned char* >( envmapRC.cbegin() ), envmapRC.size(), scene.envMap.textureImage, scene.envMap.textureImageMemory,
		scene.envMap.mipLevels, true );
	scene.envMap.textureImageView = createTextureImageView( scene.envMap.textureImage, scene.envMap.mipLevels, VK_FORMAT_R32G32B32A32_SFLOAT );
	createTextureSampler( scene.envMap.textureSampler, scene.envMap.mipLevels );
	//if (fs::path(MODEL_GLTF_PATH).extension() == ".gltf" || fs::path(MODEL_GLTF_PATH).extension() == ".glb")
	//	scene.roots = loadSceneGLTF(MODEL_GLTF_PATH);
	//else
	//	scene.roots = loadSceneUSD(MODEL_GLTF_PATH);

//	std::string scenePath("models/simple_texture_cube.usda");
//	std::string scenePath("models/pion_chess.usda");
	std::string scenePath( "models/abeautifulgame_draco.glb" );
	//std::string scenePath("models/2_tower.usda");

//	std::string scenePath("models/Ship_in_a_bottle.usdz");
//	std::string scenePath("models/transprent_cube.usda");

//	std::string scenePath("F:\\Movida\\works\\Harfang\\factory\\TechArt.usda");

	if( fs::path( scenePath ).extension() == ".gltf" || fs::path( scenePath ).extension() == ".glb" )
		scene.roots = loadSceneGLTF( scenePath );
	else
		scene.roots = loadSceneUSD( scenePath );
}

void initScene() {
	scene.DRAW_RASTERIZE = true;
	// dlss
	USE_DLSS = initDLSS();
	if( !USE_DLSS )
		DLSS_SCALE = 1.f;


	// setup motion pass
	VkExtent3D extent = { static_cast< uint32_t >( swapChainExtent.width ), static_cast< uint32_t >( swapChainExtent.height ), 1 };
	VkExtent3D extentScale = { static_cast< uint32_t >( swapChainExtent.width * DLSS_SCALE ), static_cast< uint32_t >( swapChainExtent.height * DLSS_SCALE ), 1 };

	// rasterizer framebuffer
	createStorageImage( scene.storageImagesDepth, findDepthFormat(), VK_IMAGE_ASPECT_DEPTH_BIT, extent );
	createStorageImage( scene.storageImagesRasterize, swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, extent );
	createRenderPass( scene.renderPass, swapChainImageFormat, findDepthFormat(), msaaSamples );
	createFramebuffers( scene.renderPass, scene.rasterizerFramebuffers, scene.storageImagesRasterize, scene.storageImagesDepth );

	// motion vector frame buffer
	createStorageImage( scene.storageImagesDepthMotionVector, findDepthFormat(), VK_IMAGE_ASPECT_DEPTH_BIT, extent );
	createStorageImage( scene.storageImagesMotionVector, VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, extent );
	createRenderPass( scene.renderPassMotionVector, VK_FORMAT_R32G32_SFLOAT, findDepthFormat(), VK_SAMPLE_COUNT_1_BIT );
	createFramebuffers( scene.renderPassMotionVector, scene.MotionVectorFramebuffers, scene.storageImagesMotionVector, scene.storageImagesDepthMotionVector );

	// create 2 graphics pipeline (without/without alpha);
	createDescriptorSetLayoutMotionVector( scene.descriptorSetLayout );
	createGraphicsPipeline( "spv/shaderMotionVector.vert.spv", "spv/shaderMotionVector.frag.spv", scene.pipelineLayout, scene.graphicsPipeline, scene.renderPass, VK_SAMPLE_COUNT_1_BIT, scene.descriptorSetLayout, false );
	createGraphicsPipeline( "spv/shaderMotionVector.vert.spv", "spv/shaderMotionVector.frag.spv", scene.pipelineLayoutAlpha, scene.graphicsPipelineAlpha, scene.renderPass, VK_SAMPLE_COUNT_1_BIT, scene.descriptorSetLayout, true );

	createUniformParamsBuffers( sizeof( UBOParams ), scene.uniformParamsBuffers, scene.uniformParamsBuffersMemory, scene.uniformParamsBuffersMapped );
	for( size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++ )
		updateUniformParamsBuffer( scene.uboParams, scene.uniformParamsBuffersMapped, i );

	// load
	loadScene();

	// setup raytrace
	createStorageImage( scene.storageImagesRaytrace, swapChainImageFormat, VK_IMAGE_ASPECT_COLOR_BIT, extentScale );
	createFramebuffers( scene.renderPass, scene.imguiFramebuffers, scene.storageImagesRaytrace, scene.storageImagesDepth );

	vulkanite_raytrace::InitRaytrace();

	// make blas
	std::function<void( const objectVulkanite& )> makeBLASf;
	makeBLASf = [&]( const objectVulkanite& obj ) {
		if( scene.primsMeshCache[obj.primMesh] )
			vulkanite_raytrace::createBottomLevelAccelerationStructure( obj );
		for( const auto& objChild : obj.children )
			makeBLASf( objChild );
	};
	for( const auto& o : scene.roots )
		makeBLASf( o );

	// make las
	std::function<void( objectVulkanite&, const glm::mat4& )> makeTLASf;
	makeTLASf = [&]( objectVulkanite& obj, const glm::mat4& parent_world ) {
		if( scene.primsMeshCache[obj.primMesh] )
			vulkanite_raytrace::createTopLevelAccelerationStructureInstance( obj, parent_world * obj.world, false );
		for( auto& objChild : obj.children )
			makeTLASf( objChild, parent_world * obj.world );
	};
	for( auto& o : scene.roots )
		makeTLASf( o, glm::mat4( 1 ) );

	vulkanite_raytrace::createTopLevelAccelerationStructure( false );

	vulkanite_raytrace::createUniformBuffer();
	vulkanite_raytrace::createRayTracingPipeline();
	vulkanite_raytrace::createShaderBindingTables();
	vulkanite_raytrace::createDescriptorSets();
}

void updateScene( float deltaTime ) {
	// move in circle one pion

	static auto startTime = std::chrono::high_resolution_clock::now();
	auto currentTime = std::chrono::high_resolution_clock::now();
	float timer = std::chrono::duration<float, std::chrono::seconds::period>( currentTime - startTime ).count() * 70.f;

	// update raytrace
	std::function<void( objectVulkanite&, const glm::mat4& )> updateTLASf;
	updateTLASf = [&]( objectVulkanite& obj, const glm::mat4& parent_world ) {
		if( scene.primsMeshCache[obj.primMesh] )
			vulkanite_raytrace::createTopLevelAccelerationStructureInstance( obj, parent_world * obj.world, true );
		for( auto& objChild : obj.children )
			updateTLASf( objChild, parent_world * obj.world );
	};

	// update the 5th object around (moving test for the chess game)
	if( scene.roots.size() ) {
		if( 5 < scene.roots[0].children.size() ) {
			glm::mat4 movingMat = glm::mat4( 1.0f );
			scene.roots[0].children[5].world = glm::translate( movingMat, glm::vec3( cos( glm::radians( timer ) ) * 0.1f, sin( glm::radians( timer ) ) * 0.1f, 0.014927f ) );

			if( !scene.DRAW_RASTERIZE ) {
				for( auto& o : scene.roots[0].children[5].children )
					updateTLASf( o, scene.roots[0].world * scene.roots[0].children[5].world );
			}
		}
	}

	if( !scene.DRAW_RASTERIZE ) {
		vulkanite_raytrace::createTopLevelAccelerationStructure( true );
	}
}

void drawModel( VkCommandBuffer commandBuffer, uint32_t currentFrame, objectVulkanite& obj, const glm::mat4& parent_world, const bool& isRenderingAlphaPass ) {
	auto alphaMask = scene.materialsCache[obj.matCacheID]->alphaMask;
	if( scene.primsMeshCache[obj.primMesh] && ( ( alphaMask == 0.f && !isRenderingAlphaPass ) || (
		alphaMask != 0.f && isRenderingAlphaPass ) ) ) {
		if( scene.DRAW_RASTERIZE ) {
			updateUniformBuffer( currentFrame, obj, parent_world );
		} else {
			if( USE_DLSS )
				updateUniformBufferMotionVector( currentFrame, obj, parent_world );
		}

		vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			alphaMask ? scene.graphicsPipelineAlpha : scene.graphicsPipeline );

		VkBuffer vertexBuffers[] = { scene.primsMeshCache[obj.primMesh]->vertexBuffer };
		VkDeviceSize offsets[] = { 0 };
		vkCmdBindVertexBuffers( commandBuffer, 0, 1, vertexBuffers, offsets );

		vkCmdBindIndexBuffer( commandBuffer, scene.primsMeshCache[obj.primMesh]->indexBuffer, 0, VK_INDEX_TYPE_UINT32 );

		if( currentFrame < obj.descriptorSets.size() )
			vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
				alphaMask ? scene.pipelineLayoutAlpha : scene.pipelineLayout, 0, 1, &obj.descriptorSets[currentFrame], 0,
				nullptr );

		if( scene.DRAW_RASTERIZE ) {
			vkCmdPushConstants( commandBuffer, scene.pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof( uint32_t ), &obj.mat );
		}

		vkCmdDrawIndexed( commandBuffer, static_cast< uint32_t >( scene.primsMeshCache[obj.primMesh]->indices.size() ), 1, 0, 0, 0 );
	}

	for( auto& objChild : obj.children )
		drawModel( commandBuffer, currentFrame, objChild, parent_world * obj.world, isRenderingAlphaPass );
}

void drawScene( VkCommandBuffer commandBuffer, uint32_t currentFrame ) {
	// draw opaque
	for( auto& obj : scene.roots )
		drawModel( commandBuffer, currentFrame, obj, glm::mat4( 1 ), false );
	// draw alpha
	for( auto& obj : scene.roots )
		drawModel( commandBuffer, currentFrame, obj, glm::mat4( 1 ), true );
}

void recordCommandBuffer( VkCommandBuffer commandBuffer, uint32_t currentFrame, ImDrawData* draw_data ) {
	VkCommandBufferBeginInfo beginInfo{};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT; // Optional
	beginInfo.pInheritanceInfo = nullptr; // Optional

	if( vkBeginCommandBuffer( commandBuffer, &beginInfo ) != VK_SUCCESS ) {
		throw std::runtime_error( "failed to begin recording command buffer!" );
	}

	// rasterize motion vector/depth pass
	std::array<VkClearValue, 2> clearValues{};
	clearValues[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderPassInfo{};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = scene.renderPass;
	if(scene.DRAW_RASTERIZE)
		renderPassInfo.framebuffer = scene.rasterizerFramebuffers[currentFrame];
	else
		renderPassInfo.framebuffer = scene.MotionVectorFramebuffers[currentFrame];
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = { static_cast< uint32_t >( swapChainExtent.width * DLSS_SCALE ), static_cast< uint32_t >( swapChainExtent.height * DLSS_SCALE ) };
	renderPassInfo.clearValueCount = static_cast< uint32_t >( clearValues.size() );
	renderPassInfo.pClearValues = clearValues.data();

	vkCmdBeginRenderPass( commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE );

	VkViewport viewport{};
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast< float >( swapChainExtent.width * DLSS_SCALE );
	viewport.height = static_cast< float >( swapChainExtent.height * DLSS_SCALE );
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	vkCmdSetViewport( commandBuffer, 0, 1, &viewport );

	VkRect2D scissor{};
	scissor.offset = { 0, 0 };
	scissor.extent = { static_cast< uint32_t >( swapChainExtent.width * DLSS_SCALE ), static_cast< uint32_t >( swapChainExtent.height * DLSS_SCALE ) };
	vkCmdSetScissor( commandBuffer, 0, 1, &scissor );

	drawScene( commandBuffer, currentFrame );

	// draw raytrace
	if( !scene.DRAW_RASTERIZE ) {
		vkCmdEndRenderPass( commandBuffer ); // end rasterize command buffer

		renderPassInfo.framebuffer = scene.imguiFramebuffers[currentFrame];
		vkCmdBeginRenderPass( commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS );

		// raytrace
		vulkanite_raytrace::buildCommandBuffers( commandBuffer, currentFrame );

		if( USE_DLSS )
			// dlss
			RenderDLSS( commandBuffer, currentFrame, 1.0 );
	}

	// Imgui
	// Record dear imgui primitives into command buffer
	ImGui_ImplVulkan_RenderDrawData( draw_data, commandBuffer );

	vkCmdEndRenderPass( commandBuffer );


	// Copy final output to swap chain image
	VkImageSubresourceRange subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

	// Prepare current swap chain image as transfer destination
	setImageLayout( commandBuffer, swapChainImages[currentFrame], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, subresourceRange );

	// Prepare ray tracing output image as transfer source
	if( scene.DRAW_RASTERIZE ) {
		setImageLayout( commandBuffer, scene.storageImagesRasterize[currentFrame].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, subresourceRange );
	} else {
		if( USE_DLSS )
			setImageLayout( commandBuffer, scene.storageImagesDLSS[currentFrame].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, subresourceRange );
		else
			setImageLayout( commandBuffer, scene.storageImagesRaytrace[currentFrame].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, subresourceRange );
	}

	VkImageCopy copyRegion{};
	copyRegion.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	copyRegion.srcOffset = { 0, 0, 0 };
	copyRegion.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
	// copyRegion.dstOffset = {static_cast<int32_t>(swapChainExtent.width / 2), 0, 0};
	// copyRegion.extent = {swapChainExtent.width / 2, swapChainExtent.height, 1};
	copyRegion.dstOffset = { 0, 0, 0 };
	copyRegion.extent = { swapChainExtent.width, swapChainExtent.height, 1 };
	if( scene.DRAW_RASTERIZE ) {
		vkCmdCopyImage( commandBuffer, scene.storageImagesRasterize[currentFrame].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChainImages[currentFrame],
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
	} else {
		if( USE_DLSS )
			vkCmdCopyImage( commandBuffer, scene.storageImagesDLSS[currentFrame].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChainImages[currentFrame], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
		else
			vkCmdCopyImage( commandBuffer, scene.storageImagesRaytrace[currentFrame].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, swapChainImages[currentFrame], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );
	}

	// Transition swap chain image back for presentation
	setImageLayout( commandBuffer, swapChainImages[currentFrame], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, subresourceRange );

	// Transition ray tracing output image back to general layout
	if( scene.DRAW_RASTERIZE ) {
		setImageLayout( commandBuffer, scene.storageImagesRasterize[currentFrame].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, subresourceRange );
	} else {
		if( USE_DLSS )
			setImageLayout( commandBuffer, scene.storageImagesDLSS[currentFrame].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, subresourceRange );
		else
			setImageLayout( commandBuffer, scene.storageImagesRaytrace[currentFrame].image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, subresourceRange );
	}

	// end command buffer
	if( vkEndCommandBuffer( commandBuffer ) != VK_SUCCESS ) {
		throw std::runtime_error( "failed to record command buffer!" );
	}
}

void destroyScene() {
	vkDestroyDescriptorSetLayout( device, scene.descriptorSetLayout, nullptr );
	vkDestroyPipeline( device, scene.graphicsPipeline, nullptr );
	vkDestroyPipelineLayout( device, scene.pipelineLayout, nullptr );
	vkDestroyPipeline( device, scene.graphicsPipelineAlpha, nullptr );
	vkDestroyPipelineLayout( device, scene.pipelineLayoutAlpha, nullptr );

	vkDestroyRenderPass( device, scene.renderPass, nullptr );

	for( auto framebuffer : scene.rasterizerFramebuffers )
		vkDestroyFramebuffer( device, framebuffer, nullptr );
		
	for( auto framebuffer : scene.MotionVectorFramebuffers )
		vkDestroyFramebuffer( device, framebuffer, nullptr );

	deleteStorageImage( scene.storageImagesDepth );
	deleteStorageImage( scene.storageImagesRasterize );
	deleteStorageImage( scene.storageImagesRaytrace );
	deleteStorageImage( scene.storageImagesDLSS );
	deleteStorageImage( scene.storageImagesMotionVector );
}

void deleteModel() {
	std::function<void( objectVulkanite& )> f;

	f = [=]( objectVulkanite& obj ) {
		if( scene.primsMeshCache[obj.primMesh] ) {
			for( size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++ ) {
				if( i < obj.uniformBuffers.size() )
					vkDestroyBuffer( device, obj.uniformBuffers[i], nullptr );
				if( i < obj.uniformBuffersMemory.size() )
					vkFreeMemory( device, obj.uniformBuffersMemory[i], nullptr );
			}

			// TODO delete from the cache
			// vkDestroyBuffer(device, scene.primsMeshCache[obj.primMesh]->indexBuffer, nullptr);
			// vkFreeMemory(device, scene.primsMeshCache[obj.primMesh]->indexBufferMemory, nullptr);

			// vkDestroyBuffer(device, scene.primsMeshCache[obj.primMesh]->vertexBuffer, nullptr);
			// vkFreeMemory(device, scene.primsMeshCache[obj.primMesh]->vertexBufferMemory, nullptr);
		}
		for( const auto& objChild : obj.children )
			f( obj );
	};

	for( auto o : scene.roots ) {
		f( o );
	}
}
