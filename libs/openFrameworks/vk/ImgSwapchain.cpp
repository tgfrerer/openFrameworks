#include "vk/ImgSwapchain.h"
#include "vk/ImageAllocator.h"
#include "vk/BufferAllocator.h"
#include "vk/ofVkRenderer.h"
#include "ofImage.h"
#include "ofPixels.h"

using namespace std;
using namespace of::vk;

// ----------------------------------------------------------------------

ImgSwapchain::ImgSwapchain( const ImgSwapchainSettings & settings_ )
	: mSettings( settings_ ){}

// ----------------------------------------------------------------------

void ImgSwapchain::setRendererProperties( const of::vk::RendererProperties & rendererProperties_ ){
	mRendererProperties = rendererProperties_;
}

// ----------------------------------------------------------------------

void ImgSwapchain::setup(){

	// create image allocator

	ImageAllocator::Settings imageAllocatorSettings;
	imageAllocatorSettings.imageUsageFlags                = ::vk::ImageUsageFlagBits::eColorAttachment | ::vk::ImageUsageFlagBits::eTransferSrc;
	imageAllocatorSettings.imageTiling                    = ::vk::ImageTiling::eOptimal;
	imageAllocatorSettings.physicalDeviceMemoryProperties = mRendererProperties.physicalDeviceMemoryProperties;
	imageAllocatorSettings.physicalDeviceProperties       = mRendererProperties.physicalDeviceProperties;
	imageAllocatorSettings.device                         = mRendererProperties.device;
	imageAllocatorSettings.memFlags                       = ::vk::MemoryPropertyFlagBits::eDeviceLocal;
	// We add one buffer Image granularity per frame for good measure, 
	// so we can be sure we won't run out of memory because our images need padding
	imageAllocatorSettings.size                           = ( mSettings.width * mSettings.height * 4 + mRendererProperties.physicalDeviceProperties.limits.bufferImageGranularity ) 
	                                                        * mSettings.numSwapchainImages;
	
	mImageAllocator.setup(imageAllocatorSettings);
	
	// create buffer allocator

	BufferAllocator::Settings bufferAllocatorSettings;
	bufferAllocatorSettings.physicalDeviceMemoryProperties = mRendererProperties.physicalDeviceMemoryProperties;
	bufferAllocatorSettings.physicalDeviceProperties       = mRendererProperties.physicalDeviceProperties;
	bufferAllocatorSettings.frameCount                     = mSettings.numSwapchainImages;
	bufferAllocatorSettings.device                         = mRendererProperties.device;
	bufferAllocatorSettings.memFlags                       = ::vk::MemoryPropertyFlagBits::eHostVisible | ::vk::MemoryPropertyFlagBits::eHostCoherent;
	bufferAllocatorSettings.size                           = mSettings.width * mSettings.height * 4 * mSettings.numSwapchainImages;
	bufferAllocatorSettings.bufferUsageFlags               = ::vk::BufferUsageFlagBits::eTransferDst | ::vk::BufferUsageFlagBits::eTransferSrc;

	mBufferAllocator.setup(bufferAllocatorSettings);

	// Create command pool for internal command buffers.
	{
		::vk::CommandPoolCreateInfo createInfo;
		createInfo
			.setFlags( ::vk::CommandPoolCreateFlagBits::eResetCommandBuffer )
			.setQueueFamilyIndex( mRendererProperties.graphicsFamilyIndex ) //< Todo: make sure this has been set properly when renderer/queues were set up.
			;
		mCommandPool = mDevice.createCommandPool( createInfo );
	}

	// Create transfer frames.
	mTransferFrames.resize( mImageCount );

	for ( size_t i = 0; i != mImageCount; ++i ){

		::vk::ImageCreateInfo createInfo;
		createInfo
			.setImageType( ::vk::ImageType::e2D )
			.setFormat( mSettings.colorFormat )
			.setExtent( { mSettings.width, mSettings.height, 1 } )
			.setMipLevels( 1 )
			.setArrayLayers( 1 )
			.setSamples( ::vk::SampleCountFlagBits::e1 )
			.setTiling( ::vk::ImageTiling::eOptimal )
			.setUsage( ::vk::ImageUsageFlagBits::eColorAttachment | ::vk::ImageUsageFlagBits::eTransferSrc )
			.setSharingMode( ::vk::SharingMode::eExclusive )
			.setQueueFamilyIndexCount( 1 )
			.setPQueueFamilyIndices( &mRendererProperties.graphicsFamilyIndex )
			.setInitialLayout( ::vk::ImageLayout::eUndefined )
			;

		auto & img = mTransferFrames[i].image.imageRef = mDevice.createImage( createInfo );

		// Allocate image memory via image allocator
		{
			::vk::DeviceSize offset = 0;
			mImageAllocator.allocate( mSettings.width * mSettings.height * 4, offset );
			mDevice.bindImageMemory( img, mImageAllocator.getDeviceMemory(), offset );
		}

		::vk::ImageSubresourceRange subresourceRange;
		subresourceRange
			.setAspectMask( ::vk::ImageAspectFlags( ::vk::ImageAspectFlagBits::eColor ) )
			.setBaseMipLevel( 0 )
			.setLevelCount( 1 )
			.setBaseArrayLayer( 0 )
			.setLayerCount( 1 )
			;

		::vk::ImageViewCreateInfo imageViewCreateInfo;
		imageViewCreateInfo
			.setImage( img )
			.setViewType( ::vk::ImageViewType::e2D )
			.setFormat( mSettings.colorFormat )
			.setSubresourceRange( subresourceRange )
			;
		
		mTransferFrames[i].image.view = mDevice.createImageView( imageViewCreateInfo );

		// allocate host-visible buffer memory, and map buffer memory
		{
			::vk::DeviceSize offset = 0;
			mBufferAllocator.allocate( mSettings.width * mSettings.height * 4, offset );
			mTransferFrames[i].bufferRegion.buffer = mBufferAllocator.getBuffer();
			mTransferFrames[i].bufferRegion.offset = offset;
			mTransferFrames[i].bufferRegion.range  = mSettings.width * mSettings.height * 4;
			
			// map the host-visible ram address for the buffer to the current frame
			// so information can be read back.
			mBufferAllocator.map( mTransferFrames[i].bufferReadAddress );
			// we swap the allocator since we use one frame per id
			// and swap tells the allocator to go to the next virtual frame
			mBufferAllocator.swap(); 
		}

		mTransferFrames[i].frameFence = mDevice.createFence( { ::vk::FenceCreateFlagBits::eSignaled } );

	}

	{
		::vk::CommandBufferAllocateInfo allocateInfo;
		allocateInfo
			.setCommandPool( mCommandPool )
			.setLevel( ::vk::CommandBufferLevel::ePrimary)
			.setCommandBufferCount( mTransferFrames.size() * 2 )
			;

		auto cmdBuffers = mDevice.allocateCommandBuffers( allocateInfo );
		
		// Todo: fill in commands.

		for ( size_t i = 0; i != mTransferFrames.size(); ++i ){
			mTransferFrames[i].cmdAcquire = cmdBuffers[i*2];
			mTransferFrames[i].cmdPresent = cmdBuffers[i*2+1];
		}
	}

	for (size_t i = 0; i!=mTransferFrames.size(); ++i ){
		{
			// copy == transfer image to buffer memory
			::vk::CommandBuffer & cmd = mTransferFrames[i].cmdPresent;

			cmd.begin( { ::vk::CommandBufferUsageFlags() } );

			::vk::ImageMemoryBarrier imgMemBarrier;
			imgMemBarrier
				.setSrcAccessMask( ::vk::AccessFlagBits::eMemoryRead )
				.setDstAccessMask( ::vk::AccessFlagBits::eTransferRead )
				.setOldLayout( ::vk::ImageLayout::ePresentSrcKHR )
				.setNewLayout( ::vk::ImageLayout::eTransferSrcOptimal )
				.setSrcQueueFamilyIndex( mRendererProperties.graphicsFamilyIndex )  // < TODO: queue ownership: graphics -> transfer
				.setDstQueueFamilyIndex( mRendererProperties.graphicsFamilyIndex )	// < TODO: queue ownership: graphics -> transfer
				.setImage( mTransferFrames[i].image.imageRef )
				.setSubresourceRange( { ::vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } )
				;

			cmd.pipelineBarrier( ::vk::PipelineStageFlagBits::eAllCommands, ::vk::PipelineStageFlagBits::eTransfer, ::vk::DependencyFlags(), {}, {}, { imgMemBarrier } );

			::vk::ImageSubresourceLayers imgSubResource;
			imgSubResource
				.setAspectMask( ::vk::ImageAspectFlagBits::eColor )
				.setMipLevel( 0 )
				.setBaseArrayLayer( 0 )
				.setLayerCount( 1 )
				;

			::vk::BufferImageCopy imgCopy;
			imgCopy
				.setBufferOffset( mTransferFrames[i].bufferRegion.offset )
				.setBufferRowLength( mSettings.width )
				.setBufferImageHeight( mSettings.height )
				.setImageSubresource( imgSubResource )
				.setImageOffset( { 0 } )
				.setImageExtent( { mSettings.width, mSettings.height, 1 } )
				;

			// image must be transferred to a buffer - we can then read from this buffer.
			cmd.copyImageToBuffer( mTransferFrames[i].image.imageRef, ::vk::ImageLayout::eTransferSrcOptimal, mTransferFrames[i].bufferRegion.buffer, { imgCopy } );
			cmd.end();
		}

		{	
			// Move ownership of image back from transfer -> graphics
			// Change image layout back to colorattachment

			::vk::CommandBuffer & cmd = mTransferFrames[i].cmdAcquire;

			cmd.begin( {::vk::CommandBufferUsageFlags()} );

			::vk::ImageMemoryBarrier imgMemBarrier;
			imgMemBarrier
				.setSrcAccessMask( ::vk::AccessFlagBits::eTransferRead )
				.setDstAccessMask( ::vk::AccessFlagBits::eColorAttachmentWrite )
				.setOldLayout( ::vk::ImageLayout::eUndefined )
				.setNewLayout( ::vk::ImageLayout::eColorAttachmentOptimal )
				.setSrcQueueFamilyIndex( mRendererProperties.graphicsFamilyIndex )  // < TODO: queue ownership: transfer -> graphics
				.setDstQueueFamilyIndex( mRendererProperties.graphicsFamilyIndex )  // < TODO: queue ownership: transfer -> graphics
				.setImage( mTransferFrames[i].image.imageRef )
				.setSubresourceRange( { ::vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } )
				;

			cmd.pipelineBarrier( ::vk::PipelineStageFlagBits::eAllCommands, ::vk::PipelineStageFlagBits::eTransfer, ::vk::DependencyFlags(), {}, {}, { imgMemBarrier } );
			
			cmd.end();

		}

	}
	

	// Pre-set imageIndex so it will start at 0 with first increment.
	mImageIndex = mImageCount - 1;

}

