#include "vk/Context.h"
#include "vk/TransferBatch.h"
#include "vk/ofVkRenderer.h"

using namespace of::vk;

// ------------------------------------------------------------
Context::Context( const Settings & settings )
	: mSettings( settings ){
	if ( mSettings.renderer == nullptr ){
		ofLogFatalError() << "You must specify a renderer for a context.";
		ofExit();
	}

}

// ------------------------------------------------------------

Context::~Context(){
	for ( auto & vf : mVirtualFrames ){
		if ( vf.commandPool ){
			mDevice.destroyCommandPool( vf.commandPool );
		}
		for ( auto & pool : vf.descriptorPools ){
			mDevice.destroyDescriptorPool( pool );
		}
		if ( vf.semaphoreWait ){
			mDevice.destroySemaphore( vf.semaphoreWait );
		}
		if ( vf.semaphoreSignalOnComplete ){
			mDevice.destroySemaphore( vf.semaphoreSignalOnComplete );
		}
		if ( vf.fence ){
			mDevice.destroyFence( vf.fence );
		}
		if ( !vf.frameBuffers.empty() ){
			for ( auto& fb : vf.frameBuffers ){
				mDevice.destroyFramebuffer( fb );
			}
		}
	}
	mVirtualFrames.clear();
	mTransientMemory.reset();
}
  
// ------------------------------------------------------------

void Context::setup(){

	mTransientMemory.setup(mSettings.transientMemoryAllocatorSettings);
	mVirtualFrames.resize(mSettings.transientMemoryAllocatorSettings.frameCount);

	mDescriptorPoolSizes.fill(0);
	mAvailableDescriptorCounts.fill(0);

	for ( auto &f : mVirtualFrames ){
		if ( mSettings.renderToSwapChain ){
			f.semaphoreWait = mDevice.createSemaphore( {} );  // this semaphore should be owned by the swapchain.
			f.semaphoreSignalOnComplete = mDevice.createSemaphore( {} );
		} else{
			f.semaphoreWait = nullptr;
		}
		f.fence = mDevice.createFence( { ::vk::FenceCreateFlagBits::eSignaled } );	/* Fence starts as "signaled" */
		f.commandPool = mDevice.createCommandPool( { ::vk::CommandPoolCreateFlagBits::eTransient } );
	}

	mCurrentVirtualFrame = mVirtualFrames.size() ^ 1;
	
	// self-dependency
	mSourceContext = this;
}

// ------------------------------------------------------------

const ::vk::Framebuffer& Context::createFramebuffer( const ::vk::FramebufferCreateInfo& createInfo ){
	auto & fbos = mVirtualFrames[mCurrentVirtualFrame].frameBuffers;

	// Create a framebuffer for the current virtual frame, and link it to the current swapchain images.
	auto fb = mDevice.createFramebuffer( createInfo );

	mVirtualFrames[mCurrentVirtualFrame].frameBuffers.emplace_back( fb );

	return mVirtualFrames[mCurrentVirtualFrame].frameBuffers.back();
}

// ------------------------------------------------------------

void Context::waitForFence(){
	auto fenceWaitResult = mDevice.waitForFences( { getFence() }, VK_TRUE, 100'000'000 );

	if ( fenceWaitResult != ::vk::Result::eSuccess ){
		ofLogError() << "Context: Waiting for fence takes too long: " << ::vk::to_string( fenceWaitResult );
	}
}

// ------------------------------------------------------------

void Context::begin(){

	// Move to the next available virtual frame
	swap();

	// Wait until fence for current virtual frame has been reached by GPU, which 
	// indicates that all virtual frame resource access has completed, and that
	// all resources of this virtual frame may be reset or re-used.
	//
	waitForFence();

	mDevice.resetFences( { getFence() } );

	// Free old command buffers - this is necessary since otherwise you end up with 
	// leaking them.
	if ( !mVirtualFrames[mCurrentVirtualFrame].commandBuffers.empty() ){
		mDevice.freeCommandBuffers( mVirtualFrames[mCurrentVirtualFrame].commandPool, mVirtualFrames[mCurrentVirtualFrame].commandBuffers );
		mVirtualFrames[mCurrentVirtualFrame].commandBuffers.clear();
	}
	
	mDevice.resetCommandPool( mVirtualFrames[mCurrentVirtualFrame].commandPool, ::vk::CommandPoolResetFlagBits::eReleaseResources );

	mTransientMemory.free();

	// clear old frame buffer attachments
	for ( auto & fb : mVirtualFrames[mCurrentVirtualFrame].frameBuffers ){
		mDevice.destroyFramebuffer( fb );
	}
	mVirtualFrames[mCurrentVirtualFrame].frameBuffers.clear();

	// re-create descriptor pool for current virtual frame if necessary
	updateDescriptorPool();

}

