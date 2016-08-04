#include "vk/ofVkRenderer.h"
#include "vk/Shader.h"
#include "vk/vkUtils.h"

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
	
	createSemaphores();

	// Set up Context
	// A Context holds dynamic frame state + manages GPU memory for "immediate" mode
	
	of::vk::Context::Settings contextSettings;
	contextSettings.device             = mDevice;
	contextSettings.numSwapchainImages = mSwapchain.getImageCount();
	contextSettings.renderPass         = mRenderPass;
	contextSettings.framebuffers       = mFrameBuffers;

	mDefaultContext = make_shared<of::vk::Context>(contextSettings);

	of::vk::Shader::Settings settings{
		mDefaultContext.get(),
		{
			{ VK_SHADER_STAGE_VERTEX_BIT  , "vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "frag.spv" },
		}
	};

	// shader creation makes shader reflect. 
	auto shader = std::make_shared<of::vk::Shader>( settings );
	mDefaultContext->addShader( shader );

	// this will analyse our shaders and build descriptorset
	// layouts. it will also build pipelines.
	mDefaultContext->setup( this );
	
	
	// Mesh data prototype for DrawRectangle Method.
	// Todo: move this into something more fitting
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

void ofVkRenderer::setupSwapChain(){

	// Allocate pre-present and post-present command buffers, 
	// from main command pool, mCommandPool.
	createCommandBuffers();

	// we need a setup command buffer to transition our image memory
	// this will allocate & initialise command buffer mSetupCommandBuffer
	createSetupCommandBuffer();


	// !TODO: move these parameters into vkRenderer::settings
	uint32_t numSwapChainFrames = 2;
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_MAILBOX_KHR;

	// Note that the mSwapchain.setup() method will *modify* numSwapChainFrames 
	// and presentMode if it wasn't able to apply the chosen values
	// and had to resort to using fallback settings.

	mSwapchain.setup(
		mInstance,
		mDevice,
		mPhysicalDevice,
		mWindowSurface,
		mWindowColorFormat,
		mSetupCommandBuffer,
		mWindowWidth,
		mWindowHeight,
		numSwapChainFrames,
		presentMode
	);

	setupDepthStencil();

	// create the main renderpass 
	setupRenderPass();

	mViewport = { 0.f, 0.f, float( mWindowWidth ), float( mWindowHeight ) };

	setupFrameBuffer();

	// submit, wait for tasks to finish, then free mSetupCommandBuffer.
	flushSetupCommandBuffer();
}

// ----------------------------------------------------------------------

void ofVkRenderer::resizeScreen( int w, int h ){
	ofLog() << "Screen resize requested.";

	// Note: this needs to halt any multi-threaded operations
	// or wait for all of them to finish.
	
	auto err = vkDeviceWaitIdle( mDevice );
	assert( !err );

	// reset command pool and all associated command buffers.
	err = vkResetCommandPool( mDevice, mCommandPool, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT );
	assert( !err );

	mWindowWidth = w;
	mWindowHeight = h;

	setupSwapChain();

	ofLog() << "Screen resize complete";
}
 
// ----------------------------------------------------------------------

void ofVkRenderer::createSemaphores(){
	
	VkSemaphoreCreateInfo semaphoreCreateInfo = {
		VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, // VkStructureType           sType;
		nullptr,                                 // const void*               pNext;
		0,                                       // VkSemaphoreCreateFlags    flags;
	};

	// This semaphore ensures that the image is complete before starting to submit again
	vkCreateSemaphore( mDevice, &semaphoreCreateInfo, nullptr, &mSemaphorePresentComplete );

	// This semaphore ensures that all commands submitted 
	// have been finished before submitting the image to the queue
	vkCreateSemaphore( mDevice, &semaphoreCreateInfo, nullptr, &mSemaphoreRenderComplete );
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
		VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,      // VkStructureType             sType;
		nullptr,                                         // const void*                 pNext;
		VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, // VkCommandPoolCreateFlags    flags;
		0,                                               // uint32_t                    queueFamilyIndex;
	};
		 
	// VkCommandPoolCreateFlags --> tells us how persistent the commands living in this pool are going to be
	auto err = vkCreateCommandPool( mDevice, &poolInfo, nullptr, &mCommandPool );
	assert( !err );

}

// ----------------------------------------------------------------------

