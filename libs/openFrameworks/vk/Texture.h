#pragma once
#include "vulkan/vulkan.h"
#include "ofPixels.h"

namespace of{
namespace vk{

// TODO: Texture issues commands to the queue, and creates command buffers to do so.
//       There should be a way to make these operations more transparent. 
//       Also, transfer from host memory to device memory should be handled more transparently.

class Texture
{
public:
	struct TexData
	{
		// All data neccessary to describe a Vulkan texture 
		// Q: how are mip levels dealt with?

		VkImage           image       = nullptr;
		VkDeviceMemory    mem         = nullptr;
		VkImageView       view        = nullptr;
		VkSampler         sampler     = nullptr;

		int32_t           tex_width   = 0;
		int32_t           tex_height  = 0;
	};

private:
	TexData mTexData;
	VkDevice mDevice = nullptr;

public:

	Texture(){
	};

	VkSampler getVkSampler(){
		return mTexData.sampler;
	}

	VkImageView getVkImageView(){
		return mTexData.view;
	}

	void load( const ofPixels& pix_ );;

	~Texture(){
		
		// get device
		if ( mDevice ){

			auto err = vkDeviceWaitIdle( mDevice );
			assert( !err );
			// let's cleanup - 

			if ( mTexData.view ){
				vkDestroyImageView( mDevice, mTexData.view, nullptr );
				mTexData.view = nullptr;
			}
			if ( mTexData.image ){
				vkDestroyImage( mDevice, mTexData.image, nullptr );
				mTexData.image = nullptr;
			}
			if ( mTexData.sampler ){
				vkDestroySampler( mDevice, mTexData.sampler, nullptr );
				mTexData.sampler = nullptr;
			}

			if ( mTexData.mem ){
				vkFreeMemory( mDevice, mTexData.mem, nullptr );
				mTexData.mem = nullptr;
			}
		}
		
	};



};

} /* namespace vk */
} /* namespace of */