// ------------------------------------------------------------

void Context::end(){

	auto & frame = mVirtualFrames[mCurrentVirtualFrame];

	const ::vk::Semaphore * waitSemaphore = nullptr;
	const ::vk::Semaphore * signalSemaphore = nullptr;

	if ( mSettings.renderToSwapChain ){
		waitSemaphore   = &frame.semaphoreWait;
		signalSemaphore = &frame.semaphoreSignalOnComplete;
	} else{
		// waitSemaphore = &mSourceContext->getSemaphoreSignalOnComplete();
	}

	::vk::SubmitInfo submitInfo;
	// TODO: the number of array elements must correspond to the number of wait semaphores, as each 
	//       mask specifies what the semaphore is waiting for.
	std::array<::vk::PipelineStageFlags, 1> wait_dst_stage_mask = { ::vk::PipelineStageFlagBits::eColorAttachmentOutput };


	submitInfo
		.setWaitSemaphoreCount( ( waitSemaphore ? 1 : 0) )  // set to zero if waitSemaphore was not set
		.setPWaitSemaphores(   waitSemaphore )
		.setPWaitDstStageMask( wait_dst_stage_mask.data() )
		.setCommandBufferCount( frame.commandBuffers.size() )
		.setPCommandBuffers(    frame.commandBuffers.data() )
		.setSignalSemaphoreCount( ( signalSemaphore ? 1 : 0 ) )
		.setPSignalSemaphores( signalSemaphore )
		;

	mSettings.renderer->submit(mSettings.vkQueueIndex, { submitInfo }, getFence() );
}


// ------------------------------------------------------------

void Context::swap(){
	mCurrentVirtualFrame = ( mCurrentVirtualFrame + 1 ) % mSettings.transientMemoryAllocatorSettings.frameCount;
	mTransientMemory.swap();
}

// ------------------------------------------------------------

