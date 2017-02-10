#include "vk/Swapchain.h"
#include <vector>
#include "ofLog.h"

using namespace of::vk;

// ----------------------------------------------------------------------

WsiSwapchain::WsiSwapchain( const WsiSwapchainSettings & settings_ )
	: mSettings( settings_ ){
}

// ----------------------------------------------------------------------

WsiSwapchain::~WsiSwapchain(){
	// It's imperative we clean up.

	for ( auto & b : mImages ){
		// Note that we only destroy the VkImageView,
		// as the VkImage is owned by the swapchain mSwapchain
		// and will get destroyed when destroying the swapchain
		mDevice.destroyImageView( b.view );
	}
	mImages.clear();

	mDevice.destroySwapchainKHR( mVkSwapchain );
}

// ----------------------------------------------------------------------

void WsiSwapchain::setup()
{
	::vk::Result err = ::vk::Result::eSuccess;

	// The surface in SwapchainSettings::windowSurface has been assigned by glfwwindow, through glfw,
	// just before this setup() method was called.
	querySurfaceCapabilities();

	::vk::SwapchainKHR oldSwapchain = mVkSwapchain;

	// Get physical device surface properties and formats
	const ::vk::SurfaceCapabilitiesKHR & surfCaps = mSurfaceProperties.capabilities;

	// Get available present modes for physical device
	const std::vector<::vk::PresentModeKHR>& presentModes = mSurfaceProperties.presentmodes;


	// Either set or get the swapchain surface extents
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

	// Write current present mode back to reference from parameter
	// so caller can find out whether chosen present mode has been 
	// applied successfully.
	const_cast<::vk::PresentModeKHR&>(mSettings.presentMode) = swapchainPresentMode;

	uint32_t desiredNumberOfSwapchainImages = std::max<uint32_t>( surfCaps.minImageCount, mSettings.numSwapChainFrames );
	if ( ( surfCaps.maxImageCount > 0 ) && ( desiredNumberOfSwapchainImages > surfCaps.maxImageCount ) ){
		desiredNumberOfSwapchainImages = surfCaps.maxImageCount;
	}

	// Write current value back to parameter reference so caller
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

// Return current swapchain image width in pixels

inline uint32_t WsiSwapchain::getWidth(){
	return mSettings.width;
}

// ----------------------------------------------------------------------
// Return current swapchain image height in pixels
inline uint32_t WsiSwapchain::getHeight(){
	return mSettings.height;
}

// ----------------------------------------------------------------------
// Change width and height in internal settings. 
// Caution: this method requires a call to setup() to be applied, and is very costly.
void WsiSwapchain::changeExtent( uint32_t w, uint32_t h ){
	mSurfaceProperties.queried = false;
	const_cast<uint32_t&>( mSettings.width ) = w;
	const_cast<uint32_t&>( mSettings.height ) = h;
}

// ----------------------------------------------------------------------

const ::vk::Format & WsiSwapchain::getColorFormat(){
	return mWindowColorFormat.format;
}

// ----------------------------------------------------------------------

// Acquires the next image in the swap chain
// Blocks cpu until image has been acquired
vk::Result WsiSwapchain::acquireNextImage( ::vk::Semaphore semaphorePresentComplete, uint32_t &imageIndex ){

	/*

	Q: What is the semaphore good for? 
	A: See vk spec pp. 610:
	
	"The semaphore must be unsignaled and not have any uncompleted signal or
wait operations pending. It will become signaled when the application can use the image. Queue operations that access
the image contents must wait until the semaphore signals; typically applications should include the semaphore in the
pWaitSemaphores list for the queue submission that transitions the image away from the VK_IMAGE_LAYOUT_
PRESENT_SRC_KHR layout. Use of the semaphore allows rendering operations to be recorded and submitted before the
presentation engine has completed its use of the image."
	
	This means, we must make sure not to render into the image before the semaphore signals. 
	We do this by adding the semaphore to the wait semaphores in the present queue. This also means, that
	the image only can be rendered into once the semaphore has been signalled.

	*/

	auto err = vkAcquireNextImageKHR( mDevice, mVkSwapchain, UINT64_MAX, semaphorePresentComplete, ( VkFence )nullptr, &imageIndex );
	
	if ( err != VK_SUCCESS ){
		ofLogWarning() << "Swapchain image acquisition returned: " << err;
		imageIndex = mImageIndex;
	}
	mImageIndex = imageIndex;

	return ::vk::Result(err);
}

// ----------------------------------------------------------------------

vk::Result WsiSwapchain::queuePresent( ::vk::Queue queue, const std::vector<::vk::Semaphore>& waitSemaphores_ ){
	
	::vk::PresentInfoKHR presentInfo;
	presentInfo
		.setWaitSemaphoreCount( waitSemaphores_.size() )
		.setPWaitSemaphores( waitSemaphores_.data())
		.setSwapchainCount( 1 )
		.setPSwapchains( &mVkSwapchain )
		.setPImageIndices( &mImageIndex)
		;

	// each command wich begins with vkQueue... is appended to the end of the 
	// queue. this includes presenting.
	return queue.presentKHR( presentInfo );
}

// ----------------------------------------------------------------------
// return images vector
const std::vector<ImageWithView>& WsiSwapchain::getImages() const{
	return mImages;
}

// ----------------------------------------------------------------------
// return image by index
const ImageWithView & WsiSwapchain::getImage( size_t i ) const{
	return mImages[i];
}

// ----------------------------------------------------------------------
// return number of swapchain images
const uint32_t & WsiSwapchain::getImageCount() const{
	return mImageCount;
}

// ----------------------------------------------------------------------
// return last acquired buffer id
const uint32_t & WsiSwapchain::getCurrentImageIndex() const{
	return mImageIndex;
}

// ----------------------------------------------------------------------

void WsiSwapchain::querySurfaceCapabilities(){

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

		// ofLog() << "Present supported: " << ( mSurfaceProperties.presentSupported ? "TRUE" : "FALSE" );
		mSurfaceProperties.queried = true;
	}
}

// ----------------------------------------------------------------------

void WsiSwapchain::setRendererProperties( const RendererProperties & rendererProperties_ ){
	mRendererProperties = rendererProperties_;
}



