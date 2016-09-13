#include "vk/Texture.h"
#include "ofVkRenderer.h"

void of::vk::Texture::load( const ofPixels & pix_ ){
	auto renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

	// get device
	mDevice = renderer->getVkDevice();

	// get command pool
	const auto& cmdPool = renderer->getSetupCommandPool();

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
	mTexData.tex_width = pix_.getWidth();

	::vk::Format format = ::vk::Format::eR8G8B8A8Unorm;

	::vk::Extent3D extent  {
		mTexData.tex_width,
		mTexData.tex_height,
		1
	};

	::vk::ImageCreateInfo imageCreateInfo;
	imageCreateInfo
		//.setFlags( ::vk::ImageCreateFlagBits::eMutableFormat )
		.setImageType( ::vk::ImageType::e2D )
		.setFormat( format )
		.setExtent( extent)
		.setMipLevels( 1 )
		.setArrayLayers( 1)
		.setSamples( ::vk::SampleCountFlagBits::e1 )
		.setTiling( ::vk::ImageTiling::eLinear )
		.setUsage( ::vk::ImageUsageFlagBits::eSampled)
		.setSharingMode( ::vk::SharingMode::eExclusive)
		.setQueueFamilyIndexCount( 0)
		.setPQueueFamilyIndices( nullptr )
		.setInitialLayout( ::vk::ImageLayout::ePreinitialized)
		;

	mTexData.image = mDevice.createImage( imageCreateInfo );

	// now that we have created an abstract image view
	// we want to associate it with some memory.
	// for this, we first have to allocate some memory.
	// But before we can allocate memory, we need to know
	// what kind of memory to allocate.

	::vk::MemoryRequirements memReq = mDevice.getImageMemoryRequirements( mTexData.image );

	::vk::MemoryAllocateInfo allocInfo;

	renderer->getMemoryAllocationInfo( 
		memReq
		, ::vk::MemoryPropertyFlagBits::eHostVisible | ::vk::MemoryPropertyFlagBits::eHostCoherent
		, allocInfo );

	// allocate device memory, and point to the new memory object in mTexData
	mTexData.mem = mDevice.allocateMemory( allocInfo );

	// TODO: create a VkImageView image view - so we can actually sample this image.
	// the view deals with swizzles - and also with mipmamplevels. It is also 
	// defines the subresource range for the image.

	// attach deviceMemory to img
	mDevice.bindImageMemory( mTexData.image, mTexData.mem, 0 );
	
	{
		// write out pixels to device memory
		void * pData = mDevice.mapMemory( mTexData.mem, 0, allocInfo.allocationSize );

		// write to mapped memory - as this is coherent, write will 
		// be visible to GPU without need to flush
		memcpy( pData, pix_.getData(), pix_.getTotalBytes() );
		mDevice.unmapMemory( mTexData.mem );
	}

	// First, we need a command buffer where we can record a pipeline barrier command into.
	// This command - the pipeline barrier with an image barrier - will transfer the 
	// image resource from its original layout to a layout that the gpu can use for 
	// sampling.
	::vk::CommandBuffer texCmdBuffer = nullptr;
	{
	
		::vk::CommandBufferAllocateInfo cmdBufAllocInfo;
		cmdBufAllocInfo
			.setCommandPool        ( cmdPool )
			.setLevel              ( ::vk::CommandBufferLevel::ePrimary )
			.setCommandBufferCount ( 1 )
			;
		texCmdBuffer = mDevice.allocateCommandBuffers( cmdBufAllocInfo ).front();
	}

	texCmdBuffer.begin( { ::vk::CommandBufferUsageFlagBits::eOneTimeSubmit } );

	// create image memory barrier to transfer image from preinitialised to sampler read optimal

	::vk::ImageSubresourceRange subresourceRange;
	subresourceRange
		.setAspectMask     ( ::vk::ImageAspectFlagBits::eColor )
		.setBaseMipLevel   ( 0 )
		.setLevelCount     ( 1 )
		.setBaseArrayLayer ( 0 )
		.setLayerCount     ( 1 )
		;

	::vk::ImageMemoryBarrier imageStagingBarrier;
	imageStagingBarrier
		.setSrcAccessMask( ::vk::AccessFlagBits::eHostWrite )
		.setDstAccessMask( ::vk::AccessFlagBits::eShaderRead)
		.setOldLayout( ::vk::ImageLayout::ePreinitialized)
		.setNewLayout( ::vk::ImageLayout::eShaderReadOnlyOptimal)
		.setSrcQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
		.setDstQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
		.setImage( mTexData.image )
		.setSubresourceRange( subresourceRange )
		;


	//vkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0, 0, 0, 0, 1, &stagingBarrier );
	texCmdBuffer.pipelineBarrier( 
		::vk::PipelineStageFlagBits::eTopOfPipe, 
		::vk::PipelineStageFlagBits::eTopOfPipe, 
		::vk::DependencyFlagBits(),
		{}, 
		{},
		{ imageStagingBarrier } );
	
	texCmdBuffer.end();

	// Now we want to submit this command buffer.
	// So we have to fill the fence and semaphore paramters for 
	// command buffer submit info with defaults

	::vk::Fence fence = mDevice.createFence( {} );

	::vk::SubmitInfo submitInfo;
	submitInfo
		.setCommandBufferCount( 1 )
		.setPCommandBuffers( &texCmdBuffer )
		;

	// Allright - submit the command buffer.
	// note that the fence is optional - but we use it to find out when the command buffer that 
	// we just added has been executed. 
	// That way we know when it's safe to free that command buffer.
	// we could also use the fence to figure out if the image has finished uploading.

	queue.submit( submitInfo, fence );

	mDevice.waitForFences( { fence }, VK_TRUE, 100'000'000 );

	// transfer complete
	mDevice.destroyFence( fence );

	// create an image view which may or may not get sampled.

	VkImageViewCreateFlags imageViewCreateFlags = VK_IMAGE_USAGE_SAMPLED_BIT;
	
	::vk::ImageViewCreateInfo imageViewCreateInfo;
	imageViewCreateInfo
		.setImage( mTexData.image )
		.setViewType( ::vk::ImageViewType::e2D )
		.setFormat( ::vk::Format::eR8G8B8A8Unorm )
		.setComponents( {::vk::ComponentSwizzle::eR, ::vk::ComponentSwizzle::eG, ::vk::ComponentSwizzle::eB, ::vk::ComponentSwizzle::eA} )
		.setSubresourceRange( { ::vk::ImageAspectFlagBits::eColor,0,1,0,1 } )
		;

	mTexData.view = mDevice.createImageView( imageViewCreateInfo );

	::vk::SamplerCreateInfo samplerInfo;
	samplerInfo
		.setMagFilter( ::vk::Filter::eLinear )
		.setMinFilter( ::vk::Filter::eLinear )
		.setMipmapMode( ::vk::SamplerMipmapMode::eLinear )
		.setAddressModeU( ::vk::SamplerAddressMode::eRepeat )
		.setAddressModeV( ::vk::SamplerAddressMode::eRepeat )
		.setAddressModeW( ::vk::SamplerAddressMode::eRepeat )
		.setMipLodBias( 0.f )
		.setAnisotropyEnable( VK_FALSE )
		.setMaxAnisotropy( 0.f )
		.setCompareEnable( VK_FALSE )
		.setCompareOp( ::vk::CompareOp::eLess )
		.setMinLod( 0.f )
		.setMaxLod( 1.f )
		.setBorderColor( ::vk::BorderColor::eFloatTransparentBlack)
		.setUnnormalizedCoordinates( VK_FALSE )
		;

	mTexData.sampler = mDevice.createSampler( samplerInfo );

}
