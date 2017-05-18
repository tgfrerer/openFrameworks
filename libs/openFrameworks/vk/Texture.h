#pragma once
#include "vulkan/vulkan.hpp"

namespace of{
namespace vk{

class Texture
{
private:
	void init( const ::vk::Image & image_, const ::vk::SamplerCreateInfo & samplerInfo_ );
	
	const ::vk::Sampler     mSampler     = nullptr;
	const ::vk::ImageView   mImageView   = nullptr;
	const ::vk::ImageLayout mImageLayout = ::vk::ImageLayout::eShaderReadOnlyOptimal;

	const ::vk::Device      mDevice;

	Texture() = delete;

public:

	Texture( const ::vk::Device& device_, const ::vk::Image & image_ );
	Texture( const ::vk::Device& device_, const ::vk::Image & image_, const ::vk::SamplerCreateInfo& samplerInfo_ );

	
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
	static const ::vk::SamplerCreateInfo& Texture::getDefaultSamplerCreateInfo();
};

} /* namespace vk */
} /* namespace of */
