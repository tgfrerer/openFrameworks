#include "vk/ofVkRenderer.h"
#include "vk/Shader.h"
#include "vk/ShaderManager.h"

#include <algorithm>

// ----------------------------------------------------------------------

void ofVkRenderer::setup(){

	// the surface has been assigned by glfwwindow, through glfw,
	// just before this setup() method was called.
	querySurfaceCapabilities();
	
	// create main command pool. all renderer command buffers
	// are allocated from this command pool - which means 
	// resetting this pool resets all command buffers.
	createCommandPool();

	setupSwapChain();
	
	// sets up resources to keep track of production frames
	setupFrameResources();

	// create the main renderpass 
	setupRenderPass();

	// set up shader manager
	of::vk::ShaderManager::Settings shaderManagerSettings;
	shaderManagerSettings.device = mDevice;
	mShaderManager = make_shared<of::vk::ShaderManager>( shaderManagerSettings );

	// Set up Context
	// A Context holds dynamic frame state + manages GPU memory for "immediate" mode
	setupDefaultContext();
	
	// Mesh data prototype for DrawRectangle Method.
	// CONSIDER: move this into something more fitting
	{
		uint32_t numVerts = 4;
		mRectMesh.getVertices().resize(numVerts);
		vector<ofIndexType> indices = {0,1,3,1,2,3};
		vector<glm::vec3> norm( numVerts, { 0, 0, 1.f } );
		vector<ofFloatColor> col( numVerts, ofColor::white );
		mRectMesh.addNormals(norm);
		mRectMesh.addColors(col);
		mRectMesh.addIndices(indices);
	}

}

// ----------------------------------------------------------------------

void ofVkRenderer::setupDefaultContext(){
	

	of::vk::Context::Settings contextSettings;
	contextSettings.device             = mDevice;
	contextSettings.numVirtualFrames   = getVirtualFramesCount();
	contextSettings.shaderManager      = mShaderManager;
	contextSettings.defaultRenderPass         = mRenderPass;
	mDefaultContext = make_shared<of::vk::Context>( contextSettings );

	// shader should not reflect before 
	// they are used inside a context.

	of::vk::Shader::Settings settings{
		mShaderManager,
		{
			{ VK_SHADER_STAGE_VERTEX_BIT  , "default.vert" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "default.frag" },
		}
	};

	// shader creation makes shader reflect. 
	auto shader = std::make_shared<of::vk::Shader>( settings );
	mDefaultContext->addShader( shader );

	// this will analyse our shaders and build descriptorset
	// layouts. it will also build pipelines.
	mDefaultContext->setup( this );

}
// ----------------------------------------------------------------------

void ofVkRenderer::setupFrameResources(){
	
	mFrameResources.resize( mSettings.numVirtualFrames );
	
	for ( auto & frame : mFrameResources ){
		// allocate a command buffer

		VkCommandBufferAllocateInfo commandBufferCreateInfo {
			VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, // VkStructureType         sType;
			nullptr,                                        // const void*             pNext;
			mDrawCommandPool,                               // VkCommandPool           commandPool;
			VK_COMMAND_BUFFER_LEVEL_PRIMARY,                // VkCommandBufferLevel    level;
			1,                                              // uint32_t                commandBufferCount;
		};

		VkSemaphoreCreateInfo semaphoreCreateInfo{
			VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,        // VkStructureType           sType;
			nullptr,                                        // const void*               pNext;
			0,                                              // VkSemaphoreCreateFlags    flags;
		};

		VkFenceCreateInfo fenceCreateInfo{
			VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,            // VkStructureType       sType;
			nullptr,                                        // const void*           pNext;
			VK_FENCE_CREATE_SIGNALED_BIT,                   // VkFenceCreateFlags    flags;	   //< defines initial state of the fence as signalled.
		};

		//primary command buffer for this frame
		auto err = vkAllocateCommandBuffers( mDevice, &commandBufferCreateInfo, &frame.cmd );
		assert( !err );

		err = vkCreateSemaphore( mDevice, &semaphoreCreateInfo, nullptr, &frame.semaphoreImageAcquired );
		err = vkCreateSemaphore( mDevice, &semaphoreCreateInfo, nullptr, &frame.semaphoreRenderComplete);
		err = vkCreateFence( mDevice, &fenceCreateInfo, nullptr, &frame.fence );
	}

}

