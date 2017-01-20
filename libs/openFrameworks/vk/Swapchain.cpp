#include "vk/Swapchain.h"
#include <vector>
#include "ofLog.h"
#include "GLFW/glfw3.h"

using namespace of::vk;


of::vk::Swapchain::~Swapchain(){
	// it's imperative we clean up.

	for ( auto&b : mImages ){
		// note that we only destroy the VkImageView,
		// as the VkImage is owned by the swapchain mSwapchain
		// and will get destroyed when destroying the swapchain
		mDevice.destroyImageView( b.view );
	}
	mImages.clear();

	mDevice.destroySwapchainKHR( mVkSwapchain );

}

void Swapchain::setup()
{

	::vk::Result err = ::vk::Result::eSuccess;

	// The surface in SwapchainSettings::windowSurface has been assigned by glfwwindow, through glfw,
	// just before this setup() method was called.
	querySurfaceCapabilities();

	::vk::SwapchainKHR oldSwapchain = mVkSwapchain;

	// Get physical device surface properties and formats
	const ::vk::SurfaceCapabilitiesKHR & surfCaps = mSurfaceProperties.capabilities;

	// get available present modes for physical device
	const std::vector<::vk::PresentModeKHR>& presentModes = mSurfaceProperties.presentmodes;


	// either set or get the swapchain surface extents
	::vk::Extent2D swapchainExtent = {};

	if ( surfCaps.currentExtent.width == -1 ){
		swapchainExtent.width = mSettings.width;
		swapchainExtent.height = mSettings.height;
	} else {
		// set dimensions from surface extents if surface extents are available
		swapchainExtent = surfCaps.currentExtent;
		const_cast<uint32_t&>(mSettings.width)  = surfCaps.currentExtent.width;
		const_cast<uint32_t&>(mSettings.height) = surfCaps.currentExtent.height;
	}

	// Prefer user-selected present mode, 
	// use guaranteed fallback mode (FIFO) if preferred mode couldn't be found.
	::vk::PresentModeKHR swapchainPresentMode = ::vk::PresentModeKHR::eFifo;

	bool presentModeSwitchSuccessful = false;
	for ( auto & p : presentModes ){
		if ( p == mSettings.presentMode ){
			swapchainPresentMode = p;
			presentModeSwitchSuccessful = true;
			break;
		}
	}

	if (!presentModeSwitchSuccessful){
		ofLogWarning() << "Could not switch to selected Swapchain Present Mode. Falling back to FIFO...";
	}

	// write current present mode back to reference from parameter
	// so caller can find out whether chosen present mode has been 
	// applied successfully.
	const_cast<::vk::PresentModeKHR&>(mSettings.presentMode) = swapchainPresentMode;

	uint32_t desiredNumberOfSwapchainImages = std::max<uint32_t>( surfCaps.minImageCount, mSettings.numSwapChainFrames );
	if ( ( surfCaps.maxImageCount > 0 ) && ( desiredNumberOfSwapchainImages > surfCaps.maxImageCount ) ){
		desiredNumberOfSwapchainImages = surfCaps.maxImageCount;
	}

	// write current value back to parameter reference so caller
	// has a chance to check if values were applied correctly.
	const_cast<uint32_t&>(mSettings.numSwapChainFrames) = desiredNumberOfSwapchainImages;

	::vk::SurfaceTransformFlagBitsKHR preTransform;
	// Note: this will be interesting for mobile devices
	// - if rotation and mirroring for the final output can 
	// be defined here.

	if ( surfCaps.supportedTransforms & ::vk::SurfaceTransformFlagBitsKHR::eIdentity){
		preTransform = ::vk::SurfaceTransformFlagBitsKHR::eIdentity;
	} else{
		preTransform = surfCaps.currentTransform;
	}

	::vk::SwapchainCreateInfoKHR swapChainCreateInfo;

	swapChainCreateInfo
		.setSurface           ( mSettings.windowSurface )
		.setMinImageCount     ( desiredNumberOfSwapchainImages)
		.setImageFormat       ( mWindowColorFormat.format)
		.setImageColorSpace   ( mWindowColorFormat.colorSpace)
		.setImageExtent       ( swapchainExtent )
		.setImageArrayLayers  ( 1 )
		.setImageUsage        ( ::vk::ImageUsageFlagBits::eColorAttachment )
		.setImageSharingMode  ( ::vk::SharingMode::eExclusive )
		.setPreTransform      ( preTransform  )
		.setCompositeAlpha    ( ::vk::CompositeAlphaFlagBitsKHR::eOpaque )
		.setPresentMode       ( mSettings.presentMode )
		.setClipped           ( VK_TRUE )
		.setOldSwapchain      ( oldSwapchain )
		;

	mVkSwapchain = mDevice.createSwapchainKHR( swapChainCreateInfo );

	// If an existing swap chain is re-created, destroy the old swap chain
	// This also cleans up all the presentable images
	if ( oldSwapchain ){
		mDevice.destroySwapchainKHR( oldSwapchain );
		oldSwapchain = nullptr;
	}

	std::vector<::vk::Image> swapchainImages = mDevice.getSwapchainImagesKHR( mVkSwapchain );
	mImageCount = swapchainImages.size();


	for ( auto&b : mImages ){
		// If there were any images available at all to iterate over, this means
		// that the swapchain was re-created. 
		// This happens on window resize, for example.
		// Therefore we have to destroy old ImageView object(s).
		mDevice.destroyImageView( b.view );
	}

	mImages.resize( mImageCount );

	for ( uint32_t i = 0; i < mImageCount; i++ ){
		
		mImages[i].imageRef = swapchainImages[i];
		
		::vk::ComponentMapping componentMapping { 
			::vk::ComponentSwizzle::eR,
			::vk::ComponentSwizzle::eG,
			::vk::ComponentSwizzle::eB,
			::vk::ComponentSwizzle::eA
		};
	
		::vk::ImageSubresourceRange subresourceRange;
		subresourceRange
			.setAspectMask       ( ::vk::ImageAspectFlagBits::eColor )
			.setBaseMipLevel     ( 0 )
			.setLevelCount       ( 1 )
			.setBaseArrayLayer   ( 0 )
			.setLayerCount       ( 1 )
			;

		::vk::ImageViewCreateInfo imageViewCreateInfo;
		imageViewCreateInfo
			.setImage            ( mImages[i].imageRef )
			.setViewType         ( ::vk::ImageViewType::e2D )
			.setFormat           ( mWindowColorFormat.format )
			.setComponents       ( componentMapping )
			.setSubresourceRange ( subresourceRange )
			;
		// create image view for color image
		mImages[i].view = mDevice.createImageView( imageViewCreateInfo );
		
	}

}