void ofVkRenderer::createSetupCommandBuffer(){
	if ( mSetupCommandBuffer != VK_NULL_HANDLE ){
		vkFreeCommandBuffers( mDevice, mCommandPool, 1, &mSetupCommandBuffer );
		mSetupCommandBuffer = VK_NULL_HANDLE;
	}

	VkCommandBufferAllocateInfo info = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, // VkStructureType         sType;
		nullptr,                                        // const void*             pNext;
		mCommandPool,                                   // VkCommandPool           commandPool;
		VK_COMMAND_BUFFER_LEVEL_PRIMARY,                // VkCommandBufferLevel    level;
		1,                                              // uint32_t                commandBufferCount;
	};

 	// allocate one command buffer (as stated above) and store the handle to 
	// the newly allocated buffer into mSetupCommandBuffer
	auto err = vkAllocateCommandBuffers( mDevice, &info, &mSetupCommandBuffer );
	assert( !err );

	// todo : Command buffer is also started here, better put somewhere else
	// todo : Check if necessaray at all...
	VkCommandBufferBeginInfo cmdBufInfo = {};
	cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	// todo : check null handles, flags?

	err = vkBeginCommandBuffer( mSetupCommandBuffer, &cmdBufInfo );
	assert( !err);
};

// ----------------------------------------------------------------------