// ----------------------------------------------------------------------

void ofVkRenderer::setupSwapChain(){

	vkResetCommandPool( mDevice, mDrawCommandPool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT );
	// Allocate pre-present and post-present command buffers, 
	// from main command pool, mCommandPool.
	
	uint32_t numSwapChainFrames = mSettings.numSwapchainImages;

	VkPresentModeKHR presentMode = mSettings.swapchainType;

	// Note that the mSwapchain.setup() method will *modify* numSwapChainFrames 
	// and presentMode if it wasn't able to apply the chosen values
	// and had to resort to using fallback settings.

	mSwapchain.setup(
		mInstance,
		mDevice,
		mPhysicalDevice,
		mWindowSurface,
		mWindowColorFormat,
		mWindowWidth,
		mWindowHeight,
		numSwapChainFrames,
		presentMode
	);

	setupDepthStencil();


	mViewport = { 0.f, 0.f, float( mWindowWidth ), float( mWindowHeight ) };

}

// ----------------------------------------------------------------------

void ofVkRenderer::resizeScreen( int w, int h ){
	ofLog() << "Screen resize requested.";

	// Note: this needs to halt any multi-threaded operations
	// or wait for all of them to finish.
	
	auto err = vkDeviceWaitIdle( mDevice );
	setupSwapChain();

	assert( !err );


	mWindowWidth = w;
	mWindowHeight = h;

	// reset default context 
	setupDefaultContext();

	ofLog() << "Screen resize complete";
}
 
// ----------------------------------------------------------------------

void ofVkRenderer::querySurfaceCapabilities(){

	// we need to find out if the current physical device supports 
	// PRESENT
	
	VkBool32 presentSupported = VK_FALSE;
	vkGetPhysicalDeviceSurfaceSupportKHR( mPhysicalDevice, mVkGraphicsFamilyIndex, mWindowSurface, &presentSupported );

	// find out which color formats are supported

	// Get list of supported surface formats
	uint32_t formatCount;
	auto err = vkGetPhysicalDeviceSurfaceFormatsKHR( mPhysicalDevice, mWindowSurface, &formatCount, NULL );

	if ( err != VK_SUCCESS || formatCount == 0 ){
		ofLogError() << "Vulkan error: No valid format was found.";
		ofExit( 1 );
	}

	std::vector<VkSurfaceFormatKHR> surfaceFormats( formatCount );
	err = vkGetPhysicalDeviceSurfaceFormatsKHR( mPhysicalDevice, mWindowSurface, &formatCount, surfaceFormats.data() );
	assert( !err );

	// If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
	// there is no preferred format, so we assume VK_FORMAT_B8G8R8A8_UNORM
	if ( ( formatCount == 1 ) && ( surfaceFormats[0].format == VK_FORMAT_UNDEFINED ) ){
		mWindowColorFormat.format = VK_FORMAT_B8G8R8A8_UNORM;
	}
	else{
		// Always select the first available color format
		// If you need a specific format (e.g. SRGB) you'd need to
		// iterate over the list of available surface format and
		// check for its presence
		mWindowColorFormat.format = surfaceFormats[0].format;
	}
	mWindowColorFormat.colorSpace = surfaceFormats[0].colorSpace;

	ofLog() << "Present supported: " << ( presentSupported ? "TRUE" : "FALSE" );
}

// ----------------------------------------------------------------------

void ofVkRenderer::createCommandPool(){
	// create a command pool
	VkCommandPoolCreateInfo poolInfo
	{
		VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,      // VkStructureType                sType;
		nullptr,                                         // const void*                    pNext;
		VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT  // VkCommandPoolCreateFlags       flags
		| VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
		0,                                               // uint32_t                       queueFamilyIndex;
	};
		 
	auto err = vkCreateCommandPool( mDevice, &poolInfo, nullptr, &mDrawCommandPool );
	assert( !err );
}

