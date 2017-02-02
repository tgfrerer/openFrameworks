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
	}
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

}

// ----------------------------------------------------------------------

// this method may block
::vk::Result ImgSwapchain::acquireNextImage( ::vk::Semaphore presentCompleteSemaphore, uint32_t & imageIndex ){
	//!TODO implement acquireNextImage

	// What this method does: it gets the next available (free to render into) image from the 
	// internal queue of images, and returns its index, effectively passing ownership of this
	// image to the renderer.

	// it sets the semaphore, which should signal only when present is complete - 
	// that's what the WSI does. This will be hard to simulate, as we cannot really
	// signal a semaphore from userland.

	return ::vk::Result();
}

// ----------------------------------------------------------------------

::vk::Result ImgSwapchain::queuePresent( ::vk::Queue queue, uint32_t imageIndex ){
	//!TODO: implement queue present
	return ::vk::Result();
}

// ----------------------------------------------------------------------

::vk::Result ImgSwapchain::queuePresent( ::vk::Queue queue, uint32_t imageIndex, const std::vector<::vk::Semaphore>& waitSemaphores_ ){
	//!TODO: implement queuePresent with semaphore

	// map memory, write it out.

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

const uint32_t & ImgSwapchain::getImageCount() const{
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