#pragma once

#include "vk/Swapchain.h"

namespace of{
namespace vk{

// ----------------------------------------------------------------------

struct ImgSwapchainSettings : public SwapchainSettings
{
	std::string path = "img_";
	::vk::Format colorFormat = ::vk::Format::eR8G8B8A8Unorm;
};

// ----------------------------------------------------------------------

class ImgSwapchain : public Swapchain
{
	const ImgSwapchainSettings mSettings;

	const uint32_t      &mImageCount = mSettings.numSwapchainImages;
	uint32_t             mImageIndex = 0;

	std::vector<::vk::DeviceMemory> mImageMemory; // TODO: this needs to go, use an image allocator
	std::vector<ImageWithView> mImages;  // owning, clients may only borrow

	std::vector<::vk::Fence> mImageTransferFence;

	RendererProperties      mRendererProperties;
	const ::vk::Device      &mDevice = mRendererProperties.device;

	::vk::Queue	mTransferQueue = nullptr;

public:

	ImgSwapchain( const ImgSwapchainSettings& settings_ );

	void setRendererProperties( const of::vk::RendererProperties& rendererProperties_ ) override;

	void setup() override;

	virtual ~ImgSwapchain();

	// Request an image index from the swapchain, so that we might render into it
	// the image must be returned to the swapchain when done using queuePresent
	// \note this might cause waiting.
	::vk::Result acquireNextImage( ::vk::Semaphore presentCompleteSemaphore, uint32_t &imageIndex ) override;


	// Present the current image to the queue
	// Waits with execution until all waitSemaphores have been signalled
	::vk::Result queuePresent( ::vk::Queue queue, std::mutex & queueMutex, const std::vector<::vk::Semaphore>& waitSemaphores_ ) override;

	// return images vector
	const std::vector<ImageWithView> & getImages() const override;

	// return image by index
	const ImageWithView& getImage( size_t i ) const override;

	// return number of swapchain images
	const uint32_t getImageCount() const override;

	// return last acquired buffer id
	const uint32_t & getCurrentImageIndex() const override;

	const ::vk::Format& getColorFormat() override;

	// Return current swapchain image width in pixels
	uint32_t getWidth() override;

	// Return current swapchain image height in pixels
	uint32_t getHeight() override;

	// Change width and height in internal settings. 
	// Caution: this method requires a call to setup() to be applied, and is very costly.
	void changeExtent( uint32_t w, uint32_t h ) override;

};

} // end namespace vk
} // end namespace of