const::vk::DescriptorSet Context::getDescriptorSet( uint64_t descriptorSetHash, size_t setId, const ::vk::DescriptorSetLayout & setLayout_, const std::vector<DescriptorSetData_t::DescriptorData_t> & descriptors ){

	auto & currentVirtualFrame = mVirtualFrames[mCurrentVirtualFrame];
	auto & descriptorSetCache = currentVirtualFrame.descriptorSetCache;

	auto cachedDescriptorSetIt = descriptorSetCache.find( descriptorSetHash );

	if ( cachedDescriptorSetIt != descriptorSetCache.end() ){
		return cachedDescriptorSetIt->second;
	}

	// ----------| Invariant: descriptor set has not been found in the cache for the current frame.

	::vk::DescriptorSet allocatedDescriptorSet = nullptr;

	// find out required pool sizes for this descriptor set

	std::array<uint32_t, VK_DESCRIPTOR_TYPE_RANGE_SIZE> requiredPoolSizes;
	requiredPoolSizes.fill( 0 );

	for ( const auto & d : descriptors ){
		uint32_t arrayIndex = uint32_t( d.type );
		++requiredPoolSizes[arrayIndex];
	}

	// First, we have to figure out if the current descriptor pool has enough space available 
	// over all descriptor types to allocate the desciptors needed to fill the desciptor set requested.


	// perform lexicographical compare, i.e. compare pairs of corresponding elements 
	// until the first mismatch 
	bool poolLargeEnough = ( mAvailableDescriptorCounts >= requiredPoolSizes );

	if ( poolLargeEnough == false ){

		// Allocation cannot be made using current descriptorPool (we're out of descriptors)
		//
		// Allocate a new descriptorpool - and make sure there is enough space to contain
		// all new descriptors.

		std::vector<::vk::DescriptorPoolSize> descriptorPoolSizes;
		descriptorPoolSizes.reserve( requiredPoolSizes.size() );
		for ( size_t i = VK_DESCRIPTOR_TYPE_BEGIN_RANGE; i != VK_DESCRIPTOR_TYPE_BEGIN_RANGE + VK_DESCRIPTOR_TYPE_RANGE_SIZE; ++i ){
			if ( requiredPoolSizes[i] != 0 ){
				descriptorPoolSizes.emplace_back( ::vk::DescriptorType( i ), requiredPoolSizes[i] );
			}
		}

		::vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo;
		descriptorPoolCreateInfo
			.setFlags( ::vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet )
			.setMaxSets( 1 )
			.setPoolSizeCount( descriptorPoolSizes.size() )
			.setPPoolSizes( descriptorPoolSizes.data() )
			;

		auto descriptorPool = mDevice.createDescriptorPool( descriptorPoolCreateInfo );

		mVirtualFrames[mCurrentVirtualFrame].descriptorPools.push_back( descriptorPool );

		// this means all descriptor pools are dirty and will have to be re-created with 
		// more space to accomodate more descriptor sets.
		mDescriptorPoolsDirty = -1;

		for ( size_t i = VK_DESCRIPTOR_TYPE_BEGIN_RANGE; i != VK_DESCRIPTOR_TYPE_BEGIN_RANGE + VK_DESCRIPTOR_TYPE_RANGE_SIZE; ++i ){
			mDescriptorPoolSizes[i] += requiredPoolSizes[i];
			// Update number of available descriptors from descriptor pool. 
			mAvailableDescriptorCounts[i] += requiredPoolSizes[i];
		}

		// Increase maximum number of sets for allocation from descriptor pool
		mDescriptorPoolMaxSets += 1;

	}

	// ---------| invariant: currentVirtualFrame.descriptorPools.back() contains a pool large enough to allocate our descriptor set from

	// we are able to allocate from the current descriptor pool
	
	auto allocInfo = ::vk::DescriptorSetAllocateInfo();
	allocInfo
		.setDescriptorPool( currentVirtualFrame.descriptorPools.back() )
		.setDescriptorSetCount( 1 )
		.setPSetLayouts( &setLayout_ )
		;

	allocatedDescriptorSet = mDevice.allocateDescriptorSets( allocInfo ).front();

	// decrease number of available descriptors from the pool 
	for ( size_t i = VK_DESCRIPTOR_TYPE_BEGIN_RANGE; i != VK_DESCRIPTOR_TYPE_BEGIN_RANGE + VK_DESCRIPTOR_TYPE_RANGE_SIZE; ++i ){
		mAvailableDescriptorCounts[i] -= requiredPoolSizes[i];
	}

	// Once desciptor sets have been allocated, we need to write to them using write desciptorset
	// to initialise them.

	std::vector<::vk::WriteDescriptorSet> writeDescriptorSets;

	writeDescriptorSets.reserve( descriptors.size() );

	for ( const auto & d : descriptors ){

		// ( we cast address to sampler, as the layout of DescriptorData_t::sampler and
		// DescriptorData_t::imageLayout is the same as if we had 
		// a "proper" ::vk::descriptorImageInfo )
		const ::vk::DescriptorImageInfo* descriptorImageInfo = reinterpret_cast<const ::vk::DescriptorImageInfo*>( &d.sampler );
		// same with bufferInfo
		const ::vk::DescriptorBufferInfo* descriptorBufferInfo = reinterpret_cast<const ::vk::DescriptorBufferInfo*>( &d.buffer );

		writeDescriptorSets.emplace_back(
			allocatedDescriptorSet,         // dstSet
			d.bindingNumber,                // dstBinding
			d.arrayIndex,                   // dstArrayElement
			1,                              // descriptorCount
			d.type,                         // descriptorType
			descriptorImageInfo,            // pImageInfo 
			descriptorBufferInfo,           // pBufferInfo
			nullptr                         // 
		);
		
	}

	mDevice.updateDescriptorSets( writeDescriptorSets, nullptr );

	// Now store the newly allocated descriptor set in this frame's descriptor set cache
	// so it may be re-used.
	descriptorSetCache[descriptorSetHash] = allocatedDescriptorSet;

	return allocatedDescriptorSet;
}

// ------------------------------------------------------------

