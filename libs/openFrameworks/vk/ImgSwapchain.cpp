#include "vk/ImgSwapchain.h"
#include "vk/ImageAllocator.h"
#include "vk/BufferAllocator.h"

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
	
	mImageAllocator = decltype(mImageAllocator)( new ImageAllocator( imageAllocatorSettings ), [](ImageAllocator* lhs){
		delete lhs;
		lhs = nullptr;
	} );
	
	mImageAllocator->setup();
	
	// create buffer allocator

	BufferAllocator::Settings bufferAllocatorSettings;
	bufferAllocatorSettings.physicalDeviceMemoryProperties = mRendererProperties.physicalDeviceMemoryProperties;
	bufferAllocatorSettings.physicalDeviceProperties       = mRendererProperties.physicalDeviceProperties;
	bufferAllocatorSettings.frameCount                     = mSettings.numSwapchainImages;
	bufferAllocatorSettings.device                         = mRendererProperties.device;
	bufferAllocatorSettings.memFlags                       = ::vk::MemoryPropertyFlagBits::eHostCoherent | ::vk::MemoryPropertyFlagBits::eHostVisible;
	bufferAllocatorSettings.size                           = mSettings.width * mSettings.height * 4 * mSettings.numSwapchainImages;
	bufferAllocatorSettings.bufferUsageFlags               = ::vk::BufferUsageFlagBits::eTransferDst;

	mBufferAllocator = decltype( mBufferAllocator )( new BufferAllocator( bufferAllocatorSettings ), [](BufferAllocator * lhs){
		delete lhs;
		lhs = nullptr;
	} );

	mBufferAllocator->setup();

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
			.setQueueFamilyIndexCount( 0 )
			.setPQueueFamilyIndices( nullptr )
			.setInitialLayout( ::vk::ImageLayout::eUndefined )
			;

		auto & img = mTransferFrames[i].image.imageRef = mDevice.createImage( createInfo );

		// Allocate image memory via image allocator
		{
			::vk::DeviceSize offset = 0;
			mImageAllocator->allocate( mSettings.width * mSettings.height * 4, offset );
			mDevice.bindImageMemory( img, mImageAllocator->getDeviceMemory(), offset );
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
			mBufferAllocator->allocate( mSettings.width * mSettings.height * 4, offset );
			mTransferFrames[i].bufferRegion.buffer = mBufferAllocator->getBuffer();
			mTransferFrames[i].bufferRegion.offset = offset;
			mTransferFrames[i].bufferRegion.range = mSettings.width * mSettings.height * 4;
			
			// map the host-visible ram address for the buffer to the current frame
			// so information can be read back.
			mBufferAllocator->map( mTransferFrames[i].bufferReadAddress );
			// we swap the allocator since we use one frame per id
			// and swap tells the allocator to go to the next virtual frame
			mBufferAllocator->swap(); 
			
		}

		mTransferFrames[i].frameFence = mDevice.createFence( { ::vk::FenceCreateFlagBits::eSignaled } );
	}

	// Todo: allocate host-visible memory / buffer which we can use for image readback.
	// this buffer will be 

	// pre-set imageIndex so it will start at 0 with first increment.
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

	// Invariant: we can assume the image has been transferred, unless it was the very first image,

	mDevice.resetFences( { mTransferFrames[imageIndex].frameFence } );

	/*
	
	TODO:

	The fence signal shows us that the image has been transferred to the buffer, in host-readable memory.

	We now must save the buffer to disk.

	We now must transfer the image to ram. This is done via mapping operations, and memcpy.
	once this is done, we must transfer the image back to ::vk::ImageLayout::eColorWriteOptimal
	and signal the semaphore for presentComplete. 
	
	*/


	return ::vk::Result();
}

// ----------------------------------------------------------------------

::vk::Result ImgSwapchain::queuePresent( ::vk::Queue queue, std::mutex & queueMutex, const std::vector<::vk::Semaphore>& waitSemaphores_ ){
	
	/*
	
	Submit the transfer command buffer - set waitSemaphores, so that the transfer command buffer will only execute once
	the semaphore has been signalled.

	We use the transfer fence to make sure that the command buffer has finished executing. 

	Once that's done, we issue another command buffer which will transfer the image back to its preferred layout.
	
	-
	TODO: 
	
	+ create transfer command buffers
	+ execute transfer command buffers on transfer queue
	+ make sure that ownership of the image is transferred between command buffers.

	*/

	::vk::CommandBuffer cmd;
	{
		// This command buffer must live inside a frame-protected context and must live until the fence is reached.
		// Should we use a Context?

		::vk::ImageMemoryBarrier imgMemBarrier;
		imgMemBarrier
			.setSrcAccessMask( ::vk::AccessFlagBits::eMemoryRead )
			.setDstAccessMask( ::vk::AccessFlagBits::eTransferRead )
			.setOldLayout( ::vk::ImageLayout::ePresentSrcKHR )
			.setNewLayout( ::vk::ImageLayout::eTransferSrcOptimal )
			.setSrcQueueFamilyIndex( mRendererProperties.graphicsFamilyIndex )  // < queue ownership: graphics -> transfer
			.setDstQueueFamilyIndex( mRendererProperties.transferFamilyIndex )	// < queue ownership: graphics -> transfer
			.setImage( mTransferFrames[mImageIndex].image.imageRef )
			.setSubresourceRange( { ::vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } )
			;

		// image must be translated from tiling_optimal to tiling_linear
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
			.setBufferOffset( mTransferFrames[mImageIndex].bufferRegion.offset )
			.setBufferRowLength( mSettings.width )
			.setBufferImageHeight( mSettings.height )
			.setImageSubresource( imgSubResource )
			.setImageOffset( { 0 } )
			.setImageExtent( {mSettings.width, mSettings.height, 1} )
			;

		// image must be transferred to a buffer - we can then read from this buffer.
		cmd.copyImageToBuffer( mTransferFrames[mImageIndex].image.imageRef, ::vk::ImageLayout::eTransferSrcOptimal, mTransferFrames[mImageIndex].bufferRegion.buffer, {imgCopy});

	}


	::vk::PipelineStageFlags wait_dst_stage_mask = ::vk::PipelineStageFlagBits::eColorAttachmentOutput;

	::vk::SubmitInfo submitInfo;
	submitInfo
		.setWaitSemaphoreCount( waitSemaphores_.size() )
		.setPWaitSemaphores( waitSemaphores_.data())      // these are the renderComplete semaphores
		.setPWaitDstStageMask( &wait_dst_stage_mask )
		.setCommandBufferCount( 0 )           // TODO: set transfer command buffer here.
		.setPCommandBuffers( nullptr )		  // TODO: set transfer command buffer here.
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

// const std::vector<ImageWithView>& ImgSwapchain::getImages() const{
//	return mImages;
//}

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