void ofVkRenderer::createCommandBuffers(){
	VkCommandBufferAllocateInfo allocInfo;
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = mCommandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;
	allocInfo.pNext = VK_NULL_HANDLE;

	
	// Pre present
	auto err = vkAllocateCommandBuffers( mDevice, &allocInfo, &mPrePresentCommandBuffer );
	assert( !err);
	// Post present
	err = vkAllocateCommandBuffers( mDevice, &allocInfo, &mPostPresentCommandBuffer );
	assert( !err );
};

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
	VkImageCreateInfo image = {};
	image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image.pNext = NULL;
	image.imageType = VK_IMAGE_TYPE_2D;
	image.format = mDepthFormat;
	image.extent = { mWindowWidth, mWindowHeight, 1 };
	image.mipLevels = 1;
	image.arrayLayers = 1;
	image.samples = VK_SAMPLE_COUNT_1_BIT;
	image.tiling = VK_IMAGE_TILING_OPTIMAL;
	image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	image.flags = 0;

	VkImageViewCreateInfo depthStencilView = {};
	depthStencilView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	depthStencilView.pNext = NULL;
	depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
	depthStencilView.format = mDepthFormat;
	depthStencilView.flags = 0;
	depthStencilView.subresourceRange = {};
	depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	depthStencilView.subresourceRange.baseMipLevel = 0;
	depthStencilView.subresourceRange.levelCount = 1;
	depthStencilView.subresourceRange.baseArrayLayer = 0;
	depthStencilView.subresourceRange.layerCount = 1;

	VkMemoryRequirements memReqs;
	
	if ( mDepthStencil.image){
		// Destroy previously created image, if any
		vkDestroyImage( mDevice, mDepthStencil.image, nullptr );
		mDepthStencil.image = nullptr;
	}
	auto err = vkCreateImage( mDevice, &image, nullptr, &mDepthStencil.image );
	assert( !err );
	vkGetImageMemoryRequirements( mDevice, mDepthStencil.image, &memReqs );

	VkMemoryAllocateInfo memInfo;
	getMemoryAllocationInfo( memReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memInfo );
	
	if ( mDepthStencil.mem ){
		// Free any previously allocated memory
		vkFreeMemory( mDevice, mDepthStencil.mem, nullptr );
		mDepthStencil.mem = nullptr;
	}
	err = vkAllocateMemory( mDevice, &memInfo, nullptr, &mDepthStencil.mem );
	assert( !err );

	err = vkBindImageMemory( mDevice, mDepthStencil.image, mDepthStencil.mem, 0 );
	assert( !err );

	auto transferBarrier = of::vk::createImageMemoryBarrier(
		mDepthStencil.image,
		VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL 
	);

	// Append pipeline barrier to current setup commandBuffer
	vkCmdPipelineBarrier(
		mSetupCommandBuffer,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &transferBarrier );

	depthStencilView.image = mDepthStencil.image;

	if ( mDepthStencil.view ){
		// Destroy any previous depthStencil ImageView
		vkDestroyImageView( mDevice, mDepthStencil.view, nullptr );
		mDepthStencil.view = nullptr;
	}
	err = vkCreateImageView( mDevice, &depthStencilView, nullptr, &mDepthStencil.view );
	assert( !err );
};

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
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,          // VkImageLayout                   finalLayout; 
		},
		{   // Depth attachment
			0,                                                 // VkAttachmentDescriptionFlags    flags;
			mDepthFormat,                                      // VkFormat                        format;
			VK_SAMPLE_COUNT_1_BIT,                             // VkSampleCountFlagBits           samples;
			VK_ATTACHMENT_LOAD_OP_CLEAR,                       // VkAttachmentLoadOp              loadOp;
			VK_ATTACHMENT_STORE_OP_STORE,                      // VkAttachmentStoreOp             storeOp;
			VK_ATTACHMENT_LOAD_OP_DONT_CARE,                   // VkAttachmentLoadOp              stencilLoadOp;
			VK_ATTACHMENT_STORE_OP_DONT_CARE,                  // VkAttachmentStoreOp             stencilStoreOp;
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,  // VkImageLayout                   initialLayout;
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

	VkSubpassDescription subpass = {
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

	VkRenderPassCreateInfo renderPassInfo = {
		VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,             // VkStructureType                   sType;
		nullptr,                                               // const void*                       pNext;
		0,                                                     // VkRenderPassCreateFlags           flags;
		2,                                                     // uint32_t                          attachmentCount;
		attachments,                                           // const VkAttachmentDescription*    pAttachments;
		1,                                                     // uint32_t                          subpassCount;
		&subpass,                                              // const VkSubpassDescription*       pSubpasses;
		0,                                                     // uint32_t                          dependencyCount;
		nullptr,                                               // const VkSubpassDependency*        pDependencies;
	};

	if ( mRenderPass != nullptr ){
		// Destroy any previously existing RenderPass.
		vkDestroyRenderPass( mDevice, mRenderPass, nullptr );
		mRenderPass = nullptr;
	}

	VkResult err = vkCreateRenderPass(mDevice, &renderPassInfo, nullptr, &mRenderPass);
	assert(!err);
};


// ----------------------------------------------------------------------

void ofVkRenderer::setupFrameBuffer(){

	// destroy previously exisiting FrameBuffer objects
	for ( auto& f : mFrameBuffers ){
		if ( f != nullptr ){
			vkDestroyFramebuffer( mDevice, f, nullptr );
			f = nullptr;
		}
	}

	// Create frame buffers for every swap chain frame
	mFrameBuffers.resize( mSwapchain.getImageCount() );
	for ( uint32_t i = 0; i < mFrameBuffers.size(); i++ ){
		// This is where we connect the framebuffer with the presentable image buffer
		// which is handled by the swapchain.
		// TODO: the swapchain should own this frame buffer, 
		// and allow us to reference it.
		// maybe this needs to move into the swapchain.
		
		VkImageView attachments[2];
		// attachment0 shall be the image view for the image buffer to the corresponding swapchain image view
		attachments[0] = mSwapchain.getImage(i).view;
		// attachment1 shall be the image view for the depthStencil buffer
		attachments[1] = mDepthStencil.view;

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
		VkResult err = vkCreateFramebuffer( mDevice, &frameBufferCreateInfo, nullptr, &mFrameBuffers[i] );
		assert( !err );
	}
};

// ----------------------------------------------------------------------

void ofVkRenderer::flushSetupCommandBuffer(){
	VkResult err;

	if ( mSetupCommandBuffer == VK_NULL_HANDLE )
		return;

	err = vkEndCommandBuffer( mSetupCommandBuffer );
	assert( !err );

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &mSetupCommandBuffer;

	err = vkQueueSubmit( mQueue, 1, &submitInfo, VK_NULL_HANDLE );
	assert( !err );

	err = vkQueueWaitIdle( mQueue );
	assert( !err );

	vkFreeCommandBuffers( mDevice, mCommandPool, 1, &mSetupCommandBuffer );
	mSetupCommandBuffer = VK_NULL_HANDLE; // todo : check if still necessary
};

// ----------------------------------------------------------------------

void ofVkRenderer::startRender(){

	// start of new frame
	VkResult err;

	// + block cpu until swapchain can get next image, 
	// + get index for swapchain image we may render into,
	// + signal presentComplete once the image has been acquired
	uint32_t swapIdx;

	err = mSwapchain.acquireNextImage( mSemaphorePresentComplete, &swapIdx );
	assert( !err );

	
	if ( mDefaultContext ){
		mDefaultContext->begin( swapIdx );

		mDefaultContext->setUniform( "modelMatrix", ofMatrix4x4() ); // initialise modelview with identity matrix.
		mDefaultContext->setUniform( "globalColor", ofFloatColor( ofColor::white ) );
	}


}

// ----------------------------------------------------------------------

void ofVkRenderer::submitCommandBuffer(VkCommandBuffer cmd){
	// Submit the draw command buffer
	//
	// The submit info structure contains a list of
	// command buffers and semaphores to be submitted to a queue
	// If you want to submit multiple command buffers, pass an array
	VkPipelineStageFlags pipelineStages[] = { VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT };

	size_t swapId = mSwapchain.getCurrentImageIndex();

	VkResult err = VK_SUCCESS;

	{
		VkSubmitInfo submitInfo = {
			VK_STRUCTURE_TYPE_SUBMIT_INFO,           // VkStructureType                sType;
			nullptr,                                 // const void*                    pNext;
			1,                                       // uint32_t                       waitSemaphoreCount;
			&mSemaphorePresentComplete,              // const VkSemaphore*             pWaitSemaphores;
			pipelineStages,                          // const VkPipelineStageFlags*    pWaitDstStageMask;
			1,                                       // uint32_t                       commandBufferCount;
			&cmd,                                    // const VkCommandBuffer*         pCommandBuffers;
			1,                                       // uint32_t                       signalSemaphoreCount;
			&mSemaphoreRenderComplete,               // const VkSemaphore*             pSignalSemaphores;
		};

		// Submit to the graphics queue	- 
		err = vkQueueSubmit( mQueue, 1, &submitInfo, VK_NULL_HANDLE );
		assert( !err );
	}
}



// ----------------------------------------------------------------------


// ----------------------------------------------------------------------

void ofVkRenderer::finishRender(){

	// submit current model view and projection matrices
	
	if ( mDefaultContext ){
		// this will implicitly submit the command buffer
		mDefaultContext->end();
		mDefaultContext->submit();
	}

	{  // pre-present

		/*
		
		We have to transfer the image layout of our current color attachment 
		from VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
		so that it can be handed over to the swapchain, ready for presenting. 
		
		The attachment arrives in VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL because that's 
		how our main renderpass, mRenderPass, defines it in its finalLayout parameter.
		
		*/

		VkCommandBufferBeginInfo beginInfo = {
			VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,     // VkStructureType                          sType;
			nullptr,                                         // const void*                              pNext;
			0,                                               // VkCommandBufferUsageFlags                flags;
			nullptr,                                         // const VkCommandBufferInheritanceInfo*    pInheritanceInfo;
		};
		
		auto err = vkBeginCommandBuffer( mPrePresentCommandBuffer, &beginInfo );
		assert( !err );

		{
			auto transferBarrier = of::vk::createImageMemoryBarrier(	
				mSwapchain.getImage( mSwapchain.getCurrentImageIndex() ).imageRef,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );

			// Append pipeline barrier to commandBuffer
			vkCmdPipelineBarrier(
				mPrePresentCommandBuffer,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &transferBarrier );
		}
		err = vkEndCommandBuffer( mPrePresentCommandBuffer );
		assert( !err );

		// Submit to the queue
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &mPrePresentCommandBuffer;
		

		err = vkQueueSubmit( mQueue, 1, &submitInfo, VK_NULL_HANDLE );
		assert( !err );

	}
	
	// Present the current buffer to the swap chain
	// We pass the signal semaphore from the submit info
	// to ensure that the image is not rendered until
	// all commands have been submitted
	auto err = mSwapchain.queuePresent( mQueue, mSwapchain.getCurrentImageIndex(), { mSemaphoreRenderComplete } );
	assert( !err );

	// Add a post present image memory barrier
	// This will transform the frame buffer color attachment back
	// to it's initial layout after it has been presented to the
	// windowing system
	// See buildCommandBuffers for the pre present barrier that 
	// does the opposite transformation 
	VkImageMemoryBarrier postPresentBarrier = {};
	postPresentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	postPresentBarrier.pNext = NULL;
	postPresentBarrier.srcAccessMask = 0;
	postPresentBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	postPresentBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	postPresentBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	postPresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	postPresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	postPresentBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	postPresentBarrier.image = mSwapchain.getImage( mSwapchain.getCurrentImageIndex() ).imageRef;

	// Use dedicated command buffer from example base class for submitting the post present barrier
	VkCommandBufferBeginInfo cmdBufInfo = {};
	cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	err = vkBeginCommandBuffer( mPostPresentCommandBuffer, &cmdBufInfo );
	assert( !err );

	// Put post present barrier into command buffer
	vkCmdPipelineBarrier(
		mPostPresentCommandBuffer,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &postPresentBarrier );

	err = vkEndCommandBuffer( mPostPresentCommandBuffer );
	assert( !err );

	// Submit to the queue
	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &mPostPresentCommandBuffer;

	err = vkQueueSubmit( mQueue, 1, &submitInfo, VK_NULL_HANDLE );
	assert( !err );

	err = vkQueueWaitIdle( mQueue );
	assert( !err );

}

const uint32_t ofVkRenderer::getSwapChainSize(){
	return mSwapchain.getImageCount();
}

const std::vector<VkFramebuffer>& ofVkRenderer::getDefaultFramebuffers(){
	return mFrameBuffers;
}

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
		mDefaultContext->draw( mesh_ );
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