// ----------------------------------------------------------------------

bool  ofVkRenderer::getMemoryAllocationInfo( const VkMemoryRequirements& memReqs, VkFlags memProps, VkMemoryAllocateInfo& memInfo ) const
{
	memInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memInfo.pNext = NULL;

	if ( !memReqs.size ){
		memInfo.allocationSize = 0;
		memInfo.memoryTypeIndex = ~0;
		return true;
	}

	// Find an available memory type that satifies the requested properties.
	uint32_t memoryTypeIndex;
	for ( memoryTypeIndex = 0; memoryTypeIndex < mPhysicalDeviceMemoryProperties.memoryTypeCount; ++memoryTypeIndex ){
		if ( ( memReqs.memoryTypeBits & ( 1 << memoryTypeIndex ) ) &&
			( mPhysicalDeviceMemoryProperties.memoryTypes[memoryTypeIndex].propertyFlags & memProps ) == memProps ){
			break;
		}
	}
	if ( memoryTypeIndex >= mPhysicalDeviceMemoryProperties.memoryTypeCount ){
		assert( 0 && "memorytypeindex not found" );
		return false;
	}

	memInfo.allocationSize = memReqs.size;
	memInfo.memoryTypeIndex = memoryTypeIndex;

	return true;
}


// ----------------------------------------------------------------------

void ofVkRenderer::setupDepthStencil(){
	
	VkImageCreateInfo imageCreateInfo {
		VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,          // VkStructureType          sType;
		nullptr,                                      // const void*              pNext;
		0,                                            // VkImageCreateFlags       flags;
		VK_IMAGE_TYPE_2D,                             // VkImageType              imageType;
		mDepthFormat,                                 // VkFormat                 format;
		{ mWindowWidth, mWindowHeight, 1 },           // VkExtent3D               extent;
		1,                                            // uint32_t                 mipLevels;
		1,                                            // uint32_t                 arrayLayers;
		VK_SAMPLE_COUNT_1_BIT,                        // VkSampleCountFlagBits    samples;
		VK_IMAGE_TILING_OPTIMAL,                      // VkImageTiling            tiling;
		VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT 
		| VK_IMAGE_USAGE_TRANSFER_SRC_BIT,            // VkImageUsageFlags        usage;
		VK_SHARING_MODE_EXCLUSIVE,                    // VkSharingMode            sharingMode;
		0,                                            // uint32_t                 queueFamilyIndexCount;
		nullptr,                                      // const uint32_t*          pQueueFamilyIndices;
		VK_IMAGE_LAYOUT_UNDEFINED,                    // VkImageLayout            initialLayout;
	};

	VkImageViewCreateInfo imageViewCreateInfo {
		VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, // VkStructureType            sType;
		nullptr,                                  // const void*                pNext;
		0,                                        // VkImageViewCreateFlags     flags;
		nullptr,                                  // VkImage                    image;
		VK_IMAGE_VIEW_TYPE_2D,                    // VkImageViewType            viewType;
		mDepthFormat,                             // VkFormat                   format;
		{},                                       // VkComponentMapping         components;
		{
			VK_IMAGE_ASPECT_DEPTH_BIT 
			| VK_IMAGE_ASPECT_STENCIL_BIT, // VkImageAspectFlags    aspectMask;
			0,                             // uint32_t              baseMipLevel;
			1,                             // uint32_t              levelCount;
			0,                             // uint32_t              baseArrayLayer;
			1,                             // uint32_t              layerCount;
		}                                         // VkImageSubresourceRange    subresourceRange;
	};

	mDepthStencil.resize(mSwapchain.getImageCount());

	for ( auto& depthStencil : mDepthStencil ){
		VkMemoryRequirements memReqs;
		if ( depthStencil.image ){
			// Destroy previously created image, if any
			vkDestroyImage( mDevice, depthStencil.image, nullptr );
			depthStencil.image = nullptr;
		}
		auto err = vkCreateImage( mDevice, &imageCreateInfo, nullptr, &depthStencil.image );
		assert( !err );
		vkGetImageMemoryRequirements( mDevice, depthStencil.image, &memReqs );

		VkMemoryAllocateInfo memInfo;
		getMemoryAllocationInfo( memReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memInfo );

		if ( depthStencil.mem ){
			// Free any previously allocated memory
			vkFreeMemory( mDevice, depthStencil.mem, nullptr );
			depthStencil.mem = nullptr;
		}
		err = vkAllocateMemory( mDevice, &memInfo, nullptr, &depthStencil.mem );
		assert( !err );

		err = vkBindImageMemory( mDevice, depthStencil.image, depthStencil.mem, 0 );
		assert( !err );

		imageViewCreateInfo.image = depthStencil.image;

		if ( depthStencil.view ){
			// Destroy any previous depthStencil ImageView
			vkDestroyImageView( mDevice, depthStencil.view, nullptr );
			depthStencil.view = nullptr;
		}
		err = vkCreateImageView( mDevice, &imageViewCreateInfo, nullptr, &depthStencil.view );
		assert( !err );
	}

}

