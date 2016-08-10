#pragma once
#include "vulkan/vulkan.h"
#include "vk/vkAllocator.h"
#include "ofPixels.h"

namespace of{
namespace vk{


// how do we give texture access to a command buffer?

class Texture
{
public:
	struct TexData
	{
		// All data neccessary to describe a Vulkan texture 
		// Q: how are mip levels dealt with?

		//VkSampler         sampler     = nullptr;

		VkImage           image       = nullptr;
		//VkImageLayout     imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;

		VkDeviceMemory    mem         = nullptr;
		VkImageView       view        = nullptr;

		int32_t           tex_width   = 0;
		int32_t           tex_height  = 0;
	};

private:
	TexData mTexData;

public:

	Texture(){
	};

	void load(const ofPixels& pix_){
		auto renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

		// get device
		auto device = renderer->getVkDevice();

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
		mTexData.tex_width  = pix_.getWidth();

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
			VK_IMAGE_USAGE_TRANSFER_SRC_BIT,                        // VkImageUsageFlags        usage;
			VK_SHARING_MODE_EXCLUSIVE,                              // VkSharingMode            sharingMode;
			0,                                                      // uint32_t                 queueFamilyIndexCount;
			nullptr,                                                // const uint32_t*          pQueueFamilyIndices;
			VK_IMAGE_LAYOUT_PREINITIALIZED                          // VkImageLayout            initialLayout;
		};

		
		vkCreateImage( device, &createInfo, nullptr, &mTexData.image );

		// now that we have created an abstract image view
		// we want to associate it with some memory.
		// for this, we first have to allocate some memory.
		// But before we can allocate memory, we need to know
		// what kind of memory to allocate.

		VkMemoryRequirements memReq;
		vkGetImageMemoryRequirements( device, mTexData.image, &memReq );

		VkMemoryAllocateInfo allocInfo;
		renderer->getMemoryAllocationInfo( memReq,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
			, allocInfo );

		// allocate device memory, and point to the new memory object in mTexData
		vkAllocateMemory( device, &allocInfo, nullptr, &mTexData.mem );

		// TODO: create a VkImageView image view - so we can actually sample this image.
		// the view deals with swizzles - and also with mipmamplevels. It is also 
		// defines the subresource range for the image.

		// attach deviceMemory to img
		vkBindImageMemory( device, mTexData.image, mTexData.mem, 0 );

		// now we want to write out pixels to device memory

		void * pData;
		vkMapMemory( device, mTexData.mem, 0, allocInfo.allocationSize, 0, &pData );
		// write to mapped memory - as this is coherent, write will 
		// be visible to GPU without need to flush
		memcpy( pData, pix_.getData(), pix_.getTotalBytes() );
		vkUnmapMemory( device, mTexData.mem );

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
			vkAllocateCommandBuffers( device, &allocInfo, &cmd );
		}

		VkCommandBufferBeginInfo beginInfo{
			VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,                // VkStructureType                          sType;
			nullptr,                                                    // const void*                              pNext;
			VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,                // VkCommandBufferUsageFlags                flags;
			nullptr,                                                    // const VkCommandBufferInheritanceInfo*    pInheritanceInfo;
		};

		vkBeginCommandBuffer( cmd, &beginInfo );

		//// create an image memory barrier
		//auto imageBarrier = of::vk::createImageMemoryBarrier( 
		//	mTexData.image, 
		//	VK_IMAGE_ASPECT_COLOR_BIT,
		//	createInfo.initialLayout,                  // from: preinitialised
		//	VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL   // to  :	shader read optimal
		//);

		// Record image memory barrier into pipeline barrier - this will 
		// execute before the next command buffer in the current queue,
		// and will transfer the image layout so that it can be sampled.
		//vkCmdPipelineBarrier( cmd,
		//	VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		//	VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		//	0,
		//	0, nullptr,
		//	0, nullptr,
		//	1, &imageBarrier );

		// we're done recording this mini command buffer
		auto err = vkEndCommandBuffer( cmd );
		assert( !err );

		// Now we want to submit this command buffer.
		// So we have to fill the fence and semaphore paramters for 
		// command buffer submit info with defaults

		VkFenceCreateInfo fenceInfo;
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.pNext = nullptr;
		fenceInfo.flags = 0;
		VkFence cmdFence;
		err = vkCreateFence( device, &fenceInfo, nullptr, &cmdFence );
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
		vkWaitForFences( device, 1, &cmdFence, true, 10000 ); // wait at most 10 ms
		// free command buffer now that the command has executed.
		vkFreeCommandBuffers( device, cmdPool, 1, &cmd );

		// create an image view which may or may not get sampled.
		
		VkImageViewCreateInfo view_info {
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

		//vkCreateImageView( device, &view_info, nullptr, &mTexData.view );
		
	};

	~Texture(){
		auto renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

		// get device
		auto device = renderer->getVkDevice();
		auto err = vkDeviceWaitIdle( device );
		assert( !err );
		// let's cleanup - 

		// first, remove the image view 
		if ( mTexData.mem ){
			vkFreeMemory( device, mTexData.mem, nullptr );
			// null all pointers
			mTexData = TexData();
		}
	};



};

} /* namespace vk */
} /* namespace of */
