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
public:
	struct Settings {
		::vk::Device              device = nullptr;
		::vk::SamplerCreateInfo   samplerInfo;
		::vk::ImageViewCreateInfo imageViewInfo;
		
		// Default constructor will initialise Settings with 
		// sensible values for most createInfo fields.
		Settings(); 

		inline Settings& setDevice(const ::vk::Device& device_) {
			device = device_;
			return *this;
		}

		inline Settings& setImage(const ::vk::Image& img) {
			imageViewInfo.setImage(img);
			return *this;
		}
	};

private:

	Settings          mSettings;

	::vk::Device &    mDevice      = mSettings.device;
	::vk::Sampler     mSampler     = nullptr;
	::vk::ImageView   mImageView   = nullptr;
	::vk::ImageLayout mImageLayout = ::vk::ImageLayout::eShaderReadOnlyOptimal;

public:

	void setup(const Settings& settings_);
	void reset();

	Texture() = default;
	Texture(const Texture&) = delete;                   // no copy constructor please
	Texture(const Texture&&) = delete;                  // no move constructor please
	Texture& operator=(const Texture& rhs) & = delete;  // no copy assignment constructor please
	Texture& operator=(const Texture&& rhs) & = delete; // no move assignment constructor please
	~Texture();

	const Settings & getSettings() {
		return mSettings;
	}

	const ::vk::Sampler& getSampler() const {
		return mSampler;
	}

	const ::vk::ImageView& getImageView() const {
		return mImageView;
	}

	const ::vk::ImageLayout& getImageLayout() const {
		return mImageLayout;
	}

};

} /* namespace vk */
} /* namespace of */
