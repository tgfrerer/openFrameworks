#pragma once

#include <vulkan\vulkan.h>
#include <vector>

// TODO: rename
typedef struct
{
	VkImage imageRef;	   // owned by SwapchainKHR, only referenced here
	VkImageView view;
} SwapchainImage;

class Swapchain {

	VkSwapchainKHR       mSwapchain      = VK_NULL_HANDLE;
	VkInstance           mInstance       = VK_NULL_HANDLE;
	VkDevice             mDevice         = VK_NULL_HANDLE;
	VkPhysicalDevice     mPhysicalDevice = VK_NULL_HANDLE;

	VkSurfaceKHR         mWindowSurface  = VK_NULL_HANDLE;
	VkSurfaceFormatKHR   mColorFormat    = {};
	
	uint32_t             mImageCount = 0;
	uint32_t             mImageIndex = 0;

	std::vector<SwapchainImage> mImages;  // owning, clients may only borrow!

public:

	const std::vector<SwapchainImage> & getImages() const {
		return mImages;
	};

	const SwapchainImage& getImage( size_t i ) const{
		return mImages[i];
	};

	void setup( 
		const VkInstance& instance_, 
		const VkDevice& device_, 
		const VkPhysicalDevice& physicalDevice_, 
		const VkSurfaceKHR& surface_,
		const VkSurfaceFormatKHR& surfaceFormat_,
		VkCommandBuffer cmdBuffer,
		uint32_t & width,
		uint32_t & height 
	);

	// resets all vk objects owned by this swapchain 
	void reset();

	// request an image from the swapchain, so that we might render to it
	// the image must be returned to the swapchain when done using 
	// queuePresent
	// \note this might cause waiting.
	VkResult acquireNextImage( VkSemaphore presentCompleteSemaphore, uint32_t *imageIndex );

	// mark the image ready to present by the swapchain.
	// this returns the image to the swapchain and tells the 
	// swapchain that we're done rendering to it and that 
	// it may show the image on screen.
	VkResult queuePresent( VkQueue queue, uint32_t imageIndex );
	// Present the current image to the queue
	// Waits with execution until all waitSemaphores have been signalled
	VkResult queuePresent( VkQueue queue, uint32_t imageIndex, std::vector<VkSemaphore> waitSemaphores_ );

	// return number of swapchain images
	inline const uint32_t & getImageCount() const { return mImageCount; };
	
	// return last acquired buffer id
	inline const uint32_t & getCurrentImageIndex() const { return mImageIndex; };

};