// ----------------------------------------------------------------------

ImgSwapchain::~ImgSwapchain(){

	for ( auto & f : mTransferFrames ){
		if ( f.image.imageRef){
			mDevice.destroyImageView( f.image.view ); 
			mDevice.destroyImage( f.image.imageRef ); 
		}
		if ( f.frameFence ){
			mDevice.destroyFence( f.frameFence );
		}
	}

	mDevice.destroyCommandPool( mCommandPool );

	mTransferFrames.clear();

}

// ----------------------------------------------------------------------

// immediately return index of next available index, 
// defer signalling of semaphore until this image is ready for write
::vk::Result ImgSwapchain::acquireNextImage( ::vk::Semaphore presentCompleteSemaphore, uint32_t & imageIndex ){

	// What this method does: it gets the next available (free to render into) image from the 
	// internal queue of images, and returns its index, effectively passing ownership of this
	// image to the renderer.

	// This method must signal the semaphore `presentCompleteSemaphore` 
	// as soon as the image is free to be rendered into.

	imageIndex = ( mImageIndex + 1 ) % mImageCount;

	auto fenceWaitResult = mDevice.waitForFences( { mTransferFrames[imageIndex].frameFence }, VK_TRUE, 100'000'000 );

	if ( fenceWaitResult != ::vk::Result::eSuccess ){
		ofLogError() << "ImgSwapchain: Waiting for fence takes too long: " << ::vk::to_string( fenceWaitResult );
	}

	mDevice.resetFences( { mTransferFrames[imageIndex].frameFence } );
	
	mImageIndex = imageIndex;


	static ofPixels    mPixels;

	if ( false ){
		// memcpy into pixels object
		if ( !mPixels.isAllocated() ){
			mPixels.allocate( mSettings.width, mSettings.height, ofImageType::OF_IMAGE_COLOR_ALPHA );
			ofLogNotice() << "Image Swapchain: Allocating pixels.";
		}
		memcpy( mPixels.getData(), mTransferFrames[imageIndex].bufferReadAddress, mPixels.size() );
	} else {
		// Directly use ofPixels object to wrap memory
		mPixels.setFromExternalPixels( reinterpret_cast<unsigned char*>( mTransferFrames[imageIndex].bufferReadAddress ),
			size_t( mSettings.width ), size_t( mSettings.height ), ofPixelFormat::OF_PIXELS_RGBA );
	}

	// we could use an event here to synchronise host <-> device, meaning 
	// a command buffer on the device would wait execution until the event signalling that the
	// copy operation has completed was signalled by the host.

	std::array<char, 15> numStr;
	sprintf( numStr.data(), "%08zd.png", mImageCounter );
	std::string filename( numStr.data(), numStr.size() );

	// Invariant: we can assume the image has been transferred into the mapped buffer.
	// Now we must write the memory from the mapped buffer to the hard drive.
	ofSaveImage( mPixels, ( mSettings.path + filename ), ofImageQualityType::OF_IMAGE_QUALITY_BEST );

	++mImageCounter;

	// The number of array elements must correspond to the number of wait semaphores, as each 
	// mask specifies what the semaphore is waiting for.
	std::array<::vk::PipelineStageFlags, 1> wait_dst_stage_mask = { ::vk::PipelineStageFlagBits::eTransfer };

	::vk::SubmitInfo submitInfo;
	submitInfo
		.setWaitSemaphoreCount( 0 )
		.setPWaitSemaphores( nullptr )
		.setPWaitDstStageMask( nullptr )
		.setCommandBufferCount( 1 )
		.setPCommandBuffers( &mTransferFrames[imageIndex].cmdAcquire )
		.setSignalSemaphoreCount( 1 )
		.setPSignalSemaphores( &presentCompleteSemaphore )
		;

	// !TODO: instead of submitting to queue 0, this needs to go to the transfer queue.
	mSettings.renderer->submit( 0, { submitInfo }, nullptr );

	return ::vk::Result();
}

