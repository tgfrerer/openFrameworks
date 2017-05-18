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

const ::vk::SamplerCreateInfo& of::vk::Texture::getDefaultSamplerCreateInfo(){
	static const ::vk::SamplerCreateInfo info = samplerCreateInfoPreset();
	return info;
}

// ----------------------------------------------------------------------

Texture::Texture( const ::vk::Device & device_, const ::vk::Image & image_, const ::vk::SamplerCreateInfo& samplerInfo_)
	: mDevice(device_)
{
	
	init( image_, samplerInfo_ );
}

// ----------------------------------------------------------------------

Texture::Texture( const ::vk::Device & device_, const ::vk::Image & image_ )
	: mDevice( device_ )
{
	init( image_, getDefaultSamplerCreateInfo() );
}

// ----------------------------------------------------------------------

void of::vk::Texture::init( const ::vk::Image & image_, const ::vk::SamplerCreateInfo & samplerInfo_ ){
	::vk::ImageViewCreateInfo imageViewCreateInfo;
	imageViewCreateInfo
		.setImage( image_ )
		.setViewType( ::vk::ImageViewType::e2D )
		.setFormat( ::vk::Format::eR8G8B8A8Unorm )
		.setComponents( { ::vk::ComponentSwizzle::eR, ::vk::ComponentSwizzle::eG, ::vk::ComponentSwizzle::eB, ::vk::ComponentSwizzle::eA } )
		.setSubresourceRange( { ::vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } )
		;

	const_cast<::vk::ImageView&>( mImageView ) = mDevice.createImageView( imageViewCreateInfo );
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


