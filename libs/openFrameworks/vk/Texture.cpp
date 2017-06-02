#include "vk/Texture.h"
#include "ofVkRenderer.h"

using namespace of::vk;

::vk::SamplerCreateInfo samplerCreateInfoPreset(){
	// default sampler create info
	::vk::SamplerCreateInfo samplerInfo;
	samplerInfo
		.setMagFilter( ::vk::Filter::eLinear )
		.setMinFilter( ::vk::Filter::eLinear )
		.setMipmapMode( ::vk::SamplerMipmapMode::eLinear )
		.setAddressModeU( ::vk::SamplerAddressMode::eClampToBorder )
		.setAddressModeV( ::vk::SamplerAddressMode::eClampToBorder )
		.setAddressModeW( ::vk::SamplerAddressMode::eRepeat )
		.setMipLodBias( 0.f )
		.setAnisotropyEnable( VK_FALSE )
		.setMaxAnisotropy( 0.f )
		.setCompareEnable( VK_FALSE )
		.setCompareOp( ::vk::CompareOp::eLess )
		.setMinLod( 0.f )
		.setMaxLod( 1.f )
		.setBorderColor( ::vk::BorderColor::eFloatTransparentBlack )
		.setUnnormalizedCoordinates( VK_FALSE )
		;
	return samplerInfo;
}

// ----------------------------------------------------------------------

::vk::ImageViewCreateInfo imageViewCreateInfoPreset( const ::vk::Image & image_ ){
	// default image view create info
	::vk::ImageViewCreateInfo imageViewCreateInfo;
	imageViewCreateInfo
		.setImage( image_ )
		.setViewType( ::vk::ImageViewType::e2D )
		.setFormat( ::vk::Format::eR8G8B8A8Unorm )
		.setComponents( { ::vk::ComponentSwizzle::eR, ::vk::ComponentSwizzle::eG, ::vk::ComponentSwizzle::eB, ::vk::ComponentSwizzle::eA } )
		.setSubresourceRange( { ::vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } )
		;
	return imageViewCreateInfo;
}
// ----------------------------------------------------------------------


::vk::SamplerCreateInfo of::vk::Texture::getDefaultSamplerCreateInfo(){
	auto info = samplerCreateInfoPreset();
	return info;
}

// ----------------------------------------------------------------------

::vk::ImageViewCreateInfo of::vk::Texture::getDefaultImageViewCreateInfo(const ::vk::Image& image_){
	auto info = imageViewCreateInfoPreset(image_);
	return info;
}

// ----------------------------------------------------------------------

Texture::Texture( const ::vk::Device & device_, const ::vk::SamplerCreateInfo& samplerInfo_, const ::vk::ImageViewCreateInfo& imageViewInfo_ )
	: mDevice(device_)
{
	init( samplerInfo_, imageViewInfo_ );
}

// ----------------------------------------------------------------------

Texture::Texture( const ::vk::Device & device_, const ::vk::Image & image_ )
	: mDevice( device_ )
{
	init( getDefaultSamplerCreateInfo(), getDefaultImageViewCreateInfo(image_) );
}

// ----------------------------------------------------------------------

void of::vk::Texture::init( const ::vk::SamplerCreateInfo & samplerInfo_, const ::vk::ImageViewCreateInfo& imageViewInfo_){

	const_cast<::vk::ImageView&>( mImageView ) = mDevice.createImageView( imageViewInfo_ );
	const_cast<::vk::Sampler&>( mSampler ) = mDevice.createSampler( samplerInfo_ );

}

// ----------------------------------------------------------------------

Texture::~Texture(){

	if ( mDevice ){
		// TODO: find other way to make sure elements are ready 
		// for destruction (none of them must be in use or in flight.)
		mDevice.waitIdle();
		if ( mSampler ){
			mDevice.destroySampler( mSampler );
		}
		if ( mImageView ){
			mDevice.destroyImageView( mImageView );
		}
	}

}


