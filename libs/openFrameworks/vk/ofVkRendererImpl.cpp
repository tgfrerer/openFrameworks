#include "vk/ofVkRenderer.h"
#include "vk/Pipeline.h"
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

	// shaders will let us know about descriptorSetLayouts.
	of::vk::Shader::Settings settings{
		mDevice,
		{
			{ VK_SHADER_STAGE_VERTEX_BIT  , "vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "frag.spv" },
		}
	};

	auto shader = std::make_shared<of::vk::Shader>( settings );
	mShaders.emplace_back( shader );

	// Set up Context
	// A Context holds dynamic frame state + manages GPU memory for "immediate" mode
	
	of::vk::Context::Settings contextSettings;
	contextSettings.device = mDevice;
	contextSettings.numSwapchainImages = mSwapchain.getImageCount();
	contextSettings.shaders = { mShaders };
	mContext = make_shared<of::vk::Context>(contextSettings);

	mContext->setup( this );
	
	// really, shaders and pipelines and descriptors should 
	// be owned by a context - that way, a context can hold
	// any information that needs to be dealt with on a per-
	// thread basis. Effectively this could allow us to spin
	// off as many threads as we want to have contexts.
	// this would mean that each context has its own 
	// descriptor pool and memory pool - and other pools - to 
	// allocate from.

	// here we create a pipeline cache so that we can create a pipeline from it in preparePipelines
	mPipelineCache = of::vk::createPipelineCache( mDevice, "testPipelineCache.bin" );

	setupPipelines();					  
	
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

	uint32_t numSwapChainFrames = 3;
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

void ofVkRenderer::setupPipelines(){
	// !TODO: move pipelines into context

	// pipelines should, like shaders, be part of the context
	// so that the context can be encapsulated fully within its
	// own thread if it wanted so.

	// GraphicsPipelineState comes with sensible defaults
	// and is able to produce pipelines based on its current state.
	// the idea will be to have a dynamic version of this object to
	// keep track of current context state and create new pipelines
	// on the fly if needed, or, alternatively, create all pipeline
	// combinatinons upfront based on a .json file which lists each
	// state combination for required pipelines.
	of::vk::GraphicsPipelineState defaultPSO;

	// TODO: let us choose which shader we want to use with our pipeline.
	defaultPSO.mShader           = mShaders.front();
	defaultPSO.mRenderPass       = mRenderPass;

	// create pipeline layout based on vector of descriptorSetLayouts queried from mContext
	// this is way crude, and pipeline should be inside of context, context
	// should return the layout based on shader paramter (derive layout from shader bindings) 
	defaultPSO.mLayout = of::vk::createPipelineLayout( mDevice, mContext->getDescriptorSetLayoutForShader(defaultPSO.mShader));

	// TODO: fix this - this should not be part of the renderer, 
	// but of the context.
	mPipelineLayouts.emplace_back( defaultPSO.mLayout );

	mPipelines.solid = defaultPSO.createPipeline( mDevice, mPipelineCache );

	defaultPSO.mRasterizationState.polygonMode = VK_POLYGON_MODE_LINE;

	mPipelines.wireframe = defaultPSO.createPipeline( mDevice, mPipelineCache );
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

	{
		if ( mDrawCmdBuffer.size() == mSwapchain.getImageCount() ){
			// if command buffer has been previously recorded, we want to re-use it.
			err = vkResetCommandBuffer( mDrawCmdBuffer[swapIdx], 0 );
			assert( !err );

		} else{
			// allocate a draw command buffer for each swapchain image
			mDrawCmdBuffer.resize( mSwapchain.getImageCount() );
			// (re)allocate command buffer used for draw commands
			VkCommandBufferAllocateInfo allocInfo = {
				VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,                 // VkStructureType         sType;
				nullptr,                                                        // const void*             pNext;
				mCommandPool,                                                   // VkCommandPool           commandPool;
				VK_COMMAND_BUFFER_LEVEL_PRIMARY,                                // VkCommandBufferLevel    level;
			    uint32_t(mDrawCmdBuffer.size())                                 // uint32_t                commandBufferCount;
			};

			err = vkAllocateCommandBuffers( mDevice, &allocInfo, mDrawCmdBuffer.data() );
			assert( !err );

		}
	}

	mContext->begin( swapIdx );

	mContext->setUniform( "modelMatrix", ofMatrix4x4() ); // initialise modelview with identity matrix.
	mContext->setUniform( "globalColor", ofFloatColor(ofColor::white));

	beginDrawCommandBuffer( mDrawCmdBuffer[swapIdx] );

}

// ----------------------------------------------------------------------

void ofVkRenderer::beginDrawCommandBuffer(VkCommandBuffer& cmdBuf_){
	VkCommandBufferBeginInfo cmdBufInfo = {};
	cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBufInfo.pNext = NULL;

	// Set target frame buffer
	auto err = vkBeginCommandBuffer( cmdBuf_, &cmdBufInfo );
	assert( !err );


	// Update dynamic viewport state
	VkViewport viewport = {};
	viewport.width = (float)mViewport.width;
	viewport.height = (float)mViewport.height;
	viewport.minDepth = 0.0f;		   // this is the min depth value for the depth buffer
	viewport.maxDepth = 1.0f;		   // this is the max depth value for the depth buffer
	vkCmdSetViewport( cmdBuf_, 0, 1, &viewport );

	// Update dynamic scissor state
	VkRect2D scissor = {};
	scissor.extent.width = mWindowWidth;
	scissor.extent.height = mWindowHeight;
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	vkCmdSetScissor( cmdBuf_, 0, 1, &scissor );

	beginRenderPass(cmdBuf_, mFrameBuffers[mSwapchain.getCurrentImageIndex()] );
}

// ----------------------------------------------------------------------

void ofVkRenderer::beginRenderPass(VkCommandBuffer& cmdBuf_, VkFramebuffer& frameBuf_){
	VkClearValue clearValues[2];
	clearValues[0].color = mDefaultClearColor;
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRect2D renderArea{
		{ 0, 0 },								  // VkOffset2D
		{ mWindowWidth, mWindowHeight },		  // VkExtent2D
	};

	//auto currentFrameBufferId = mSwapchain.getCurrentBuffer();

	VkRenderPassBeginInfo renderPassBeginInfo = {
		VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, // VkStructureType        sType;
		nullptr,                                  // const void*            pNext;
		mRenderPass,                              // VkRenderPass           renderPass;
		frameBuf_,                                // VkFramebuffer          framebuffer;
		renderArea,                               // VkRect2D               renderArea;
		2,                                        // uint32_t               clearValueCount;
		clearValues,                              // const VkClearValue*    pClearValues;
	};

	// VK_SUBPASS_CONTENTS_INLINE means we're putting all our render commands into
	// the primary command buffer - otherwise we would have to call execute on secondary
	// command buffers to draw.
	vkCmdBeginRenderPass( cmdBuf_, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );
};



// ----------------------------------------------------------------------

void ofVkRenderer::endDrawCommandBuffer(){
	endRenderPass();
	auto err = vkEndCommandBuffer( mDrawCmdBuffer[mSwapchain.getCurrentImageIndex()] );
	assert( !err );

}

// ----------------------------------------------------------------------

void ofVkRenderer::endRenderPass(){
	vkCmdEndRenderPass( mDrawCmdBuffer[mSwapchain.getCurrentImageIndex()] );
};

// ----------------------------------------------------------------------

void ofVkRenderer::finishRender(){

	// submit current model view and projection matrices
	
	endDrawCommandBuffer();
	mContext->end();

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
			VK_STRUCTURE_TYPE_SUBMIT_INFO,                       // VkStructureType                sType;
			nullptr,                                             // const void*                    pNext;
			1,                                                   // uint32_t                       waitSemaphoreCount;
			&mSemaphorePresentComplete,                          // const VkSemaphore*             pWaitSemaphores;
			pipelineStages,                                      // const VkPipelineStageFlags*    pWaitDstStageMask;
			1,                                                   // uint32_t                       commandBufferCount;
			&mDrawCmdBuffer[swapId],                             // const VkCommandBuffer*         pCommandBuffers;
			1,                                                   // uint32_t                       signalSemaphoreCount;
			&mSemaphoreRenderComplete,                           // const VkSemaphore*             pSignalSemaphores;
		};

		// Submit to the graphics queue	- 
		err = vkQueueSubmit( mQueue, 1, &submitInfo, VK_NULL_HANDLE );
		assert( !err );
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
		
		err = vkBeginCommandBuffer( mPrePresentCommandBuffer, &beginInfo );
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
	err = mSwapchain.queuePresent( mQueue, mSwapchain.getCurrentImageIndex(), { mSemaphoreRenderComplete } );
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

// ----------------------------------------------------------------------

void ofVkRenderer::setColor( const ofColor & color ){
	mContext->setUniform( "globalColor", ofFloatColor(color) );
}

// ----------------------------------------------------------------------

void ofVkRenderer::draw( const ofMesh & mesh_, ofPolyRenderMode renderType, bool useColors, bool useTextures, bool useNormals ) const{

	// store uniforms if needed
	mContext->flushUniformBufferState();

	// as context knows which shader/pipeline is currently bound the context knows which
	// descriptorsets are currently required.
	// 
	vector<VkDescriptorSet> currentlyBoundDescriptorSets = mContext->getBoundDescriptorSets();

	// we build dynamic offsets by going over each of the currently bound descriptorSets in 
	// currentlyBoundDescriptorsets, and for each dynamic binding within these sets, we add an offset to the list.
	// we must guarantee that dynamicOffsets has the same number of elements as currentlBoundDescriptorSets has descriptors
	// the number of descriptors is calculated by summing up all descriptorCounts per binding per descriptorSet

	const auto & dynamicOffsets = mContext->getDynamicUniformBufferOffsets();

	auto & cmd = mDrawCmdBuffer[mSwapchain.getCurrentImageIndex()];

	// Bind uniforms (the first set contains the matrices)
	vkCmdBindDescriptorSets(
		cmd,
	    VK_PIPELINE_BIND_POINT_GRAPHICS,                // use graphics, not compute pipeline
	    *mPipelineLayouts[0],                           // VkPipelineLayout object used to program the bindings.
	    0, 						                        // firstset: first set index (of the above) to bind to - mDescriptorSet[0] will be bound to pipeline layout [firstset]
	    uint32_t(currentlyBoundDescriptorSets.size()),  // setCount: how many sets to bind
	    currentlyBoundDescriptorSets.data(),            // the descriptor sets to match up with our mPipelineLayout (need to be compatible)
	    uint32_t(dynamicOffsets.size()),                // dynamic offsets count how many dynamic offsets
	    dynamicOffsets.data()                           // dynamic offsets for each
	);

	// Bind the rendering pipeline (including the shaders)
	vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelines.solid );

	std::vector<VkDeviceSize> vertexOffsets;
	std::vector<VkDeviceSize> indexOffsets;

	// Store vertex data using Context.
	// - this uses Allocator to store mesh data in the current frame' s dynamic memory
	// Context will return memory offsets into vertices, indices, based on current context memory buffer
	// 
	// TODO: check if it made sense to cache already stored meshes, 
	//       so that meshes which have already been stored this frame 
	//       may be re-used.
	mContext->storeMesh( mesh_, vertexOffsets, indexOffsets);

	// TODO: cull vertexOffsets which refer to empty vertex attribute data
	//       make sure that a pipeline with the correct bindings is bound to match the 
	//       presence or non-presence of mesh data.

	// Bind vertex data buffers to current pipeline. 
	// The vector indices into bufferRefs, vertexOffsets correspond to [binding numbers] of the currently bound pipeline.
	// See Shader.h for an explanation of how this is mapped to shader attribute locations
	vector<VkBuffer> bufferRefs( vertexOffsets.size(), mContext->getVkBuffer() );
	vkCmdBindVertexBuffers( cmd, 0, uint32_t(bufferRefs.size()), bufferRefs.data(), vertexOffsets.data() );

	if ( indexOffsets.empty() ){
		// non-indexed draw
		vkCmdDraw( cmd, uint32_t(mesh_.getNumVertices()), 1, 0, 1 );
	} else{
		// indexed draw
		vkCmdBindIndexBuffer( cmd, bufferRefs[0], indexOffsets[0], VK_INDEX_TYPE_UINT32 );
		vkCmdDrawIndexed( cmd, uint32_t(mesh_.getNumIndices()), 1, 0, 0, 1 );
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
