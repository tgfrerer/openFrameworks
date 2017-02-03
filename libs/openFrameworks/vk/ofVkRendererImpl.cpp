#include "vk/ofVkRenderer.h"
#include "vk/Shader.h"
#include "vk/RenderBatch.h"

#include <algorithm>
#include <array>

// ----------------------------------------------------------------------

void ofVkRenderer::setup(){

	createSetupCommandPool();

	mSwapchain->setRendererProperties( mRendererProperties );
	setupSwapChain();

	mViewport = { 0.f, 0.f, float( mSwapchain->getWidth() ), float( mSwapchain->getHeight()) };

	mPipelineCache = of::vk::createPipelineCache( mDevice, "pipelineCache.bin" );

	// sets up resources to keep track of production frames
	setupDefaultContext();

}

// ----------------------------------------------------------------------

void ofVkRenderer::setupDefaultContext(){
	
	of::vk::RenderContext::Settings settings;
	
	settings.transientMemoryAllocatorSettings.device = mDevice;
	settings.transientMemoryAllocatorSettings.frameCount =  mSettings.numVirtualFrames ;
	settings.transientMemoryAllocatorSettings.physicalDeviceMemoryProperties = mPhysicalDeviceMemoryProperties ;
	settings.transientMemoryAllocatorSettings.physicalDeviceProperties = mPhysicalDeviceProperties ;
	settings.transientMemoryAllocatorSettings.size = ( ( 1ULL << 24 ) * mSettings.numVirtualFrames );
	settings.renderer = this;
	settings.pipelineCache = getPipelineCache();
	settings.renderArea = { 0,0, mSwapchain->getWidth(), mSwapchain->getHeight()};
	settings.renderPass = generateDefaultRenderPass();
	
	mDefaultContext = make_shared<of::vk::RenderContext>(std::move(settings));
	mDefaultContext->setup();
}

// ----------------------------------------------------------------------

void ofVkRenderer::setupSwapChain(){
	
	mDevice.resetCommandPool( mSetupCommandPool, vk::CommandPoolResetFlagBits::eReleaseResources );
	
	// Allocate pre-present and post-present command buffers, 
	// from main command pool, mCommandPool.
	mSwapchain->setup( );

	setupDepthStencil();
}

// ----------------------------------------------------------------------


void ofVkRenderer::resizeScreen( int w, int h ){
	ofLogVerbose() << "Screen resize requested.";

	// Note: this needs to halt any multi-threaded operations
	// or wait for all of them to finish.
	
	auto err = vkDeviceWaitIdle( mDevice );
	assert( !err );
	
	mSwapchain->changeExtent( w, h );
	setupSwapChain();
	
	mViewport.setWidth( mSwapchain->getWidth() );
	mViewport.setHeight( mSwapchain->getHeight() );

	if ( mDefaultContext ){
		mDefaultContext->setRenderArea( { { 0, 0 }, { mSwapchain->getWidth() ,  mSwapchain->getHeight() } } );
	}

	ofLogVerbose() << "Screen resize complete";
}
 


// ----------------------------------------------------------------------

void ofVkRenderer::createSetupCommandPool(){
	// create a command pool
	vk::CommandPoolCreateInfo poolInfo;
	poolInfo
		.setQueueFamilyIndex( 0 )
		.setFlags( vk::CommandPoolCreateFlags( vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient ) ) ;

	mSetupCommandPool = mDevice.createCommandPool( poolInfo );
}

// ----------------------------------------------------------------------

const std::shared_ptr<::vk::PipelineCache>& ofVkRenderer::getPipelineCache(){
	if ( mPipelineCache.get() == nullptr ){
		mPipelineCache = of::vk::createPipelineCache( mDevice, "pipelineCache.bin" );
		ofLog() << "Created default pipeline cache";
	}
	return mPipelineCache;
}


// ----------------------------------------------------------------------