// ----------------------------------------------------------------------

void ofVkRenderer::setupRenderPass(){
	
	VkAttachmentDescription attachments[2] = {
		{   // Color attachment
	    
			// Note that we keep initialLayout of this color attachment 
			// `VK_IMAGE_LAYOUT_UNDEFINED` -- we do this to say we effectively don't care
			// about the initial layout and contents of (swapchain) images which 
			// are attached here. See also: 
			// http://stackoverflow.com/questions/37524032/how-to-deal-with-the-layouts-of-presentable-images
			//
			// We might re-investigate this and pre-transfer images to COLOR_OPTIMAL, but only on initial use, 
			// if we wanted to be able to accumulate drawing into this buffer.
			
			0,                                                 // VkAttachmentDescriptionFlags    flags;
			mWindowColorFormat.format,                         // VkFormat                        format;
			VK_SAMPLE_COUNT_1_BIT,                             // VkSampleCountFlagBits           samples;
			VK_ATTACHMENT_LOAD_OP_CLEAR,                       // VkAttachmentLoadOp              loadOp;
			VK_ATTACHMENT_STORE_OP_STORE,                      // VkAttachmentStoreOp             storeOp;
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,                   // VkAttachmentLoadOp              stencilLoadOp;
			VK_ATTACHMENT_STORE_OP_DONT_CARE,                  // VkAttachmentStoreOp             stencilStoreOp;
			VK_IMAGE_LAYOUT_UNDEFINED,                         // VkImageLayout                   initialLayout;
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,          // VkImageLayout                   finalLayout; 
		},
		{   // Depth attachment
			0,                                                 // VkAttachmentDescriptionFlags    flags;
			mDepthFormat,                                      // VkFormat                        format;
			VK_SAMPLE_COUNT_1_BIT,                             // VkSampleCountFlagBits           samples;
			VK_ATTACHMENT_LOAD_OP_CLEAR,                       // VkAttachmentLoadOp              loadOp;
			VK_ATTACHMENT_STORE_OP_STORE,                      // VkAttachmentStoreOp             storeOp;
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,                   // VkAttachmentLoadOp              stencilLoadOp;
			VK_ATTACHMENT_STORE_OP_DONT_CARE,                  // VkAttachmentStoreOp             stencilStoreOp;
			VK_IMAGE_LAYOUT_UNDEFINED,                         // VkImageLayout                   initialLayout;
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,  // VkImageLayout                   finalLayout; 
		},
	};

	VkAttachmentReference colorReference = {
		0,                                                     // uint32_t         attachment;
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,              // VkImageLayout    layout;
	};

	VkAttachmentReference depthReference = {
		1,                                                     // uint32_t         attachment;
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,      // VkImageLayout    layout;
	};

	VkSubpassDescription subpassDescription = {
		0,                                                     // VkSubpassDescriptionFlags       flags;
		VK_PIPELINE_BIND_POINT_GRAPHICS,                       // VkPipelineBindPoint             pipelineBindPoint;
		0,                                                     // uint32_t                        inputAttachmentCount;
		nullptr,                                               // const VkAttachmentReference*    pInputAttachments;
		1,                                                     // uint32_t                        colorAttachmentCount;
		&colorReference,                                       // const VkAttachmentReference*    pColorAttachments;
		nullptr,                                               // const VkAttachmentReference*    pResolveAttachments;
		&depthReference,                                       // const VkAttachmentReference*    pDepthStencilAttachment;
		0,                                                     // uint32_t                        preserveAttachmentCount;
		nullptr,                                               // const uint32_t*                 pPreserveAttachments;
	};

	std::vector<VkSubpassDependency> dependencies = {
		{
			VK_SUBPASS_EXTERNAL,                            // uint32_t                       srcSubpass
			0,                                              // uint32_t                       dstSubpass
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,           // VkPipelineStageFlags           srcStageMask
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // VkPipelineStageFlags           dstStageMask
			VK_ACCESS_MEMORY_READ_BIT,                      // VkAccessFlags                  srcAccessMask
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,           // VkAccessFlags                  dstAccessMask
			VK_DEPENDENCY_BY_REGION_BIT                     // VkDependencyFlags              dependencyFlags
		},
		{
			0,                                              // uint32_t                       srcSubpass
			VK_SUBPASS_EXTERNAL,                            // uint32_t                       dstSubpass
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,  // VkPipelineStageFlags           srcStageMask
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,           // VkPipelineStageFlags           dstStageMask
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,           // VkAccessFlags                  srcAccessMask
			VK_ACCESS_MEMORY_READ_BIT,                      // VkAccessFlags                  dstAccessMask
			VK_DEPENDENCY_BY_REGION_BIT                     // VkDependencyFlags              dependencyFlags
		}
	};

	VkRenderPassCreateInfo renderPassInfo = {
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,             // VkStructureType                   sType;
		nullptr,                                               // const void*                       pNext;
		0,                                                     // VkRenderPassCreateFlags           flags;
		2,                                                     // uint32_t                          attachmentCount;
		attachments,                                           // const VkAttachmentDescription*    pAttachments;
		1,                                                     // uint32_t                          subpassCount;
		&subpassDescription,                                   // const VkSubpassDescription*       pSubpasses;
		static_cast<uint32_t>(dependencies.size()),            // uint32_t                          dependencyCount;
		dependencies.data(),                                   // const VkSubpassDependency*        pDependencies;
	};

	if ( mRenderPass != nullptr ){
		// Destroy any previously existing RenderPass.
		vkDestroyRenderPass( mDevice, mRenderPass, nullptr );
		mRenderPass = nullptr;
	}

	VkResult err = vkCreateRenderPass(mDevice, &renderPassInfo, nullptr, &mRenderPass);
	assert(!err);
}


