#include "vk/Texture.h"
#include "ofVkRenderer.h"

using namespace std;
using namespace of::vk;

Texture::Settings::Settings() {
	// default sampler create info
	samplerInfo
		.setMagFilter(::vk::Filter::eLinear)
		.setMinFilter(::vk::Filter::eLinear)
		.setMipmapMode(::vk::SamplerMipmapMode::eLinear)
		.setAddressModeU(::vk::SamplerAddressMode::eClampToBorder)
		.setAddressModeV(::vk::SamplerAddressMode::eClampToBorder)
		.setAddressModeW(::vk::SamplerAddressMode::eRepeat)
		.setMipLodBias(0.f)
		.setAnisotropyEnable(VK_FALSE)
		.setMaxAnisotropy(0.f)
		.setCompareEnable(VK_FALSE)
		.setCompareOp(::vk::CompareOp::eLess)
		.setMinLod(0.f)
		.setMaxLod(1.f)
		.setBorderColor(::vk::BorderColor::eFloatTransparentBlack)
		.setUnnormalizedCoordinates(VK_FALSE)
		;

	imageViewInfo
		.setImage(nullptr)  //< there cannot be a default for this
		.setViewType(::vk::ImageViewType::e2D)
		.setFormat(::vk::Format::eR8G8B8A8Unorm)
		.setComponents({ ::vk::ComponentSwizzle::eR, ::vk::ComponentSwizzle::eG, ::vk::ComponentSwizzle::eB, ::vk::ComponentSwizzle::eA })
		.setSubresourceRange({ ::vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 })
		;
}

// ----------------------------------------------------------------------

void Texture::setup( const Texture::Settings& settings_){

	mSettings = settings_;

	if (!mSettings.device) {
		ofLogError() << "Cannot initialise Texture without device - make sure Texture::Settings::device is set and valid.";
		return;
	}

	// --------| invariant: device is valid

	// First we reset - so to make sure that no previous vk objects get leaked 
	// in case a Texture is re-used.
	reset();

	mImageView = mDevice.createImageView(settings_.imageViewInfo);
	mSampler   = mDevice.createSampler(settings_.samplerInfo);

}

// ----------------------------------------------------------------------

void Texture::reset() {
	if (mDevice) {

		if (mSampler) {
			mDevice.destroySampler(mSampler);
			mSampler = nullptr;
		}

		if (mImageView) {
			mDevice.destroyImageView(mImageView);
			mImageView = nullptr;
		}
		
	}
}

// ----------------------------------------------------------------------

Texture::~Texture(){
	// Todo: we must find a more elegant way of telling whether 
	// the sampler or the image view are still in flight or can
	// be deleted.
	reset();
}

// ----------------------------------------------------------------------