void Context::updateDescriptorPool(){

	// If current virtual frame descriptorpool is dirty,
	// re-allocate frame descriptorpool based on total number
	// of descriptorsets enumerated in mDescriptorPoolSizes
	// and mDescriptorPoolMaxsets.

	if ( 0 == ( ( 1ULL << mCurrentVirtualFrame ) & mDescriptorPoolsDirty ) ){
		return;
	}

	// --------| invariant: Descriptor Pool for the current virtual frame is dirty.

	// Destroy all cached descriptorSets for the current virtual frame, if any
	mVirtualFrames[mCurrentVirtualFrame].descriptorSetCache.clear();

	// Destroy all descriptor pools for the current virtual frame, if any.
	// This will free any descriptorSets allocated from these pools.
	for ( const auto& d : mVirtualFrames[mCurrentVirtualFrame].descriptorPools ){
		mDevice.destroyDescriptorPool( d );
	}
	mVirtualFrames[mCurrentVirtualFrame].descriptorPools.clear();

	// Re-create descriptor pool for current virtual frame
	// based on number of max descriptor pool count

	std::vector<::vk::DescriptorPoolSize> descriptorPoolSizes;
	descriptorPoolSizes.reserve( VK_DESCRIPTOR_TYPE_RANGE_SIZE );
	for ( size_t i = VK_DESCRIPTOR_TYPE_BEGIN_RANGE; i != VK_DESCRIPTOR_TYPE_BEGIN_RANGE + VK_DESCRIPTOR_TYPE_RANGE_SIZE; ++i ){
		if ( mDescriptorPoolSizes[i] != 0 ){
			descriptorPoolSizes.emplace_back( ::vk::DescriptorType( i ), mDescriptorPoolSizes[i] );
		}
	}

	if ( descriptorPoolSizes.empty() ){
		return;
		// TODO: this needs a fix: happens when method is called for the very first time
		// with no pool sizes known.
	}

	::vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo;
	descriptorPoolCreateInfo
		.setMaxSets( mDescriptorPoolMaxSets )
		.setPoolSizeCount( descriptorPoolSizes.size() )
		.setPPoolSizes( descriptorPoolSizes.data() )
		;

	mVirtualFrames[mCurrentVirtualFrame].descriptorPools.emplace_back( mDevice.createDescriptorPool( descriptorPoolCreateInfo ) );

	// Reset number of available descriptors for allocation from 
	// main descriptor pool
	mAvailableDescriptorCounts = mDescriptorPoolSizes;

	// Mark descriptor pool for this frame as not dirty
	mDescriptorPoolsDirty ^= ( 1ULL << mCurrentVirtualFrame );

}

// ------------------------------------------------------------

std::vector<BufferRegion> Context::storeBufferDataCmd( const std::vector<TransferSrcData>& dataVec, BufferAllocator& targetAllocator ){
	std::vector<BufferRegion> resultBuffers;

	auto copyRegions = stageBufferData( dataVec, targetAllocator );
	resultBuffers.reserve( copyRegions.size() );

	const auto targetBuffer = targetAllocator.getBuffer();

	for ( size_t i = 0; i != copyRegions.size(); ++i ){
		const auto &region = copyRegions[i];
		const auto &srcData = dataVec[i];
		BufferRegion bufRegion;
		bufRegion.buffer = targetBuffer;
		bufRegion.numElements = srcData.numElements;
		bufRegion.offset = region.dstOffset;
		bufRegion.range = region.size;
		resultBuffers.push_back( std::move( bufRegion ) );
	}

	::vk::DeviceSize firstOffset = copyRegions.front().dstOffset;
	::vk::DeviceSize totalStaticRange = ( copyRegions.back().dstOffset + copyRegions.back().size ) - firstOffset;

	::vk::CommandBuffer cmd = allocateCommandBuffer( ::vk::CommandBufferLevel::ePrimary );

	cmd.begin( { ::vk::CommandBufferUsageFlagBits::eOneTimeSubmit } );
	{

		::vk::BufferMemoryBarrier srcPrepareBarrier;
		srcPrepareBarrier
			.setSrcAccessMask( ::vk::AccessFlagBits::eHostWrite )			       // finish host stage     : host write
			.setDstAccessMask( ::vk::AccessFlagBits::eTransferRead )			   // before transfer stage : transfer-read
			.setSrcQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
			.setDstQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
			.setBuffer( mTransientMemory.getBuffer() )
			.setOffset( firstOffset )
			.setSize( totalStaticRange )
			;

		::vk::BufferMemoryBarrier targetPrepareBarrier;
		targetPrepareBarrier
			.setSrcAccessMask( {} )                                               // finish host stage     : no prior access
			.setDstAccessMask( ::vk::AccessFlagBits::eTransferWrite )			  // before transfer stage  : transfer-write
			.setSrcQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
			.setDstQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
			.setBuffer( targetAllocator.getBuffer() )
			.setOffset( firstOffset )
			.setSize( totalStaticRange )
			;

		// Issue two pipeline barriers to make sure that:
		//
		// + Source (transient)   buffer is in transfer read stage
		// + Target (device-only) buffer is in transfer write stage

		cmd.pipelineBarrier( ::vk::PipelineStageFlagBits::eHost, ::vk::PipelineStageFlagBits::eTransfer, {}, {}, { srcPrepareBarrier, targetPrepareBarrier }, {} );

		cmd.copyBuffer( mTransientMemory.getBuffer(), targetAllocator.getBuffer(), copyRegions );

		::vk::BufferMemoryBarrier bufferTransferBarrier;
		bufferTransferBarrier
			.setSrcAccessMask( ::vk::AccessFlagBits::eTransferWrite )
			.setDstAccessMask( ::vk::AccessFlagBits::eShaderRead | ::vk::AccessFlagBits::eMemoryRead | ::vk::AccessFlagBits::eShaderWrite )
			.setSrcQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
			.setDstQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
			.setBuffer( targetAllocator.getBuffer() )
			.setOffset( firstOffset )
			.setSize( totalStaticRange )
			;

		// Issue pipeline barrier to make sure that:
		//
		// Transfer write has completed before any shader operations access the data

		cmd.pipelineBarrier(
			::vk::PipelineStageFlagBits::eTransfer ,
			::vk::PipelineStageFlagBits::eFragmentShader | ::vk::PipelineStageFlagBits::eVertexShader | ::vk::PipelineStageFlagBits::eComputeShader,
			::vk::DependencyFlagBits(),
			{}, /* no fence */
			{ bufferTransferBarrier }, /* buffer barriers */
			{}                         /* image barriers */
		);
	}
	cmd.end();

	// Submit copy command buffer to current context
	// This needs to happen before first draw calls are submitted for the frame.
	submit( std::move( cmd ) );

	return resultBuffers;
}