// ----------------------------------------------------------------------

void ofVkRenderer::setupFrameBuffer( uint32_t swapchainImageIndex ){

	if ( nullptr != mFrameResources[mFrameIndex].framebuffer ){
	    // destroy pre-existing frame buffer
		vkDestroyFramebuffer( mDevice, mFrameResources[mFrameIndex].framebuffer, nullptr );
		mFrameResources[mFrameIndex].framebuffer = nullptr;

	}

	// This is where we connect the framebuffer with the presentable image buffer
	// which is handled by the swapchain.
	// TODO: maybe this needs to move into the swapchain.
		
	VkImageView attachments[2];
	// attachment0 shall be the image view for the image buffer to the corresponding swapchain image view
	attachments[0] = mSwapchain.getImage( swapchainImageIndex ).view;
	// attachment1 shall be the image view for the depthStencil buffer
	attachments[1] = mDepthStencil[swapchainImageIndex].view;

	VkFramebufferCreateInfo frameBufferCreateInfo = {};
	frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	frameBufferCreateInfo.pNext = NULL;
	frameBufferCreateInfo.renderPass = mRenderPass;
	frameBufferCreateInfo.attachmentCount = 2;
	frameBufferCreateInfo.pAttachments = attachments;
	frameBufferCreateInfo.width = mWindowWidth;
	frameBufferCreateInfo.height = mWindowHeight;
	frameBufferCreateInfo.layers = 1;

	// create a framebuffer for each swap chain frame
	VkResult err = vkCreateFramebuffer( mDevice, &frameBufferCreateInfo, nullptr, &mFrameResources[mFrameIndex].framebuffer );
	assert( !err );
	
}

