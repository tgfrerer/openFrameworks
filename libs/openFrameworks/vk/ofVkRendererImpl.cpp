#include "vk/ofVkRenderer.h"
#include "vk/Shader.h"
#include "vk/RenderBatch.h"

#include <algorithm>
#include <array>

// ----------------------------------------------------------------------

void ofVkRenderer::setup(){

	mSwapchain->setRendererProperties( mRendererProperties );
	setupSwapChain();

	mViewport = { 0.f, 0.f, float( mSwapchain->getWidth() ), float( mSwapchain->getHeight()) };


	// sets up resources to keep track of production frames
	setupDefaultContext();

	if ( !mDefaultRenderPass){
		mDefaultRenderPass = generateDefaultRenderPass( mSwapchain->getColorFormat(), mDepthFormat );
	}

}

// ----------------------------------------------------------------------

void ofVkRenderer::setupStagingContext(){

	of::vk::Context::Settings settings;

	settings.transientMemoryAllocatorSettings.device = mDevice;
	settings.transientMemoryAllocatorSettings.frameCount = mSettings.numVirtualFrames;
	settings.transientMemoryAllocatorSettings.physicalDeviceMemoryProperties = mPhysicalDeviceMemoryProperties;
	settings.transientMemoryAllocatorSettings.physicalDeviceProperties = mPhysicalDeviceProperties;
	settings.transientMemoryAllocatorSettings.size = ( ( 1ULL << 24 ) * mSettings.numVirtualFrames );
	settings.renderer = this;
	settings.pipelineCache = nullptr;
	settings.renderToSwapChain = false;

	mStagingContext = make_shared<of::vk::Context>( std::move( settings ) );
	mStagingContext->setup();
}

// ----------------------------------------------------------------------

void ofVkRenderer::setupDefaultContext(){

	of::vk::Context::Settings settings;

	settings.transientMemoryAllocatorSettings.device                         = mDevice;
	settings.transientMemoryAllocatorSettings.frameCount                     = mSettings.numVirtualFrames ;
	settings.transientMemoryAllocatorSettings.physicalDeviceMemoryProperties = mPhysicalDeviceMemoryProperties ;
	settings.transientMemoryAllocatorSettings.physicalDeviceProperties       = mPhysicalDeviceProperties ;
	settings.transientMemoryAllocatorSettings.size                           = ( ( 1ULL << 24 ) * mSettings.numVirtualFrames );
	settings.renderer = this;
	settings.pipelineCache = getPipelineCache();
	settings.renderToSwapChain = true;

	mDefaultContext = make_shared<of::vk::Context>(std::move(settings));
	mDefaultContext->setup();
}

// ----------------------------------------------------------------------


void ofVkRenderer::setupSwapChain(){

	// This method is called on initialisation, and 
	// every time the window is resized, as a resize
	// means render image targets have to be re-created.

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

	ofLogVerbose() << "Screen resize complete";
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
	
	mDepthStencil.reset();

	mDepthStencil = decltype( mDepthStencil )( new DepthStencilResource, [device = mDevice]( DepthStencilResource* depthStencil ){
	
		// custom deleter for depthStencil.

		if ( depthStencil->image ){
			// Destroy previously created image, if any
			device.destroyImage( depthStencil->image );
			depthStencil->image = nullptr;
		}
		if ( depthStencil->mem ){
			// Free any previously allocated memory
			device.freeMemory( depthStencil->mem );
			depthStencil->mem = nullptr;
		}
		if ( depthStencil->view ){
			// Destroy any previous depthStencil ImageView
			device.destroyImageView( depthStencil->view );
			depthStencil->view = nullptr;
		}
		delete ( depthStencil );
	} );

	{
		vk::MemoryRequirements memReqs;
				
		mDepthStencil->image = mDevice.createImage( imgCreateInfo );
		
		memReqs = mDevice.getImageMemoryRequirements( mDepthStencil->image );

		vk::MemoryAllocateInfo memInfo;
		of::vk::getMemoryAllocationInfo( memReqs,
			vk::MemoryPropertyFlags( vk::MemoryPropertyFlagBits::eDeviceLocal ),
			mPhysicalDeviceMemoryProperties,
			memInfo );

		mDepthStencil->mem = mDevice.allocateMemory( memInfo );
		mDevice.bindImageMemory( mDepthStencil->image, mDepthStencil->mem, 0 );

		// now attach the newly minted image to the image view
		imgViewCreateInfo.setImage( mDepthStencil->image );

		mDepthStencil->view = mDevice.createImageView( imgViewCreateInfo, nullptr );

	}

}

// ----------------------------------------------------------------------

