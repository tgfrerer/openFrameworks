#pragma once
#include "vulkan/vulkan.hpp"

/*

An of::vk::Texture combines a vulkan Image, ImageView and Sampler. 
This is mostly for convenience. Note that while the ImageView and 
Sampler are owned by of::vk::Texture, the Image itself is not.

*/

namespace of{
namespace vk{

class Texture
{
private:
	void init( const ::vk::SamplerCreateInfo & samplerInfo_, const ::vk::ImageViewCreateInfo& imageViewInfo_ );
	
	const ::vk::Sampler     mSampler     = nullptr;
	const ::vk::ImageView   mImageView   = nullptr;
	const ::vk::ImageLayout mImageLayout = ::vk::ImageLayout::eShaderReadOnlyOptimal;

	const ::vk::Device      mDevice;

	Texture() = delete;

public:

	Texture( const ::vk::Device& device_, const ::vk::Image & image_ );
	Texture( const ::vk::Device& device_, const ::vk::SamplerCreateInfo& samplerInfo_, const ::vk::ImageViewCreateInfo& imageViewInfo_ );
	
	const ::vk::Sampler& getSampler() const{
		return mSampler;
	}

	const ::vk::ImageView& getImageView() const {
		return mImageView;
	}

	const ::vk::ImageLayout& getImageLayout() const{
		return mImageLayout;
	}

	~Texture();

	// helper method
	static ::vk::SamplerCreateInfo getDefaultSamplerCreateInfo();
	static ::vk::ImageViewCreateInfo getDefaultImageViewCreateInfo( const ::vk::Image& image_ );
};

} /* namespace vk */
} /* namespace of */