// ----------------------------------------------------------------------

void ofVkRenderer::flushSetupCommandBuffer(VkCommandBuffer cmd){
	VkResult err;

	err = vkEndCommandBuffer( cmd );
	assert( !err );

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &cmd;

	err = vkQueueSubmit( mQueue, 1, &submitInfo, VK_NULL_HANDLE );
	assert( !err );

	err = vkQueueWaitIdle( mQueue );
	assert( !err );

	// CONSIDER: Q: Should we free the setup command buffer here?
	// A: we can "leak" it - and then just reset the pool, that should work better.
}

// ----------------------------------------------------------------------

void ofVkRenderer::startRender(){

	// start of new frame

	uint32_t swapIdx = 0; /*receives index of current swap chain image*/

	const auto &currentFrame = mFrameResources[mFrameIndex];

	// wait for current frame to finish rendering
	if ( vkWaitForFences( mDevice, 1, &currentFrame.fence, VK_FALSE, 1000000000 ) != VK_SUCCESS ){
		std::cout << "Waiting for fence takes too long!" << std::endl;
	}

	vkResetFences( mDevice, 1, &currentFrame.fence );

	// receive index for next available swapchain image
	auto err = mSwapchain.acquireNextImage( currentFrame.semaphoreImageAcquired, &swapIdx );
	assert( !err );

	setupFrameBuffer( swapIdx ); /* connect current frame buffer with swapchain image, and depth stencil image */


	if ( mDefaultContext ){
		mDefaultContext->begin( mFrameIndex );
		mDefaultContext->setUniform( "modelMatrix", ofMatrix4x4() ); // initialise modelview with identity matrix.
		mDefaultContext->setUniform( "globalColor", ofFloatColor( ofColor::white ) );
	}


	{
		VkCommandBufferBeginInfo cmdBeginInfo{
			VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, // VkStructureType                          sType;
			nullptr, // const void*                              pNext;
			VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, // VkCommandBufferUsageFlags                flags;
			nullptr // const VkCommandBufferInheritanceInfo*    pInheritanceInfo;
		};

		vkBeginCommandBuffer( currentFrame.cmd, &cmdBeginInfo );
	}
	{	// begin renderpass

		VkRect2D renderArea {
			{0,0},
			{mWindowWidth, mWindowHeight}
		};

		VkClearValue clearValues[2];
		clearValues[0].color = { 0.f, 0.f, 0.f, 0.f };
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo{
			VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, // VkStructureType        sType;
			nullptr,                                  // const void*            pNext;
			mRenderPass,                              // VkRenderPass           renderPass;
			currentFrame.framebuffer,                 // VkFramebuffer          framebuffer;
			renderArea,                               // VkRect2D               renderArea;
			2,                                        // uint32_t               clearValueCount;
			clearValues,                              // const VkClearValue*    pClearValues;
		};

		vkCmdBeginRenderPass( currentFrame.cmd, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

	}

	{	// set dynamic viewport and scissor values for renderpass
		auto & currentViewport = ofGetCurrentViewport();

		// Update dynamic viewport state
		VkViewport viewport = {};
		viewport.x = currentViewport.x;
		viewport.y = currentViewport.y;
		viewport.width = (float)currentViewport.width;
		viewport.height = (float)currentViewport.height;
		viewport.minDepth = 0.0f;		   // this is the min depth value for the depth buffer
		viewport.maxDepth = 1.0f;		   // this is the max depth value for the depth buffer
		vkCmdSetViewport( currentFrame.cmd, 0, 1, &viewport );

		// Update dynamic scissor state
		VkRect2D scissor = {};
		scissor.extent.width = viewport.width;
		scissor.extent.height = viewport.height;
		scissor.offset.x = viewport.x;
		scissor.offset.y = viewport.y;
		vkCmdSetScissor( currentFrame.cmd, 0, 1, &scissor );
	}

}


// ----------------------------------------------------------------------

void ofVkRenderer::finishRender(){
	const auto &currentFrame = mFrameResources[mFrameIndex];

	vkCmdEndRenderPass( currentFrame.cmd );

	vkEndCommandBuffer( currentFrame.cmd );

	if ( mDefaultContext ){
		// this will implicitly submit the command buffer
		mDefaultContext->end();
	}

	{	// submit command buffer 

		VkPipelineStageFlags wait_dst_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo submitInfo = {
			VK_STRUCTURE_TYPE_SUBMIT_INFO,                          // VkStructureType              sType
			nullptr,                                                // const void                  *pNext
			1,                                                      // uint32_t                     waitSemaphoreCount
			&currentFrame.semaphoreImageAcquired,                   // const VkSemaphore           *pWaitSemaphores
			&wait_dst_stage_mask,                                   // const VkPipelineStageFlags  *pWaitDstStageMask;
			1,                                                      // uint32_t                     commandBufferCount
			&currentFrame.cmd,                                      // const VkCommandBuffer       *pCommandBuffers
			1,                                                      // uint32_t                     signalSemaphoreCount
			&currentFrame.semaphoreRenderComplete                   // const VkSemaphore           *pSignalSemaphores
		};
		auto err = vkQueueSubmit( mQueue, 1, &submitInfo, currentFrame.fence );
		assert( !err );
	}

	{	// present swapchain frame
		auto err = mSwapchain.queuePresent( mQueue, mSwapchain.getCurrentImageIndex(), { currentFrame.semaphoreRenderComplete } );
		assert( !err );
	}

	// swap current frame index
	mFrameIndex = ( mFrameIndex + 1 ) % mSettings.numVirtualFrames;
}

// ----------------------------------------------------------------------

const uint32_t ofVkRenderer::getSwapChainSize(){
	return mSwapchain.getImageCount();
}

// ----------------------------------------------------------------------

const VkRenderPass & ofVkRenderer::getDefaultRenderPass(){
	return mRenderPass;
}

// ----------------------------------------------------------------------

void ofVkRenderer::setColor( const ofColor & color ){
	if ( mDefaultContext ){
		mDefaultContext->setUniform( "globalColor", ofFloatColor( color ) );
	}
	
}

// ----------------------------------------------------------------------

void ofVkRenderer::draw( const ofMesh & mesh_, ofPolyRenderMode polyMode, bool useColors, bool useTextures, bool useNormals ) const{
	
	// TODO: implement polymode and usageBools

	if ( mDefaultContext ){
		mDefaultContext->draw( mFrameResources[mFrameIndex].cmd, mesh_ );
	}

}  

// ----------------------------------------------------------------------

void ofVkRenderer::drawRectangle(float x, float y, float z, float w, float h) const{

	if (currentStyle.rectMode == OF_RECTMODE_CORNER){
		mRectMesh.getVertices()[0] = { x    , y    , z };
		mRectMesh.getVertices()[1] = { x + w, y    , z };
		mRectMesh.getVertices()[2] = { x + w, y + h, z };
		mRectMesh.getVertices()[3] = { x    , y + h, z };
	}else{
		mRectMesh.getVertices()[0] = { x - w / 2.0f, y - h / 2.0f, z };
		mRectMesh.getVertices()[1] = { x + w / 2.0f, y - h / 2.0f, z };
		mRectMesh.getVertices()[2] = { x + w / 2.0f, y + h / 2.0f, z };
		mRectMesh.getVertices()[3] = { x - w / 2.0f, y + h / 2.0f, z };
	}

	draw(mRectMesh,OF_MESH_FILL,false,false,false);

}
