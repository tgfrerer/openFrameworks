#pragma once

#include <vulkan\vulkan.h>
#include <vector>

// TODO: rename
typedef struct _SwapchainBuffers
{
	VkImage imageRef;	   // owned by SwapchainKHR, only referenced here
	VkImageView view;
} SwapchainBuffer;

class Swapchain {

	VkSwapchainKHR       mSwapchain = VK_NULL_HANDLE;

	VkInstance           mInstance = VK_NULL_HANDLE;
	VkDevice             mDevice = VK_NULL_HANDLE;
	VkPhysicalDevice     mPhysicalDevice = VK_NULL_HANDLE;

	VkSurfaceKHR         mWindowSurface = VK_NULL_HANDLE;
	VkSurfaceFormatKHR   mColorFormat = {};
	
	uint32_t             mImageCount = 0;
	uint32_t             mCurrentBuffer = 0;

	// these are the front and the back buffer for our main 
	// render target.
	// todo: this should not be public, as these are owned by 
	// us.
	std::vector<SwapchainBuffer> buffers;  // owning!

public:

	const std::vector<SwapchainBuffer> & getBuffers() const {
		return buffers;
	};

	const SwapchainBuffer& getBuffer( size_t i ) const{
		return buffers[i];
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
	VkResult acquireNextImage( VkSemaphore presentCompleteSemaphore, uint32_t *currentBuffer );

	// mark the image ready to present by the swapchain.
	// this returns the image to the swapchain and tells the 
	// swapchain that we're done rendering to it and that 
	// it may show the image on screen.
	VkResult queuePresent( VkQueue queue, uint32_t currentBuffer );
	VkResult queuePresent( VkQueue queue, uint32_t currentBuffer, VkSemaphore waitSemaphore );

	// return number of swapchain buffers
	const uint32_t & getImageCount();
	// return last acquired buffer id
	inline const uint32_t & getCurrentBuffer() const {
		return mCurrentBuffer;
	};
};