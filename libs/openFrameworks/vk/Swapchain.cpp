#include "vk/Swapchain.h"
#include <vector>
#include "ofLog.h"
#include "GLFW\glfw3.h"
#include "vk/vkUtils.h"


void Swapchain::setup(
	const VkInstance & instance_,
	const VkDevice & device_,
	const VkPhysicalDevice & physicalDevice_,
	const VkSurfaceKHR & surface_,
	const VkSurfaceFormatKHR& surfaceFormat_,
	VkCommandBuffer cmdBuffer,
	uint32_t & width_,
	uint32_t & height_ ){
	VkResult err = VK_SUCCESS;

	mInstance = instance_;
	mDevice = device_;
	mPhysicalDevice = physicalDevice_;
	mWindowSurface = surface_;
	mColorFormat = surfaceFormat_;

	VkSwapchainKHR oldSwapchain = mSwapchain;

	// Get physical device surface properties and formats
	VkSurfaceCapabilitiesKHR surfCaps;
	err = vkGetPhysicalDeviceSurfaceCapabilitiesKHR( mPhysicalDevice, mWindowSurface, &surfCaps );

	// get available present modes for physical device
	uint32_t presentModeCount = 0;
	err = vkGetPhysicalDeviceSurfacePresentModesKHR( mPhysicalDevice, mWindowSurface, &presentModeCount, VK_NULL_HANDLE );
	std::vector<VkPresentModeKHR> presentModes( presentModeCount );
	err = vkGetPhysicalDeviceSurfacePresentModesKHR( mPhysicalDevice, mWindowSurface, &presentModeCount, presentModes.data() );

	// either set or get the swapchain surface extents
	VkExtent2D swapchainExtent = {};

	if ( surfCaps.currentExtent.width == -1 ){
		swapchainExtent.width = width_;
		swapchainExtent.height = height_;
	}
	else{
		swapchainExtent = surfCaps.currentExtent;
		width_ = surfCaps.currentExtent.width;
		height_ = surfCaps.currentExtent.height;
	}

	// Prefer mailbox mode if present, it's the lowest latency non-tearing present mode
	// TODO: allow user to choose presentmode 
	// with setup constants.
	VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;
	for ( size_t i = 0; i < presentModeCount; i++ ){
		if ( presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR ){
			swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
			break;
		}
		if ( ( swapchainPresentMode != VK_PRESENT_MODE_MAILBOX_KHR ) && ( presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR ) ){
			swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
		}
	}

	// Determine the number of images
	// TODO: allow user to choose number of swapchain elements
	// with setup constants
	uint32_t desiredNumberOfSwapchainImages = surfCaps.minImageCount + 1;
	if ( ( surfCaps.maxImageCount > 0 ) && ( desiredNumberOfSwapchainImages > surfCaps.maxImageCount ) ){
		desiredNumberOfSwapchainImages = surfCaps.maxImageCount;
	}

	VkSurfaceTransformFlagsKHR preTransform;

	// Note: this will be interesting for mobile devices
	// - if rotation and mirroring for the final output can 
	// be defined here.

	if ( surfCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR ){
		preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	}
	else{
		preTransform = surfCaps.currentTransform;
	}

	VkSwapchainCreateInfoKHR swapchainCI = {};
	swapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainCI.pNext = VK_NULL_HANDLE;
	swapchainCI.surface = mWindowSurface;
	swapchainCI.minImageCount = desiredNumberOfSwapchainImages;
	swapchainCI.imageFormat = mColorFormat.format;
	swapchainCI.imageColorSpace = mColorFormat.colorSpace;
	swapchainCI.imageExtent = { swapchainExtent.width, swapchainExtent.height };
	swapchainCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchainCI.preTransform = (VkSurfaceTransformFlagBitsKHR)preTransform;
	swapchainCI.imageArrayLayers = 1;
	swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainCI.queueFamilyIndexCount = 0;
	swapchainCI.pQueueFamilyIndices = VK_NULL_HANDLE;
	swapchainCI.presentMode = swapchainPresentMode;
	swapchainCI.oldSwapchain = oldSwapchain;
	swapchainCI.clipped = VK_TRUE;
	swapchainCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

	err = vkCreateSwapchainKHR( mDevice, &swapchainCI, nullptr, &mSwapchain );

	// If an existing sawp chain is re-created, destroy the old swap chain
	// This also cleans up all the presentable images
	if ( oldSwapchain != VK_NULL_HANDLE ){
		vkDestroySwapchainKHR( mDevice, oldSwapchain, nullptr );
	}

	std::vector<VkImage> swapchainImages;	  // these are owned by the swapchain, they
	// get the image count
	vkGetSwapchainImagesKHR( mDevice, mSwapchain, &mImageCount, VK_NULL_HANDLE);

	// Get the swap chain images
	swapchainImages.resize( mImageCount );
	vkGetSwapchainImagesKHR( mDevice, mSwapchain, &mImageCount, swapchainImages.data() );
	
	// Get the swap chain buffers containing the image and imageview
	mImages.resize( mImageCount );
	for ( uint32_t i = 0; i < mImageCount; i++ ){
		VkImageViewCreateInfo colorAttachmentView = {};
		colorAttachmentView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		colorAttachmentView.pNext = NULL;
		colorAttachmentView.format = mColorFormat.format;
		colorAttachmentView.components = {
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_G,
			VK_COMPONENT_SWIZZLE_B,
			VK_COMPONENT_SWIZZLE_A
		};
		colorAttachmentView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorAttachmentView.subresourceRange.baseMipLevel = 0;
		colorAttachmentView.subresourceRange.levelCount = 1;
		colorAttachmentView.subresourceRange.baseArrayLayer = 0;
		colorAttachmentView.subresourceRange.layerCount = 1;
		colorAttachmentView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorAttachmentView.flags = 0;

		mImages[i].imageRef = swapchainImages[i];

		auto transferBarrier = of::vk::createImageBarrier( mImages[i].imageRef,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );

		// Append pipeline barrier to commandBuffer
		vkCmdPipelineBarrier(
			cmdBuffer,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			0,
			0, nullptr,
			0, nullptr,
			1, &transferBarrier );

		colorAttachmentView.image = mImages[i].imageRef;

		err = vkCreateImageView( mDevice, &colorAttachmentView, nullptr, &mImages[i].view );
		assert( !err );
	}

}

