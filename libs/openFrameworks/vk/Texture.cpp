#include "vk/Texture.h"
#include "ofVkRenderer.h"

void of::vk::Texture::load( const ofPixels & pix_ ){
	auto renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

	// get device
	mDevice = renderer->getVkDevice();

	// get command pool
	auto& cmdPool = renderer->getCommandPool();

	// get queue
	auto& queue = renderer->getQueue();

	/*

	Transfer of memory happens when you write to memory - we want coherent host-visible memory, so that
	we can be sure that memory is on gpu once memory is finished writing i.e. memcpy returns control.

	If we can't get host-visible coherent memory, we have to flush the affected memory ranges.

	We then have to transition memory to device-visible memory, we can do this with a number of commands:

	* vkCmdPipelineBarrier with Image memory barrier
	* vkCmdWaitEvents      with Image memory barrier
	* subpass dependency within render pass

	*/

	mTexData.tex_height = pix_.getHeight();
	mTexData.tex_width = pix_.getWidth();

	VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
	VkExtent3D extent = {
		mTexData.tex_width,
		mTexData.tex_height,
		1
	};
	VkImageCreateInfo createInfo = {
		VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,                    // VkStructureType          sType;
		nullptr,                                                // const void*              pNext;
		0,                                                      // VkImageCreateFlags       flags;
		VK_IMAGE_TYPE_2D,                                       // VkImageType              imageType;
		format,                                                 // VkFormat                 format;
		extent,                                                 // VkExtent3D               extent;
		1,                                                      // uint32_t                 mipLevels;
		1,                                                      // uint32_t                 arrayLayers;
		VK_SAMPLE_COUNT_1_BIT,     //<-- multisampling?         // VkSampleCountFlagBits    samples;
		VK_IMAGE_TILING_LINEAR,                                 // VkImageTiling            tiling;
		VK_IMAGE_USAGE_SAMPLED_BIT,                             // VkImageUsageFlags        usage;
		VK_SHARING_MODE_EXCLUSIVE,                              // VkSharingMode            sharingMode;
		0,                                                      // uint32_t                 queueFamilyIndexCount;
		nullptr,                                                // const uint32_t*          pQueueFamilyIndices;
		VK_IMAGE_LAYOUT_PREINITIALIZED                          // VkImageLayout            initialLayout;
	};


	auto err = vkCreateImage( mDevice, &createInfo, nullptr, &mTexData.image );
	assert( !err );

	// now that we have created an abstract image view
	// we want to associate it with some memory.
	// for this, we first have to allocate some memory.
	// But before we can allocate memory, we need to know
	// what kind of memory to allocate.

	VkMemoryRequirements memReq;
	vkGetImageMemoryRequirements( mDevice, mTexData.image, &memReq );

	VkMemoryAllocateInfo allocInfo;
	renderer->getMemoryAllocationInfo( memReq,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
		, allocInfo );

	// allocate device memory, and point to the new memory object in mTexData
	vkAllocateMemory( mDevice, &allocInfo, nullptr, &mTexData.mem );

	// TODO: create a VkImageView image view - so we can actually sample this image.
	// the view deals with swizzles - and also with mipmamplevels. It is also 
	// defines the subresource range for the image.

	// attach deviceMemory to img
	vkBindImageMemory( mDevice, mTexData.image, mTexData.mem, 0 );

	// now we want to write out pixels to device memory

	void * pData;
	vkMapMemory( mDevice, mTexData.mem, 0, allocInfo.allocationSize, 0, &pData );
	// write to mapped memory - as this is coherent, write will 
	// be visible to GPU without need to flush
	memcpy( pData, pix_.getData(), pix_.getTotalBytes() );
	vkUnmapMemory( mDevice, mTexData.mem );

	// first, we need a command buffer we can store the pipeline barrier command into
	// this command - the pipeline barrier with an image barrier - will transfer the 
	// image resource from its original layout to a layout that the gpu can use for 
	// sampling.
	VkCommandBuffer cmd = nullptr;
	{
		VkCommandBufferAllocateInfo allocInfo{
			VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			nullptr,
			cmdPool,
			VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			1,
		};
		vkAllocateCommandBuffers( mDevice, &allocInfo, &cmd );
	}

	VkCommandBufferBeginInfo beginInfo{
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,                // VkStructureType                          sType;
		nullptr,                                                    // const void*                              pNext;
		VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,                // VkCommandBufferUsageFlags                flags;
		nullptr,                                                    // const VkCommandBufferInheritanceInfo*    pInheritanceInfo;
	};

	vkBeginCommandBuffer( cmd, &beginInfo );

	// create image memory barrier to transfer image from preinitialised to sampler read optimal

	VkImageMemoryBarrier stagingBarrier{
		VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,                     // VkStructureType            sType;
		nullptr,                                                    // const void*                pNext;
		VK_ACCESS_HOST_WRITE_BIT,                                   // VkAccessFlags              srcAccessMask;
		VK_ACCESS_SHADER_READ_BIT,                                  // VkAccessFlags              dstAccessMask;
		VK_IMAGE_LAYOUT_PREINITIALIZED,                             // VkImageLayout              oldLayout;
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,                   // VkImageLayout              newLayout;
		VK_QUEUE_FAMILY_IGNORED,                                    // uint32_t                   srcQueueFamilyIndex;
		VK_QUEUE_FAMILY_IGNORED,                                    // uint32_t                   dstQueueFamilyIndex;
		mTexData.image,                                             // VkImage                    image;
		{
			VK_IMAGE_ASPECT_COLOR_BIT, // VkImageAspectFlags    aspectMask;
			0, // uint32_t              baseMipLevel;
			1, // uint32_t              levelCount;
			0, // uint32_t              baseArrayLayer;
			1, // uint32_t              layerCount;
		} // VkImageSubresourceRange    subresourceRange;
	};

	vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, 0, 0, 0, 1, &stagingBarrier );
	
	// we're done recording this mini command buffer
	err = vkEndCommandBuffer( cmd );
	assert( !err );

	// Now we want to submit this command buffer.
	// So we have to fill the fence and semaphore paramters for 
	// command buffer submit info with defaults

	VkFenceCreateInfo fenceInfo;
	fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	fenceInfo.pNext = nullptr;
	fenceInfo.flags = 0;
	VkFence cmdFence;
	err = vkCreateFence( mDevice, &fenceInfo, nullptr, &cmdFence );
	assert( !err );

	VkSubmitInfo submitInfo{
		VK_STRUCTURE_TYPE_SUBMIT_INFO,                             // VkStructureType                sType;
		nullptr,                                                   // const void*                    pNext;
		0,                                                         // uint32_t                       waitSemaphoreCount;
		nullptr,                                                   // const VkSemaphore*             pWaitSemaphores;
		nullptr,                                                   // const VkPipelineStageFlags*    pWaitDstStageMask;
		1,                                                         // uint32_t                       commandBufferCount;
		&cmd,                                                      // const VkCommandBuffer*         pCommandBuffers;
		0,                                                         // uint32_t                       signalSemaphoreCount;
		nullptr,                                                   // const VkSemaphore*             pSignalSemaphores;
	};

	// Allright - submit the command buffer.
	// note that the fence is optional - but we use it to find out when the command buffer that 
	// we just added has been executed. 
	// That way we know when it's safe to free that command buffer.
	// we could also use the fence to figure out if the image has finished uploading.

	err = vkQueueSubmit( queue, 1, &submitInfo, cmdFence );
	assert( !err );


	// You could now use vkGetFenceStatus to check whether the transfer has been completed. 

	// cleanups
	vkWaitForFences( mDevice, 1, &cmdFence, true, 100000 ); // wait at most 100 ms
														  // free command buffer now that the command has executed.
	//vkFreeCommandBuffers( device, cmdPool, 1, &cmd );

	vkDestroyFence( mDevice, cmdFence, nullptr );

	// create an image view which may or may not get sampled.

	VkImageViewCreateFlags imageViewCreateFlags = VK_IMAGE_USAGE_SAMPLED_BIT;

	VkImageViewCreateInfo view_info{
		VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,     // VkStructureType            sType;
		nullptr,                                      // const void*                pNext;
		0,                                            // VkImageViewCreateFlags     flags;
		mTexData.image,                               // VkImage                    image;
		VK_IMAGE_VIEW_TYPE_2D,                        // VkImageViewType            viewType;
		VK_FORMAT_R8G8B8A8_UNORM,                     // VkFormat                   format;
		{                                             // VkComponentMapping         components;
			VK_COMPONENT_SWIZZLE_R,
			VK_COMPONENT_SWIZZLE_G,
			VK_COMPONENT_SWIZZLE_B,
			VK_COMPONENT_SWIZZLE_A,
		},
		{                                             // VkImageSubresourceRange    subresourceRange;
			VK_IMAGE_ASPECT_COLOR_BIT,                   // VkImageAspectFlags    aspectMask;
			0,                                           // uint32_t              baseMipLevel;
			1,                                           // uint32_t              levelCount;
			0,                                           // uint32_t              baseArrayLayer;
			1,                                           // uint32_t              layerCount;
		}
	};

	vkCreateImageView( mDevice, &view_info, nullptr, &mTexData.view );

	VkSamplerCreateFlags samplerFlags = 0;

	VkSamplerCreateInfo samplerInfo{
		VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,        // VkStructureType         sType;
		nullptr,                                      // const void*             pNext;
		samplerFlags,                                 // VkSamplerCreateFlags    flags;
		VK_FILTER_LINEAR,                             // VkFilter                magFilter;
		VK_FILTER_LINEAR,                             // VkFilter                minFilter;
		VK_SAMPLER_MIPMAP_MODE_LINEAR,                // VkSamplerMipmapMode     mipmapMode;
		VK_SAMPLER_ADDRESS_MODE_REPEAT,               // VkSamplerAddressMode    addressModeU;
		VK_SAMPLER_ADDRESS_MODE_REPEAT,               // VkSamplerAddressMode    addressModeV;
		VK_SAMPLER_ADDRESS_MODE_REPEAT,               // VkSamplerAddressMode    addressModeW;
		0.f,                                          // float                   mipLodBias;
		VK_FALSE,                                     // VkBool32                anisotropyEnable;
		0.f,                                          // float                   maxAnisotropy;
		VK_FALSE,                                     // VkBool32                compareEnable;
		VK_COMPARE_OP_LESS,                           // VkCompareOp             compareOp;
		0.f,                                          // float                   minLod;
		1.f,                                          // float                   maxLod;
		VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,      // VkBorderColor           borderColor;
		VK_FALSE,                                     // VkBool32                unnormalizedCoordinates;
	};

	vkCreateSampler( mDevice, &samplerInfo, nullptr, &mTexData.sampler );

}