// ------------------------------------------------------------

std::shared_ptr<::vk::Image> Context::storeImageCmd( const ImageTransferSrcData& data, ImageAllocator& targetImageAllocator ){

	/*
	
	These are the steps needed to upload an image:

	1. load image pixels and image subresource pixels
	2. create vkImage
	3. create vkImageView
	4. aggregate layers and mipmap data into structure of
	   VkBufferImageCopy
	5. copy layers and mipmap data into contiguous RAM memory = ImageMemBlob
	6. copy ImageMemBlob to (transient) staging memory
	7. use command-buffer copy to copy image
		7.1 layout transition barrier of image for copy
		7.2 vkCmdCopyBuffer image
		7.3 layout transition of image for use by shader (shader read)
		7.4 execute command buffer
	
	*/

	auto image = std::shared_ptr<::vk::Image>( new ::vk::Image(), [device = mDevice]( ::vk::Image * lhs ){
		if ( lhs ){
			if ( *lhs ){
				device.destroyImage( *lhs );
			}
			delete lhs;
		}
	} );


	::vk::ImageCreateInfo imageCreateInfo;
	imageCreateInfo
		.setImageType( data.imageType )
		.setFormat( data.format )
		.setExtent( data.extent )
		.setMipLevels( data.mipLevels )
		.setArrayLayers( data.arrayLayers )
		.setSamples( data.samples )
		.setTiling( ::vk::ImageTiling::eOptimal )
		.setUsage( ::vk::ImageUsageFlagBits::eSampled | ::vk::ImageUsageFlagBits::eTransferDst )
		.setSharingMode( ::vk::SharingMode::eExclusive )
		.setQueueFamilyIndexCount( 0 )
		.setPQueueFamilyIndices( nullptr )
		.setInitialLayout( ::vk::ImageLayout::eUndefined )
		;

	*image = mDevice.createImage( imageCreateInfo );
	
	::vk::DeviceSize numBytes = mDevice.getImageMemoryRequirements( *image ).size;
	
	::vk::DeviceSize dstOffset = 0;
	if ( targetImageAllocator.allocate( numBytes, dstOffset ) ){
		mDevice.bindImageMemory( *image, targetImageAllocator.getDeviceMemory(), dstOffset );
	} else{
		ofLogError() << "Image Allocation failed.";
		image.reset();
		return image;
	}

	// --------| invariant: target allocation successful

	::vk::DeviceSize transientBufferOffset = 0;	// location where image data is stored temporarily in transient buffer
	void * pData;
	if ( mTransientMemory.allocate( data.numBytes, transientBufferOffset )
		&& mTransientMemory.map(pData)){
		memcpy( pData, data.pData, data.numBytes );
	} else{
		ofLogError() << "Transient Image data allocation failed.";
		image.reset();
		return image;
	}

	::vk::ImageSubresourceLayers subresourceLayers;
	subresourceLayers
		.setAspectMask(::vk::ImageAspectFlagBits::eColor)
		.setMipLevel( 0 )
		.setBaseArrayLayer( 0)
		.setLayerCount( 1 )
		;

	::vk::BufferImageCopy bufferImageCopy;
	bufferImageCopy
		.setBufferOffset( transientBufferOffset )    // must be a multiple of four 
		.setBufferRowLength( data.extent.width )     // must be 0, or greater or equal to imageExtent.width 
		.setBufferImageHeight( data.extent.height )
		.setImageSubresource( subresourceLayers )
		.setImageOffset( {0,0,0} )
		.setImageExtent( data.extent )
		;

	::vk::ImageSubresourceRange subresourceRange;
	subresourceRange
		.setAspectMask( ::vk::ImageAspectFlagBits::eColor )
		.setBaseMipLevel( 0 )
		.setLevelCount( 1 )
		.setBaseArrayLayer( 0 )
		.setLayerCount( 1 )
		;

	::vk::BufferMemoryBarrier bufferTransferBarrier;
	bufferTransferBarrier
		.setSrcAccessMask( ::vk::AccessFlagBits::eHostWrite )	   // after host write
		.setDstAccessMask( ::vk::AccessFlagBits::eTransferRead )   // ready buffer for transfer read
		.setSrcQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
		.setDstQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
		.setBuffer( mTransientMemory.getBuffer() )
		.setOffset( bufferImageCopy.bufferOffset )
		.setSize( numBytes )
		;

	::vk::ImageMemoryBarrier imageLayoutToTransferDstOptimal;
	imageLayoutToTransferDstOptimal
		.setSrcAccessMask( { } )								   // no prior access
		.setDstAccessMask( ::vk::AccessFlagBits::eTransferWrite )  // ready image for transferwrite
		.setOldLayout( ::vk::ImageLayout::eUndefined )			   // from don't care
		.setNewLayout( ::vk::ImageLayout::eTransferDstOptimal )	   // to transfer destination optimal
		.setSrcQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
		.setDstQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
		.setImage( *image )
		.setSubresourceRange( subresourceRange )
		;

	
	::vk::CommandBuffer cmd = allocateCommandBuffer( ::vk::CommandBufferLevel::ePrimary );

	cmd.begin( { ::vk::CommandBufferUsageFlagBits::eOneTimeSubmit } );

	cmd.pipelineBarrier (
		::vk::PipelineStageFlagBits::eHost, 
		::vk::PipelineStageFlagBits::eTransfer,
		{},
		{},
		{ bufferTransferBarrier },				   // buffer: host write -> transfer read
		{ imageLayoutToTransferDstOptimal }        // image: prepare for transfer write
	);
	
	cmd.copyBufferToImage( mTransientMemory.getBuffer(), *image, ::vk::ImageLayout::eTransferDstOptimal, bufferImageCopy );

	::vk::ImageMemoryBarrier imageStagingBarrier;
	imageStagingBarrier
		.setSrcAccessMask( ::vk::AccessFlagBits::eTransferWrite )  // after transfer write
		.setDstAccessMask( ::vk::AccessFlagBits::eShaderRead )	   // ready image for shader read
		.setOldLayout( ::vk::ImageLayout::eTransferDstOptimal )	   // from transfer dst optimal 
		.setNewLayout( ::vk::ImageLayout::eShaderReadOnlyOptimal ) // to shader readonly optimal
		.setSrcQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
		.setDstQueueFamilyIndex( VK_QUEUE_FAMILY_IGNORED )
		.setImage( *image )
		.setSubresourceRange( subresourceRange )
		;

	cmd.pipelineBarrier(
		::vk::PipelineStageFlagBits::eTransfer,
		::vk::PipelineStageFlagBits::eVertexShader | ::vk::PipelineStageFlagBits::eComputeShader,
		{},
		{},
		{},
		{ imageStagingBarrier }
	);

	cmd.end();
	
	// Submit copy command buffer to current context
	// This needs to happen before first draw calls are submitted for the frame.
	submit( std::move( cmd ) );

	return image;
}

// ------------------------------------------------------------

void Context::addContextDependency( Context * ctx ){
	mSourceContext = ctx;
}

// ------------------------------------------------------------