std::shared_ptr<::vk::RenderPass> ofVkRenderer::generateDefaultRenderPass(::vk::Format colorFormat_, ::vk::Format depthFormat_) const {

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
		.setFormat          ( colorFormat_ )
		.setSamples         ( vk::SampleCountFlagBits::e1 )
		.setLoadOp          ( vk::AttachmentLoadOp::eClear )
		.setStoreOp         ( vk::AttachmentStoreOp::eStore )
		.setStencilLoadOp   ( vk::AttachmentLoadOp::eDontCare )
		.setStencilStoreOp  ( vk::AttachmentStoreOp::eDontCare )
		.setInitialLayout   ( vk::ImageLayout::eUndefined )
		.setFinalLayout     ( vk::ImageLayout::ePresentSrcKHR )
		;
	attachments[1]		//depth stencil attachment
		.setFormat          ( depthFormat_ )
		.setSamples         ( vk::SampleCountFlagBits::e1 )
		.setLoadOp          ( vk::AttachmentLoadOp::eClear )
		.setStoreOp         ( vk::AttachmentStoreOp::eStore)
		.setStencilLoadOp   ( vk::AttachmentLoadOp::eDontCare )
		.setStencilStoreOp  ( vk::AttachmentStoreOp::eDontCare )
		.setInitialLayout   ( vk::ImageLayout::eUndefined )
		.setFinalLayout     ( vk::ImageLayout::eDepthStencilAttachmentOptimal )
		;

	// Define 2 attachments, and tell us what layout to expect these to be in.
	// Index references attachments from above.

	vk::AttachmentReference colorReference{ 0, vk::ImageLayout::eColorAttachmentOptimal };
	vk::AttachmentReference depthReference{ 1, vk::ImageLayout::eDepthStencilAttachmentOptimal};

	vk::SubpassDescription subpassDescription;
	subpassDescription
		.setPipelineBindPoint       ( vk::PipelineBindPoint::eGraphics )
		.setColorAttachmentCount    ( 1 )
		.setPColorAttachments       ( &colorReference )
		.setPDepthStencilAttachment ( &depthReference )
		;

	// Define 2 self-dependencies for subpass 0

	std::array<vk::SubpassDependency, 2> dependencies;
	dependencies[0]
		.setSrcSubpass      ( VK_SUBPASS_EXTERNAL ) // producer
		.setDstSubpass      ( 0 )                   // consumer
		.setSrcStageMask    ( vk::PipelineStageFlagBits::eBottomOfPipe )
		.setDstStageMask    ( vk::PipelineStageFlagBits::eColorAttachmentOutput )
		.setSrcAccessMask   ( vk::AccessFlagBits::eMemoryRead )
		.setDstAccessMask   ( vk::AccessFlagBits::eColorAttachmentWrite )
		.setDependencyFlags ( vk::DependencyFlagBits::eByRegion )
		;
	dependencies[1]
		.setSrcSubpass      ( 0 )                                     // producer (last possible subpass)
		.setDstSubpass      ( VK_SUBPASS_EXTERNAL )                   // consumer
		.setSrcStageMask    ( vk::PipelineStageFlagBits::eColorAttachmentOutput )
		.setDstStageMask    ( vk::PipelineStageFlagBits::eBottomOfPipe )
		.setSrcAccessMask   ( vk::AccessFlagBits::eColorAttachmentWrite )
		.setDstAccessMask   ( vk::AccessFlagBits::eMemoryRead )
		.setDependencyFlags ( vk::DependencyFlagBits::eByRegion )
		;
	
	// Define 1 renderpass with 1 subpass

	vk::RenderPassCreateInfo renderPassCreateInfo;
	renderPassCreateInfo
		.setAttachmentCount ( attachments.size() )
		.setPAttachments    ( attachments.data() )
		.setSubpassCount    ( 1 )
		.setPSubpasses      ( &subpassDescription )
		.setDependencyCount ( dependencies.size() )
		.setPDependencies   ( dependencies.data() );

	std::shared_ptr<::vk::RenderPass> renderPassPtr = 
		std::shared_ptr<::vk::RenderPass>( new ::vk::RenderPass(mDevice.createRenderPass( renderPassCreateInfo )), 
			[device=mDevice] (::vk::RenderPass * lhs){
		device.destroyRenderPass( *lhs );
		delete lhs;
	} );

	return renderPassPtr;
}

// ----------------------------------------------------------------------

void ofVkRenderer::startRender(){

	// start of new frame
	mStagingContext->begin();
	mDefaultContext->begin();

	// ----------| invariant: last frame has finished rendering. 

	uint32_t swapIdx = 0; /*receives index of current swap chain image*/

	// Receive index for next available swapchain image.
	// Effectively, ownership of the image is transferred from the swapchain to the context.
	//
	// Ownership is transferred async, only once semaphorePresentComplete was signalled.
	// The swapchain will signal the semaphore as soon as the image is ready to be written into.
	//
	// This means, a queue submission from the context that draws into the image 
	// must wait for this semaphore.
	auto err = mSwapchain->acquireNextImage( mDefaultContext->getSemaphoreWait(), swapIdx );

	// ---------| invariant: new swap chain image has been acquired for drawing into.

	// connect default context frame buffer to swapchain image, and depth stencil image
	mDefaultContext->setSwapchainImageView( mSwapchain->getImage( swapIdx ).view );

}

// ----------------------------------------------------------------------

void ofVkRenderer::finishRender(){

	// TODO: if there are other Contexts flying around on other threads, 
	// ask them to finish their work for the frame.
	mStagingContext->end();
	mDefaultContext->end();
	
	// present swapchain frame
	mSwapchain->queuePresent( mQueues[0], mQueueMutex[0], { mDefaultContext->getSemaphoreSignalOnComplete()} );
	
}

// ----------------------------------------------------------------------

void ofVkRenderer::submit( size_t queueIndex, ::vk::ArrayProxy<const ::vk::SubmitInfo>&& submits,const ::vk::Fence& fence ){
	std::lock_guard<std::mutex> lock{ mQueueMutex[queueIndex] };
	mQueues[queueIndex].submit( submits, fence );
}

// ----------------------------------------------------------------------

const uint32_t ofVkRenderer::getSwapChainSize(){
	return mSwapchain->getImageCount();
}
