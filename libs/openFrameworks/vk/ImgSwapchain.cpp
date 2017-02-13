#include "vk/ImgSwapchain.h"

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

	// !TODO: use image allocator to combine and simplify allocations.

	// first, clean up previous images, if any.
	for ( auto&imgView : mImages ){
		if ( imgView.imageRef ){
			mDevice.destroyImageView( imgView.view ); imgView.view = nullptr;
			mDevice.destroyImage( imgView.imageRef ); imgView.imageRef = nullptr;
		}
	}

	mImages.resize( mImageCount );
	mImageTransferFence.resize( mImageCount );

	for ( auto & mMem : mImageMemory ){
		if ( mMem ){
			mDevice.freeMemory( mMem );
			mMem = nullptr;
		}
	}

	mImageMemory.resize( mImageCount, nullptr );


	// We will need commandbuffers to translate image from one layout to another.
	// We will also need an allocator to get memory for our images.
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

		mImages[i].imageRef = mDevice.createImage( createInfo );

		// now allocate memory
		auto memReqs = mDevice.getImageMemoryRequirements( mImages[i].imageRef );

		::vk::MemoryAllocateInfo memAllocateInfo;

		if ( false == getMemoryAllocationInfo( memReqs,
			::vk::MemoryPropertyFlags( ::vk::MemoryPropertyFlagBits::eDeviceLocal ),
			mRendererProperties.physicalDeviceMemoryProperties,
			memAllocateInfo ) ){
			ofLogFatalError() << "Image Swapchain: Could not allocate suitable memory for swapchain images.";
			assert( false );
		};

		// ----------| Invariant: Chosen memory type may be allocated 

		mImageMemory[i] = mDevice.allocateMemory( memAllocateInfo );

		mDevice.bindImageMemory( mImages[i].imageRef, mImageMemory[i], 0 );

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
			.setImage( mImages[i].imageRef )
			.setViewType( ::vk::ImageViewType::e2D )
			.setFormat( mSettings.colorFormat )
			.setSubresourceRange( subresourceRange )
			;
		mImages[i].view = mDevice.createImageView( imageViewCreateInfo );

		mImageTransferFence[i] = mDevice.createFence( { ::vk::FenceCreateFlagBits::eSignaled } );
	}

	// pre-set imageIndex so it will start at 0 with first increment.
	mImageIndex = mImageCount - 1;

}

// ----------------------------------------------------------------------

ImgSwapchain::~ImgSwapchain(){

	for ( auto & imgView : mImages ){
		if ( imgView.imageRef ){
			mDevice.destroyImageView( imgView.view ); imgView.view = nullptr;
			mDevice.destroyImage( imgView.imageRef ); imgView.imageRef = nullptr;
		}
	}
	mImages.clear();

	for ( auto & mMem : mImageMemory ){
		if ( mMem ){
			mDevice.freeMemory( mMem );
			mMem = nullptr;
		}
	}
	mImageMemory.clear();

	for ( auto & f : mImageTransferFence ){
		mDevice.destroyFence( f );
	}
	mImageTransferFence.clear();

}

// ----------------------------------------------------------------------

// this method may block
::vk::Result ImgSwapchain::acquireNextImage( ::vk::Semaphore presentCompleteSemaphore, uint32_t & imageIndex ){

	// What this method does: it gets the next available (free to render into) image from the 
	// internal queue of images, and returns its index, effectively passing ownership of this
	// image to the renderer.

	// This method must signal the semaphore `presentCompleteSemaphore` 
	// as soon as the image is free to be rendered into.

	imageIndex = ( mImageIndex + 1 ) % mImageCount;

	auto fenceWaitResult = mDevice.waitForFences( { mImageTransferFence[imageIndex] }, VK_TRUE, 100'000'000 );

	if ( fenceWaitResult != ::vk::Result::eSuccess ){
		ofLogError() << "ImgSwapchain: Waiting for fence takes too long: " << ::vk::to_string( fenceWaitResult );
	}

	// Invariant: we can assume the image has been transferred, unless it was the very first image,

	mDevice.resetFences( { mImageTransferFence[imageIndex] } );

	// TODO: we must set the presentComplete semaphore.

	return ::vk::Result();
}

// ----------------------------------------------------------------------

::vk::Result ImgSwapchain::queuePresent( ::vk::Queue queue, const std::vector<::vk::Semaphore>& waitSemaphores_ ){
	
	::vk::PipelineStageFlags wait_dst_stage_mask = ::vk::PipelineStageFlagBits::eColorAttachmentOutput;

	::vk::SubmitInfo submitInfo;
	submitInfo
		.setWaitSemaphoreCount( waitSemaphores_.size() )
		.setPWaitSemaphores( waitSemaphores_.data())      // these are the renderComplete semaphores
		.setPWaitDstStageMask( &wait_dst_stage_mask )
		.setCommandBufferCount( 0 )
		.setPCommandBuffers( nullptr )
		.setSignalSemaphoreCount( 0 )
		.setPSignalSemaphores( nullptr ) // once this has been reached, the semaphore for present complete will signal.
		;

	// TODO: 
	// We should add a command buffer which transfers the image resource from 
	// optimal to linear - using a pipeline barrier

	queue.submit( { submitInfo }, mImageTransferFence[mImageIndex]);

	return ::vk::Result();
}

// ----------------------------------------------------------------------

const std::vector<ImageWithView>& ImgSwapchain::getImages() const{
	return mImages;
}

// ----------------------------------------------------------------------

const ImageWithView & ImgSwapchain::getImage( size_t i ) const{
	return mImages[i];
}

// ----------------------------------------------------------------------

const uint32_t  ImgSwapchain::getImageCount() const{
	return mImages.size();
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