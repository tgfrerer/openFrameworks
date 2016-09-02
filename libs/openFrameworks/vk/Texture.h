#pragma once
#include "vulkan/vulkan.hpp"
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

		::vk::Image           image       = nullptr;
		::vk::DeviceMemory    mem         = nullptr;
		::vk::ImageView       view        = nullptr;
		::vk::Sampler         sampler     = nullptr;

		uint32_t           tex_width   = 0;
		uint32_t           tex_height  = 0;
	};

private:
	TexData mTexData;
	::vk::Device mDevice = nullptr;

public:

	Texture(){
	};

	::vk::Sampler getVkSampler(){
		return mTexData.sampler;
	}

	::vk::ImageView getVkImageView(){
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
