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

		VkImage           image       = nullptr;
		VkDeviceMemory    mem         = nullptr;
		VkImageView       view        = nullptr;
		VkSampler         sampler     = nullptr;

		int32_t           tex_width   = 0;
		int32_t           tex_height  = 0;
	};

private:
	TexData mTexData;
	std::weak_ptr<ofVkRenderer> mRenderer;

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
		if ( auto renderer = mRenderer.lock() ){

			// get device
			auto device = renderer->getVkDevice();
			auto err = vkDeviceWaitIdle( device );
			assert( !err );
			// let's cleanup - 

			if ( mTexData.view ){
				vkDestroyImageView( device, mTexData.view, nullptr );
				mTexData.view = nullptr;
			}
			if ( mTexData.image ){
				vkDestroyImage( device, mTexData.image, nullptr );
				mTexData.image = nullptr;
			}
			if ( mTexData.sampler ){
				vkDestroySampler( device, mTexData.sampler, nullptr );
				mTexData.sampler = nullptr;
			}

			if ( mTexData.mem ){
				vkFreeMemory( device, mTexData.mem, nullptr );
				mTexData.mem = nullptr;
			}
			mTexData = TexData();
		}
		
	};



};

} /* namespace vk */
} /* namespace of */