// ----------------------------------------------------------------------

void Swapchain::reset(){

	// it's imperative we clean up.
	
	for ( auto&b : mImages ){
		// note that we only destroy the VkImageView,
		// as the VkImage is owned by the swapchain mSwapchain
		// and will get destroyed when destroying the swapchain
		vkDestroyImageView( mDevice, b.view, nullptr );
	}
	mImages.clear();

	vkDestroySwapchainKHR( mDevice, mSwapchain, nullptr );
}

// ----------------------------------------------------------------------

// Acquires the next image in the swap chain
// blocks cpu until image has been acquired
// signals semaphorePresentComplete once image has been acquired
VkResult Swapchain::acquireNextImage( VkSemaphore semaphorePresentComplete, uint32_t *imageIndex ){
	// TODO: research:
	// because we are blocking here, could this affect our frame rate? could it take away time for cpu work?
	// we somehow need to make sure to keep the internal time value increasing in regular intervals,
	// tracking the current frame number!
	auto err = vkAcquireNextImageKHR( mDevice, mSwapchain, UINT64_MAX, semaphorePresentComplete, ( VkFence )nullptr, imageIndex );
	
	if ( err != VK_SUCCESS ){
		ofLogWarning() << "image acquisition returned: " << err;
	}
	mImageIndex = *imageIndex;

	return err;
}


// ----------------------------------------------------------------------
  
// Present the current image to the queue
VkResult Swapchain::queuePresent( VkQueue queue, uint32_t currentBuffer ){
	std::vector<VkSemaphore> noSemaphores;
	return queuePresent( queue, currentBuffer, noSemaphores);
}

// ----------------------------------------------------------------------

VkResult Swapchain::queuePresent( VkQueue queue, uint32_t currentImageIndex, std::vector<VkSemaphore> waitSemaphores_ ){
	VkPresentInfoKHR presentInfo = {
		VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,                            // VkStructureType          sType;
		nullptr,                                                       // const void*              pNext;
		waitSemaphores_.size(),                                        // uint32_t                 waitSemaphoreCount;
		waitSemaphores_.data(),                                        // const VkSemaphore*       pWaitSemaphores;
		1,                                                             // uint32_t                 swapchainCount;
		&mSwapchain,                                                   // const VkSwapchainKHR*    pSwapchains;
		&currentImageIndex,                                            // const uint32_t*          pImageIndices;
		nullptr,                                                       // VkResult*                pResults;
	};
	// each command wich begins with vkQueue... is appended to the end of the 
	// queue. this includes presenting.
	return vkQueuePresentKHR( queue, &presentInfo );
}