// ----------------------------------------------------------------------

::vk::Result ImgSwapchain::queuePresent( ::vk::Queue queue, std::mutex & queueMutex, const std::vector<::vk::Semaphore>& waitSemaphores_ ){
	
	/*
	
	Submit the transfer command buffer - set waitSemaphores, so that the transfer command buffer will only execute once
	the semaphore has been signalled.

	We use the transfer fence to make sure that the command buffer has finished executing. 

	Once that's done, we issue another command buffer which will transfer the image back to its preferred layout.

	*/

	::vk::PipelineStageFlags wait_dst_stage_mask = ::vk::PipelineStageFlagBits::eColorAttachmentOutput;

	::vk::SubmitInfo submitInfo;
	submitInfo
		.setWaitSemaphoreCount( waitSemaphores_.size() )
		.setPWaitSemaphores( waitSemaphores_.data())      // these are the renderComplete semaphores
		.setPWaitDstStageMask( &wait_dst_stage_mask )
		.setCommandBufferCount( 1 )                                           // TODO: set transfer command buffer here.
		.setPCommandBuffers( &mTransferFrames[mImageIndex].cmdPresent )		  // TODO: set transfer command buffer here.
		.setSignalSemaphoreCount( 0 )
		.setPSignalSemaphores( nullptr ) // once this has been reached, the semaphore for present complete will signal.
		;

	// Todo: submit to transfer queue, not main queue, if possible

	{
		std::lock_guard<std::mutex> lock{ queueMutex };
		queue.submit( { submitInfo }, mTransferFrames[mImageIndex].frameFence );
	}

	return ::vk::Result();
}

// ----------------------------------------------------------------------

const ImageWithView & ImgSwapchain::getImage( size_t i ) const{
	return mTransferFrames[i].image;
}

// ----------------------------------------------------------------------

const uint32_t  ImgSwapchain::getImageCount() const{
	return mTransferFrames.size();
}

// ----------------------------------------------------------------------

const uint32_t & ImgSwapchain::getCurrentImageIndex() const{
	return mImageIndex;
}

// ----------------------------------------------------------------------

const ::vk::Format & ImgSwapchain::getColorFormat(){
	return mSettings.colorFormat;
}

// ----------------------------------------------------------------------
uint32_t ImgSwapchain::getWidth(){
	return mSettings.width;
}

// ----------------------------------------------------------------------
uint32_t ImgSwapchain::getHeight(){
	return mSettings.height;
}

// ----------------------------------------------------------------------
void ImgSwapchain::changeExtent( uint32_t w, uint32_t h ){
	const_cast<uint32_t&>( mSettings.width ) = w;
	const_cast<uint32_t&>( mSettings.height ) = h;
}

// ----------------------------------------------------------------------