void ofVkRenderer::setupDepthStencil(){

	vk::ImageCreateInfo imgCreateInfo;

	imgCreateInfo
		.setImageType( vk::ImageType::e2D )
		.setFormat( mDepthFormat )
		.setExtent( { mSwapchain->getWidth(), mSwapchain->getHeight(), 1 } )
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
	
	subresourceRange
		.setAspectMask( vk::ImageAspectFlags( vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil ) )
		.setBaseMipLevel( 0 )
		.setLevelCount( 1 )
		.setBaseArrayLayer( 0 )
		.setLayerCount( 1 );

	imgViewCreateInfo
		.setViewType( vk::ImageViewType::e2D )
		.setFormat( mDepthFormat )
		.setSubresourceRange( subresourceRange );
	
	mDepthStencil.resize(mSwapchain->getImageCount());

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
		of::vk::getMemoryAllocationInfo( memReqs,
			vk::MemoryPropertyFlags( vk::MemoryPropertyFlagBits::eDeviceLocal ),
			mPhysicalDeviceMemoryProperties,
			memInfo );

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

::vk::RenderPass ofVkRenderer::generateDefaultRenderPass() const {

	::vk::RenderPass result = nullptr;

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
		.setFormat          ( mSwapchain->getColorFormat() )
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
		.setStoreOp         ( vk::AttachmentStoreOp::eStore)
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
		.setSrcAccessMask   ( vk::AccessFlagBits::eMemoryRead )
		.setDstStageMask    ( vk::PipelineStageFlagBits::eColorAttachmentOutput )
		.setDstAccessMask   ( vk::AccessFlagBits::eColorAttachmentWrite )
		.setDependencyFlags ( vk::DependencyFlagBits::eByRegion )
		;
	dependencies[1]
		.setSrcSubpass      ( VK_SUBPASS_EXTERNAL )
		.setDstSubpass      ( 0 )
		.setSrcStageMask    ( vk::PipelineStageFlagBits::eColorAttachmentOutput )
		.setSrcAccessMask   ( vk::AccessFlagBits::eColorAttachmentWrite )
		.setDstStageMask    ( vk::PipelineStageFlagBits::eBottomOfPipe )
		.setDstAccessMask   ( vk::AccessFlagBits::eMemoryRead )
		.setDependencyFlags ( vk::DependencyFlagBits::eByRegion )
		;
	
	vk::RenderPassCreateInfo renderPassCreateInfo;
	renderPassCreateInfo
		.setAttachmentCount ( attachments.size() )
		.setPAttachments    ( attachments.data() )
		.setSubpassCount    ( 1 )
		.setPSubpasses      ( &subpassDescription )
		.setDependencyCount ( dependencies.size() )
		.setPDependencies   ( dependencies.data() );

	result = mDevice.createRenderPass( renderPassCreateInfo );

	return result;
}

// ----------------------------------------------------------------------

void ofVkRenderer::attachSwapChainImages( uint32_t swapchainImageIndex ){
	
	// Connect the framebuffer with the presentable image buffer
	// which is handled by the swapchain.

	std::vector<vk::ImageView> attachments(2, nullptr);
	
	// Attachment0 is the image view for the image buffer to the corresponding swapchain image view
	attachments[0] = mSwapchain->getImage( swapchainImageIndex ).view;
	
	// Attachment1 is the image view for the depthStencil buffer
	attachments[1] = mDepthStencil[swapchainImageIndex].view;

	mDefaultContext->setupFrameBufferAttachments(attachments);

}

// ----------------------------------------------------------------------

void ofVkRenderer::startRender(){

	// start of new frame

	uint32_t swapIdx = 0; /*receives index of current swap chain image*/

	//----------| invariant: last frame has finished rendering. It may not yet be finished presenting.

	// !TODO: notify any contexts in a thread-safe way that the last frame has finished rendering.
	// allContexts.renderComplete();
	// This means they may dispose of any transient resources for that frame, and start building new command buffers.
	// maybe the way to do this is through a condition_variable
	mDefaultContext->begin();

	// receive index for next available swapchain image
	// effectively, this means the renderer is taking ownership of the image away from the swapchain.
	auto err = mSwapchain->acquireNextImage( mDefaultContext->getSemaphorePresentComplete(), swapIdx );

	// ---------| invariant: new swap chain image has been acquired for drawing into.

	/* connect default context frame buffer to swapchain image, and depth stencil image */
	attachSwapChainImages( swapIdx ); 

}

// ----------------------------------------------------------------------

void ofVkRenderer::finishRender(){

	// TODO: if there are other Contexts flying around on other threads, 
	// ask them to finish their work for the frame.

	mDefaultContext->submitToQueue();

	// present swapchain frame
	mSwapchain->queuePresent( mQueue, mSwapchain->getCurrentImageIndex(), { mDefaultContext->getSemaphoreRenderComplete()} );
	
	// swap current frame index inside context
	mDefaultContext->swap();
}

// ----------------------------------------------------------------------

const uint32_t ofVkRenderer::getSwapChainSize(){
	return mSwapchain->getImageCount();
}
