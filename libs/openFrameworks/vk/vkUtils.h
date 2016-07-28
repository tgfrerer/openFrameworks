#pragma once
#include "vulkan/vulkan.h"

namespace of{
namespace vk{

	// This method is based on Sascha Willems' vulkantools.h/cpp
	// 
	// Assorted commonly used Vulkan helper functions
	// Copyright( C ) 2016 by Sascha Willems - www.saschawillems.de
	// This code is licensed under the MIT license( MIT ) ( http://opensource.org/licenses/MIT)
	//
	/// \brief  creates an image barrier object
	/// \note   you still have to add this barrier to the command buffer for it to become effective.
static VkImageMemoryBarrier createImageMemoryBarrier( VkImage image, VkImageAspectFlags aspectMask, VkImageLayout oldImageLayout, VkImageLayout newImageLayout ){

	VkImageMemoryBarrier imageMemoryBarrier{
		VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,                    // VkStructureType            sType;
		nullptr,                                                   // const void*                pNext;
		0,                                                         // VkAccessFlags              srcAccessMask;
		VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,                      // VkAccessFlags              dstAccessMask;
		oldImageLayout,                                            // VkImageLayout              oldLayout;
		newImageLayout,                                            // VkImageLayout              newLayout;
		VK_QUEUE_FAMILY_IGNORED,                                   // uint32_t                   srcQueueFamilyIndex;
		VK_QUEUE_FAMILY_IGNORED,                                   // uint32_t                   dstQueueFamilyIndex;
		image,                                                     // VkImage                    image;
		{                                                          // VkImageSubresourceRange    subresourceRange;
			aspectMask,                                               // VkImageAspectFlags    aspectMask;
			0,                                                        // uint32_t              baseMipLevel;
			1,                                                        // uint32_t              levelCount;
			0,                                                        // uint32_t              baseArrayLayer;
			1,                                                        // uint32_t              layerCount;
		},
	};

	// Source layouts (old)

	// Undefined layout
	// Only allowed as initial layout!
	// Make sure any writes to the image have been finished
	if ( oldImageLayout == VK_IMAGE_LAYOUT_PREINITIALIZED ){
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
	}

	// Old layout is color attachment
	// Make sure any writes to the color buffer have been finished
	if ( oldImageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ){
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	}

	// Old layout is depth/stencil attachment
	// Make sure any writes to the depth/stencil buffer have been finished
	if ( oldImageLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ){
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	}

	// Old layout is transfer source
	// Make sure any reads from the image have been finished
	if ( oldImageLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ){
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	}

	// Old layout is shader read (sampler, input attachment)
	// Make sure any shader reads from the image have been finished
	if ( oldImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ){
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
	}

	// Target layouts (new)

	// New layout is transfer destination (copy, blit)
	// Make sure any copies to the image have been finished
	if ( newImageLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ){
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	}

	// New layout is transfer source (copy, blit)
	// Make sure any reads from and writes to the image have been finished
	if ( newImageLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL ){
		imageMemoryBarrier.srcAccessMask = imageMemoryBarrier.srcAccessMask | VK_ACCESS_TRANSFER_READ_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	}

	// New layout is color attachment
	// Make sure any writes to the color buffer hav been finished
	if ( newImageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL ){
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		if ( oldImageLayout != VK_IMAGE_LAYOUT_UNDEFINED ){
			imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		}
	}

	// New layout is depth attachment
	// Make sure any writes to depth/stencil buffer have been finished
	if ( newImageLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ){
		imageMemoryBarrier.dstAccessMask =  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	}

	// New layout is shader read (sampler, input attachment)
	// Make sure any writes to the image have been finished
	if ( newImageLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL ){
		imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
		imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
	}


	// we're expecting the compiler to be smart enough to return this as an rvalue.
	return imageMemoryBarrier;
}

} // namespace vk
} // namespace of
