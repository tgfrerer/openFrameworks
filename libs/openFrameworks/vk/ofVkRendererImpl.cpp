#include "vk/ofVkRenderer.h"
#include "vk/Shader.h"
#include "vk/ShaderManager.h"

#include <algorithm>
#include <array>

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
	contextSettings.defaultRenderPass  = mRenderPass;
	mDefaultContext = make_shared<of::vk::Context>( contextSettings );

	// shader should not reflect before 
	// they are used inside a context.

	of::vk::Shader::Settings settings{
		mShaderManager,
		{
			{ ::vk::ShaderStageFlagBits::eVertex  , "default.vert" },
			{ ::vk::ShaderStageFlagBits::eFragment, "default.frag" },
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
void ofVkRenderer::resetDefaultContext(){
	if ( mDefaultContext ){
		mDefaultContext->reset();
	}
}
// ----------------------------------------------------------------------

void ofVkRenderer::setupFrameResources(){
	
	mFrameResources.resize( mSettings.numVirtualFrames );
	
	for ( auto & frame : mFrameResources ){
		// allocate a command buffer

		vk::CommandBufferAllocateInfo commandBufferAllocInfo{ mDrawCommandPool };
		commandBufferAllocInfo
			.setCommandBufferCount( 1 );
		
		//primary command buffer for this frame

		auto commandBuffers = mDevice.allocateCommandBuffers( commandBufferAllocInfo );
		
		if ( !commandBuffers.empty() ){
			frame.cmd = commandBuffers.front();
		}

		frame.semaphoreImageAcquired  = mDevice.createSemaphore( {} );
		frame.semaphoreRenderComplete = mDevice.createSemaphore( {} );

		frame.fence = mDevice.createFence( { vk::FenceCreateFlags( vk::FenceCreateFlagBits::eSignaled ) } );
		
	}

}

// ----------------------------------------------------------------------

void ofVkRenderer::setupSwapChain(){

	mDevice.resetCommandPool( mDrawCommandPool, vk::CommandPoolResetFlagBits::eReleaseResources );
	
	// Allocate pre-present and post-present command buffers, 
	// from main command pool, mCommandPool.
	
	uint32_t numSwapChainFrames = mSettings.numSwapchainImages;

	vk::PresentModeKHR presentMode = mSettings.swapchainType;

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
	ofLogVerbose() << "Screen resize requested.";

	// Note: this needs to halt any multi-threaded operations
	// or wait for all of them to finish.
	
	auto err = vkDeviceWaitIdle( mDevice );
	assert( !err );

	setupSwapChain();

	mWindowWidth = w;
	mWindowHeight = h;

	ofLogVerbose() << "Screen resize complete";
}
 
// ----------------------------------------------------------------------

void ofVkRenderer::querySurfaceCapabilities(){

	// we need to find out if the current physical device supports 
	// PRESENT
	
	VkBool32 presentSupported = VK_FALSE;
	vkGetPhysicalDeviceSurfaceSupportKHR( mPhysicalDevice, mVkGraphicsFamilyIndex, mWindowSurface, &presentSupported );

	// find out which color formats are supported

	// Get list of supported surface formats
	std::vector<vk::SurfaceFormatKHR> surfaceFormats = mPhysicalDevice.getSurfaceFormatsKHR( mWindowSurface );

	// If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
	// there is no preferred format, so we assume VK_FORMAT_B8G8R8A8_UNORM
	if ( ( surfaceFormats.size() == 1 ) && ( surfaceFormats[0].format == vk::Format::eUndefined) ){
		mWindowColorFormat.format = vk::Format::eB8G8R8A8Unorm;
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
	vk::CommandPoolCreateInfo poolInfo;
	poolInfo
		.setQueueFamilyIndex( 0 )
		.setFlags( vk::CommandPoolCreateFlags( vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient ) ) ;
	mDrawCommandPool = mDevice.createCommandPool( poolInfo );
}

// ----------------------------------------------------------------------

bool  ofVkRenderer::getMemoryAllocationInfo( const ::vk::MemoryRequirements& memReqs, ::vk::MemoryPropertyFlags memProps, ::vk::MemoryAllocateInfo& memInfo ) const
{
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
	

	vk::ImageCreateInfo imgCreateInfo;

	imgCreateInfo
		.setImageType( vk::ImageType::e2D )
		.setFormat( mDepthFormat )
		.setExtent( { mWindowWidth,mWindowHeight,1 } )
		.setMipLevels( 1 )
		.setArrayLayers( 1 )
		.setSamples( vk::SampleCountFlagBits::e1 )
		.setTiling( vk::ImageTiling::eOptimal )
		.setUsage( vk::ImageUsageFlags( vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferSrc ) )
		.setSharingMode( vk::SharingMode::eExclusive )
		.setQueueFamilyIndexCount( 0 )
		.setInitialLayout( vk::ImageLayout::eUndefined );

	vk::ImageViewCreateInfo imgViewCreateInfo;

	vk::ImageSubresourceRange subresourceRange;
	
	subresourceRange.setAspectMask( vk::ImageAspectFlags( vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil ) )
		.setBaseMipLevel( 0 )
		.setLevelCount( 1 )
		.setBaseArrayLayer( 0 )
		.setLayerCount( 1 );

	imgViewCreateInfo
		.setViewType( vk::ImageViewType::e2D )
		.setFormat( mDepthFormat )
		.setSubresourceRange( subresourceRange );
	
	mDepthStencil.resize(mSwapchain.getImageCount());

	for ( auto& depthStencil : mDepthStencil ){
		vk::MemoryRequirements memReqs;
		if ( depthStencil.image ){
			// Destroy previously created image, if any
			mDevice.destroyImage( depthStencil.image );
			depthStencil.image = nullptr;
		}
		
		depthStencil.image = mDevice.createImage( imgCreateInfo );
		
		memReqs = mDevice.getImageMemoryRequirements( depthStencil.image );

		vk::MemoryAllocateInfo memInfo;
		getMemoryAllocationInfo( memReqs, vk::MemoryPropertyFlags( vk::MemoryPropertyFlagBits::eDeviceLocal ), memInfo );

		if ( depthStencil.mem ){
			// Free any previously allocated memory
			mDevice.freeMemory( depthStencil.mem );
			depthStencil.mem = nullptr;
		}

		depthStencil.mem = mDevice.allocateMemory( memInfo );
		mDevice.bindImageMemory( depthStencil.image, depthStencil.mem, 0 );

		// now attach the newly minted image to the image view
		imgViewCreateInfo.setImage( depthStencil.image );

		if ( depthStencil.view ){
			// Destroy any previous depthStencil ImageView
			mDevice.destroyImageView( depthStencil.view );
			depthStencil.view = nullptr;
		}

		depthStencil.view = mDevice.createImageView( imgViewCreateInfo, nullptr );

	}

}

// ----------------------------------------------------------------------

void ofVkRenderer::setupRenderPass(){
	

	// Note that we keep initialLayout of the color attachment eUndefined ==
	// `VK_IMAGE_LAYOUT_UNDEFINED` -- we do this to say we effectively don't care
	// about the initial layout and contents of (swapchain) images which 
	// are attached here. See also: 
	// http://stackoverflow.com/questions/37524032/how-to-deal-with-the-layouts-of-presentable-images
	//
	// We might re-investigate this and pre-transfer images to COLOR_OPTIMAL, but only on initial use, 
	// if we wanted to be able to accumulate drawing into this buffer.

	std::array<vk::AttachmentDescription, 2> attachments;
	
	attachments[0]		// color attachment
		.setFormat          ( mWindowColorFormat.format )
		.setSamples         ( vk::SampleCountFlagBits::e1 )
		.setLoadOp          ( vk::AttachmentLoadOp::eClear )
		.setStoreOp         ( vk::AttachmentStoreOp::eStore )
		.setStencilLoadOp   ( vk::AttachmentLoadOp::eDontCare )
		.setStencilStoreOp  ( vk::AttachmentStoreOp::eDontCare )
		.setInitialLayout   ( vk::ImageLayout::eUndefined )
		.setFinalLayout     ( vk::ImageLayout::ePresentSrcKHR )
		;
	attachments[1]		//depth stencil attachment
		.setFormat          ( mDepthFormat )
		.setSamples         ( vk::SampleCountFlagBits::e1 )
		.setLoadOp          ( vk::AttachmentLoadOp::eClear )
		.setStoreOp         ( vk::AttachmentStoreOp::eStore )
		.setStencilLoadOp   ( vk::AttachmentLoadOp::eDontCare )
		.setStencilStoreOp  ( vk::AttachmentStoreOp::eDontCare )
		.setInitialLayout   ( vk::ImageLayout::eUndefined )
		.setFinalLayout     ( vk::ImageLayout::eDepthStencilAttachmentOptimal )
		;

	vk::AttachmentReference colorReference{ 0, vk::ImageLayout::eColorAttachmentOptimal };
	vk::AttachmentReference depthReference{ 1, vk::ImageLayout::eDepthStencilAttachmentOptimal};

	vk::SubpassDescription subpassDescription;
	subpassDescription
		.setPipelineBindPoint       ( vk::PipelineBindPoint::eGraphics )
		.setColorAttachmentCount    ( 1 )
		.setPColorAttachments       ( &colorReference )
		.setPDepthStencilAttachment ( &depthReference )
		;

	std::array<vk::SubpassDependency, 2> dependencies;
	dependencies[0]
		.setSrcSubpass      ( VK_SUBPASS_EXTERNAL )
		.setDstSubpass      ( 0 )
		.setSrcStageMask    ( vk::PipelineStageFlagBits::eBottomOfPipe )
		.setDstStageMask    ( vk::PipelineStageFlagBits::eColorAttachmentOutput )
		.setSrcAccessMask   ( vk::AccessFlagBits::eMemoryRead )
		.setDstAccessMask   ( vk::AccessFlagBits::eColorAttachmentWrite )
		.setDependencyFlags ( vk::DependencyFlagBits::eByRegion )
		;
	dependencies[1]
		.setSrcSubpass      ( VK_SUBPASS_EXTERNAL )
		.setDstSubpass      ( 0 )
		.setSrcStageMask    ( vk::PipelineStageFlagBits::eColorAttachmentOutput )
		.setDstStageMask    ( vk::PipelineStageFlagBits::eBottomOfPipe )
		.setSrcAccessMask   ( vk::AccessFlagBits::eColorAttachmentWrite )
		.setDstAccessMask   ( vk::AccessFlagBits::eMemoryRead )
		.setDependencyFlags ( vk::DependencyFlagBits::eByRegion )
		;
	
	vk::RenderPassCreateInfo renderPassInfo;
	renderPassInfo
		.setAttachmentCount ( attachments.size() )
		.setPAttachments    ( attachments.data() )
		.setSubpassCount    ( 1 )
		.setPSubpasses      ( &subpassDescription )
		.setDependencyCount ( dependencies.size() )
		.setPDependencies   ( dependencies.data() );

	if ( mRenderPass ){
		// Destroy any previously existing RenderPass.
		mDevice.destroyRenderPass( mRenderPass );
		mRenderPass = nullptr;
	}

	mRenderPass = mDevice.createRenderPass( renderPassInfo );
}


// ----------------------------------------------------------------------

void ofVkRenderer::setupFrameBuffer( uint32_t swapchainImageIndex ){

	if ( mFrameResources[mFrameIndex].framebuffer ){
	    // destroy pre-existing frame buffer
		mDevice.destroyFramebuffer( mFrameResources[mFrameIndex].framebuffer );
		mFrameResources[mFrameIndex].framebuffer = nullptr;
	}

	// This is where we connect the framebuffer with the presentable image buffer
	// which is handled by the swapchain.
		
	std::array<vk::ImageView,2> attachments;
	// attachment0 shall be the image view for the image buffer to the corresponding swapchain image view
	attachments[0] = mSwapchain.getImage( swapchainImageIndex ).view;
	// attachment1 shall be the image view for the depthStencil buffer
	attachments[1] = mDepthStencil[swapchainImageIndex].view;

	vk::FramebufferCreateInfo frameBufferCreateInfo;
	frameBufferCreateInfo
		.setRenderPass( mRenderPass )
		.setAttachmentCount( 2 )
		.setPAttachments( attachments.data() )
		.setWidth( mWindowWidth )
		.setHeight( mWindowHeight )
		.setLayers( 1 )
		;

	// create a framebuffer for each swap chain frame
	mFrameResources[mFrameIndex].framebuffer = mDevice.createFramebuffer( frameBufferCreateInfo );
}

// ----------------------------------------------------------------------

void ofVkRenderer::startRender(){

	// start of new frame

	uint32_t swapIdx = 0; /*receives index of current swap chain image*/

	const auto &currentFrame = mFrameResources[mFrameIndex];

	auto fenceWaitResult = mDevice.waitForFences( { currentFrame.fence }, VK_TRUE, 100'000'000 );

	if ( fenceWaitResult != vk::Result::eSuccess ){
		ofLog() << "Waiting for fence takes too long: " << vk::to_string( fenceWaitResult );
	}

	mDevice.resetFences( { currentFrame.fence } );
	//vkResetFences( mDevice, 1, &currentFrame.fence );

	// receive index for next available swapchain image
	auto err = mSwapchain.acquireNextImage( currentFrame.semaphoreImageAcquired, &swapIdx );

	setupFrameBuffer( swapIdx ); /* connect current frame buffer with swapchain image, and depth stencil image */

	if ( mDefaultContext ){
		mDefaultContext->begin( mFrameIndex );
		mDefaultContext->setUniform( "modelMatrix", ofMatrix4x4() ); // initialise modelview with identity matrix.
		mDefaultContext->setUniform( "globalColor", ofFloatColor( ofColor::white ) );
	}

	// begin command buffer
	currentFrame.cmd.begin( { vk::CommandBufferUsageFlagBits::eOneTimeSubmit } );

	{	

		vk::Rect2D renderArea{
			{0,0},
			{mWindowWidth, mWindowHeight}
		};

		std::array<vk::ClearValue, 2> clearValues;
		clearValues[0].setColor(  reinterpret_cast<const vk::ClearColorValue&>(ofFloatColor::black) );
		clearValues[1].setDepthStencil( { 1.f, 0 } );

		vk::RenderPassBeginInfo renderPassBeginInfo;
		
		renderPassBeginInfo
			.setRenderPass( mRenderPass )
			.setFramebuffer( currentFrame.framebuffer )
			.setRenderArea( renderArea )
			.setClearValueCount( 2 )
			.setPClearValues( clearValues.data() );
		
		// begin renderpass inside command buffer
		currentFrame.cmd.beginRenderPass( renderPassBeginInfo, vk::SubpassContents::eInline );

	}

	{	
		// set dynamic viewport and scissor values for renderpass
		const auto & currentViewport = ofGetCurrentViewport();

		// Update dynamic viewport state
		vk::Viewport viewport{ currentViewport.x, currentViewport.y, currentViewport.width, currentViewport.height, 0.f, 1.f };
		currentFrame.cmd.setViewport( 0, { viewport } );
		
		// Update dynamic scissor state
		vk::Rect2D scissor;
		scissor.extent.width = viewport.width;
		scissor.extent.height = viewport.height;
		scissor.offset.x = viewport.x;
		scissor.offset.y = viewport.y;
		currentFrame.cmd.setScissor( 0, { scissor } );

	}

}


// ----------------------------------------------------------------------

void ofVkRenderer::finishRender(){
	const auto &currentFrame = mFrameResources[mFrameIndex];

	currentFrame.cmd.endRenderPass();
	currentFrame.cmd.end();

	if ( mDefaultContext ){
		// currently, this is a noop, but it might come in handy for performance measurements
		mDefaultContext->end();
	}

	{	// submit command buffer 

		vk::PipelineStageFlags wait_dst_stage_mask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
		vk::SubmitInfo submitInfo;

		submitInfo
			.setWaitSemaphoreCount   ( 1 )
			.setPWaitSemaphores      ( &currentFrame.semaphoreImageAcquired )
			.setPWaitDstStageMask    ( &wait_dst_stage_mask )
			.setCommandBufferCount   ( 1 )
			.setPCommandBuffers      ( &currentFrame.cmd)
			.setSignalSemaphoreCount ( 1)
			.setPSignalSemaphores    ( &currentFrame.semaphoreRenderComplete)
			;

		mQueue.submit( { submitInfo }, currentFrame.fence );
	}

	// present swapchain frame
	mSwapchain.queuePresent( mQueue, mSwapchain.getCurrentImageIndex(), { currentFrame.semaphoreRenderComplete } );
	
	// swap current frame index
	mFrameIndex = ( mFrameIndex + 1 ) % mSettings.numVirtualFrames;
}

// ----------------------------------------------------------------------

const uint32_t ofVkRenderer::getSwapChainSize(){
	return mSwapchain.getImageCount();
}

// ----------------------------------------------------------------------

const ::vk::RenderPass & ofVkRenderer::getDefaultRenderPass(){
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
