#pragma once

#include "vulkan/vulkan.hpp"
#include "vk/HelperTypes.h"

#include <vector>

namespace of{
namespace vk{


// TODO: rename
typedef struct
{
	::vk::Image imageRef;	   // owned by SwapchainKHR, only referenced here
	::vk::ImageView view;
} SwapchainImage;


struct SwapchainSettings
{
	uint32_t                width              = 0;
	uint32_t                height             = 0;
	uint32_t                numSwapChainFrames = 0;
	::vk::PresentModeKHR    presentMode        = ::vk::PresentModeKHR::eFifo;
	::vk::SurfaceKHR        windowSurface      = nullptr;
};


class Swapchain {

	::vk::SwapchainKHR      mVkSwapchain;
	::vk::SurfaceFormatKHR  mWindowColorFormat = {};

	uint32_t             mImageCount = 0;
	uint32_t             mImageIndex = 0;

	std::vector<SwapchainImage> mImages;  // owning, clients may only borrow!
	
	void                 querySurfaceCapabilities();

	RendererProperties mRendererProperties;
	const ::vk::Device &mDevice = mRendererProperties.device;

	const SwapchainSettings mSettings;

	struct SurfaceProperties
	{
		bool queried = false;
		::vk::SurfaceCapabilitiesKHR        capabilities;
		std::vector<::vk::PresentModeKHR>   presentmodes;
		std::vector<::vk::SurfaceFormatKHR> surfaceFormats;
		VkBool32 presentSupported = VK_FALSE;
	} mSurfaceProperties;

	//void querySurfaceCapabilities();

public:

	Swapchain( const SwapchainSettings& settings_ )
	: mSettings(settings_){
	};

	~Swapchain();

	void setRendererProperties( const of::vk::RendererProperties& rendererProperties_ ){
		mRendererProperties = rendererProperties_;
	};

	void setup();

	// request an image from the swapchain, so that we might render to it
	// the image must be returned to the swapchain when done using 
	// queuePresent
	// \note this might cause waiting.
	::vk::Result acquireNextImage( ::vk::Semaphore presentCompleteSemaphore, uint32_t &imageIndex );

	// mark the image ready to present by the swapchain.
	// this returns the image to the swapchain and tells the 
	// swapchain that we're done rendering to it and that 
	// it may show the image on screen.
	::vk::Result queuePresent( ::vk::Queue queue, uint32_t imageIndex );
	// Present the current image to the queue
	// Waits with execution until all waitSemaphores have been signalled
	::vk::Result queuePresent( ::vk::Queue queue, uint32_t imageIndex, const std::vector<::vk::Semaphore>& waitSemaphores_ );

	// return images vector
	inline const std::vector<SwapchainImage> & getImages() const{ return mImages; };
	
	// return image by index
	inline const SwapchainImage& getImage( size_t i ) const{ return mImages[i]; };

	// return number of swapchain images
	inline const uint32_t & getImageCount() const { return mImageCount; };
	
	// return last acquired buffer id
	inline const uint32_t & getCurrentImageIndex() const { return mImageIndex; };

	inline ::vk::Format& getColorFormat(){
		return mWindowColorFormat.format;
	};

	// return current swapchain image width in pixels
	inline uint32_t getWidth(){
		return mSettings.width;
	};

	// retunr current swapchain image height in pixels
	inline uint32_t getHeight(){
		return mSettings.height;
	}

	// change width and height in internal settings. 
	// caution: this method requires a call to setup() to be applied.
	inline void changeExtent( uint32_t w, uint32_t h ){
		const_cast<uint32_t&>(mSettings.width ) = w;
		const_cast<uint32_t&>(mSettings.height) = h;
	};
};



} // end namespace vk
} // end namespace of