// ----------------------------------------------------------------------

// Acquires the next image in the swap chain
// blocks cpu until image has been acquired
// signals semaphorePresentComplete once image has been acquired
vk::Result Swapchain::acquireNextImage( ::vk::Semaphore semaphorePresentComplete, uint32_t &imageIndex ){
	// TODO: research:
	// because we are blocking here, could this affect our frame rate? could it take away time for cpu work?
	// we somehow need to make sure to keep the internal time value increasing in regular intervals,
	// tracking the current frame number!

	//mImageIndex = mDevice.acquireNextImageKHR( mSwapchain, UINT64_MAX, semaphorePresentComplete, nullptr );

	auto err = vkAcquireNextImageKHR( mDevice, mVkSwapchain, UINT64_MAX, semaphorePresentComplete, ( VkFence )nullptr, &imageIndex );
	
	if ( err != VK_SUCCESS ){
		ofLogWarning() << "Swapchain image acquisition returned: " << err;
		imageIndex = mImageIndex;
	}
	mImageIndex = imageIndex;

	return ::vk::Result(err);
}

// ----------------------------------------------------------------------
  
// Present the current image to the queue
vk::Result Swapchain::queuePresent( ::vk::Queue queue, uint32_t currentBuffer ){
	std::vector<::vk::Semaphore> noSemaphores;
	return queuePresent( queue, currentBuffer, noSemaphores);
}

// ----------------------------------------------------------------------

vk::Result Swapchain::queuePresent( ::vk::Queue queue, uint32_t currentImageIndex, const std::vector<::vk::Semaphore>& waitSemaphores_ ){
	
	::vk::PresentInfoKHR presentInfo;
	presentInfo
		.setWaitSemaphoreCount( waitSemaphores_.size() )
		.setPWaitSemaphores( waitSemaphores_.data())
		.setSwapchainCount( 1 )
		.setPSwapchains( &mVkSwapchain )
		.setPImageIndices( &currentImageIndex )
		;

	// each command wich begins with vkQueue... is appended to the end of the 
	// queue. this includes presenting.
	return queue.presentKHR( presentInfo );
}

// ----------------------------------------------------------------------

void Swapchain::querySurfaceCapabilities(){

	if ( mSurfaceProperties.queried == false ){
		
		// we need to find out if the current physical device supports PRESENT
		mRendererProperties.physicalDevice.getSurfaceSupportKHR( mRendererProperties.graphicsFamilyIndex, mSettings.windowSurface, &mSurfaceProperties.presentSupported );

		// find out which color formats are supported
		// Get list of supported surface formats
		mSurfaceProperties.surfaceFormats = mRendererProperties.physicalDevice.getSurfaceFormatsKHR( mSettings.windowSurface );

		mSurfaceProperties.capabilities = mRendererProperties.physicalDevice.getSurfaceCapabilitiesKHR( mSettings.windowSurface );
		mSurfaceProperties.presentmodes = mRendererProperties.physicalDevice.getSurfacePresentModesKHR( mSettings.windowSurface );

		// If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
		// there is no preferred format, so we assume VK_FORMAT_B8G8R8A8_UNORM
		if ( ( mSurfaceProperties.surfaceFormats.size() == 1 ) && ( mSurfaceProperties.surfaceFormats[0].format == ::vk::Format::eUndefined ) ){
			mWindowColorFormat.format = ::vk::Format::eB8G8R8A8Unorm;
		} else{
			// Always select the first available color format
			// If you need a specific format (e.g. SRGB) you'd need to
			// iterate over the list of available surface format and
			// check for its presence
			mWindowColorFormat.format = mSurfaceProperties.surfaceFormats[0].format;
		}
		mWindowColorFormat.colorSpace = mSurfaceProperties.surfaceFormats[0].colorSpace;

		ofLog() << "Present supported: " << ( mSurfaceProperties.presentSupported ? "TRUE" : "FALSE" );
		mSurfaceProperties.queried = true;
	}
}

// ----------------------------------------------------------------------
