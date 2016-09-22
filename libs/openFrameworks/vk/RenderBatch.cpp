#include "vk/RenderBatch.h"
#include "vk/DrawCommand.h"
#include "vk/spooky/SpookyV2.h"
#include "vk/Shader.h"
#include "vk/ofVkRenderer.h"

// ------------------------------------------------------------
of::RenderContext::RenderContext( const Settings & settings )
: mSettings(settings)
{
	mTransientMemory = std::make_unique<of::vk::Allocator>( settings.transientMemoryAllocatorSettings );
	mVirtualFrames.resize( mSettings.transientMemoryAllocatorSettings.frameCount );
	mDescriptorPoolSizes.fill( 0 );
	mAvailableDescriptorCounts.fill( 0 );
}

// ------------------------------------------------------------

::vk::CommandPool & of::RenderContext::getCommandPool(){
	return mVirtualFrames.at(mCurrentVirtualFrame).commandPool;
}


// ------------------------------------------------------------

void of::RenderContext::setup(){
	for ( auto &f : mVirtualFrames ){
		f.semaphoreImageAcquired = mDevice.createSemaphore( {} );
		f.semaphoreRenderComplete = mDevice.createSemaphore( {} );
		f.fence = mDevice.createFence( { ::vk::FenceCreateFlagBits::eSignaled } );	/* Fence starts as "signaled" */
		f.commandPool = mDevice.createCommandPool( { ::vk::CommandPoolCreateFlagBits::eResetCommandBuffer } );
	}
	mTransientMemory->setup();
}

// ------------------------------------------------------------

void of::RenderContext::begin(){
	mTransientMemory->free();
	// re-create descriptor pool for current virtual frame if necessary
	updateDescriptorPool();
}

// ------------------------------------------------------------

void of::RenderContext::swap(){
	mCurrentVirtualFrame = ( mCurrentVirtualFrame + 1 ) % mSettings.transientMemoryAllocatorSettings.frameCount;
	mTransientMemory->swap();
}

// ------------------------------------------------------------

const::vk::DescriptorSet of::RenderContext::getDescriptorSet( uint64_t descriptorSetHash, size_t setId, const of::DrawCommand & drawCommand ){
	
	auto & currentVirtualFrame = mVirtualFrames[mCurrentVirtualFrame];
	auto & descriptorSetCache = currentVirtualFrame.descriptorSetCache;

	auto cachedDescriptorSetIt = descriptorSetCache.find( descriptorSetHash );

	if ( cachedDescriptorSetIt != descriptorSetCache.end() ){
		return cachedDescriptorSetIt->second;
	}

	// ----------| Invariant: descriptor set has not been found in the cache for the current frame.

	::vk::DescriptorSet allocatedDescriptorSet = nullptr;

	auto & descriptors = drawCommand.getDescriptorSetData(setId).descriptorBindings;

	// find out required pool sizes for this descriptor set

	std::array<uint32_t, VK_DESCRIPTOR_TYPE_RANGE_SIZE> requiredPoolSizes;
	requiredPoolSizes.fill( 0 );

	for ( const auto & d : descriptors ){
		uint32_t arrayIndex =  uint32_t(d.type);
		++requiredPoolSizes[arrayIndex];
	}

	
	// First, we have to figure out if the current descriptor pool has enough space available 
	// over all descriptor types to allocate the desciptors needed to fill the desciptor set requested.


	// perform lexicographical compare, i.e. compare pairs of corresponding elements 
	// until the first mismatch 
	bool poolLargeEnough = ( mAvailableDescriptorCounts >= requiredPoolSizes );

	if ( poolLargeEnough == false){

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
	auto & setLayout = *( drawCommand.getInfo().pipeline.getShader()->getDescriptorSetLayout(setId));
	auto allocInfo = ::vk::DescriptorSetAllocateInfo();
	allocInfo
		.setDescriptorPool( currentVirtualFrame.descriptorPools.back() )
		.setDescriptorSetCount( 1 )
		.setPSetLayouts( &setLayout )
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

void of::RenderContext::updateDescriptorPool(){

	// If current virtual frame descriptorpool is dirty,
	// re-allocate frame descriptorpool based on total number
	// of descriptorsets enumerated in mDescriptorPoolSizes
	// and mDescriptorPoolMaxsets.

	if ( 0 == (( 1ULL << mCurrentVirtualFrame ) & mDescriptorPoolsDirty) ){
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
		//!TODO: this needs a fix: happens when method is called for the very first time
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
of::RenderBatch::RenderBatch( RenderContext & rpc )
	:mRenderContext( &rpc ){
	mRenderContext->begin();
}

// ------------------------------------------------------------

void of::RenderBatch::draw( const of::DrawCommand& dc_ ){

	// local copy of draw command.
	of::DrawCommand dc = dc_;

	//!TODO: commit draw command memory to gpu-update
	// this will update dynamic offsets as a side-effect, 
	// and will also update the buffer 
	// for the bindings affected.
	dc.commitUniforms( mRenderContext->getAllocator() );
	dc.commitMeshAttributes( mRenderContext->getAllocator() );
	
	mDrawCommands.emplace_back( std::move(dc) );

}

// ----------------------------------------------------------------------

void of::RenderBatch::submit(){
	// submit command buffer 
	// ofLogNotice() << "submit render batch";

	beginCommandBuffer();
	{
		// set dynamic viewport
		::vk::Viewport vp;
		vp.setX( 0 )
			.setY( 0 )
			.setWidth( mRenderContext->getRenderArea().extent.width )
			.setHeight( mRenderContext->getRenderArea().extent.height )
			.setMinDepth( 0.f )
			.setMaxDepth( 1.f )
			;
		mVkCmd.setViewport( 0, { vp } );
		mVkCmd.setScissor( 0, { mRenderContext->getRenderArea() } );

		processDrawCommands();
	}
	endCommandBuffer();

	::vk::PipelineStageFlags wait_dst_stage_mask = ::vk::PipelineStageFlagBits::eColorAttachmentOutput;
	::vk::SubmitInfo submitInfo;

	submitInfo
		.setWaitSemaphoreCount( 1 )
		.setPWaitSemaphores( &mRenderContext->getImageAcquiredSemaphore() )
		.setPWaitDstStageMask( &wait_dst_stage_mask )
		.setCommandBufferCount( 1 )
		.setPCommandBuffers( &mVkCmd )
		.setSignalSemaphoreCount( 1 )
		.setPSignalSemaphores( &mRenderContext->getSemaphoreRenderComplete() )
		;

	const auto & renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

	renderer->getQueue().submit( { submitInfo }, mRenderContext->getFence() );
}

// ----------------------------------------------------------------------

void of::RenderBatch::processDrawCommands(){

	// first order draw commands

	// order them by renderpass, 
	// then pipeline,
	// then descriptor set usage

	// then process draw commands

	auto & renderPass = mDrawCommands.front().getInfo().pipeline.getRenderPass();
	
	beginRenderPass( renderPass, mRenderContext->getFramebuffer(), mRenderContext->getRenderArea() );


	for ( auto & dc : mDrawCommands ){

		auto & info = const_cast<of::DrawCommandInfo&>( dc.getInfo() );

		// find out pipeline state needed for this draw command

		//info.modifyPipeline().setRenderPass(mVkRenderPass);
		//info.modifyPipeline().setSubPass(mVkSubPassId);

		if ( !mCurrentPipelineState || *mCurrentPipelineState != dc.getInfo().getPipeline() ){
			// look up pipeline in pipeline cache
			// otherwise, create a new pipeline, then bind pipeline.

			mCurrentPipelineState = std::make_unique<of::vk::GraphicsPipelineState>( dc.getInfo().getPipeline() );

			// !TODO: do we need a cleanup/destructor method for this pipeline?
			// pipelines need to be stored inside draw command - and created upfront!

			uint64_t pipelineStateHash = mCurrentPipelineState->calculateHash();

			auto pipelineIt = mPipelineCache.find( pipelineStateHash );

			if ( pipelineIt == mPipelineCache.end() ){
				mPipelineCache[pipelineStateHash] =
					std::shared_ptr<::vk::Pipeline>( ( new ::vk::Pipeline ),
						[device = mRenderContext->mDevice]( ::vk::Pipeline*rhs ){
					if ( rhs ){
						device.destroyPipeline( *rhs );
					}
					delete rhs;
				} );
			}

			*mPipelineCache[pipelineStateHash] = mCurrentPipelineState->createPipeline( mRenderContext->mDevice, mRenderContext->mSettings.pipelineCache);

			mVkCmd.bindPipeline( ::vk::PipelineBindPoint::eGraphics, *mPipelineCache[pipelineStateHash] );
		}

		// ----------| invariant: correct pipeline is bound

		// Match currently bound DescriptorSetLayouts against 
		// dc pipeline DescriptorSetLayouts
		std::vector<::vk::DescriptorSet> boundVkDescriptorSets;
		std::vector<uint32_t> dynamicBindingOffsets;

		const std::vector<uint64_t> & setLayoutKeys = info.getPipeline().getShader()->getDescriptorSetLayoutKeys();

		for ( size_t setId = 0; setId != setLayoutKeys.size(); ++setId ){

			uint64_t setLayoutKey = setLayoutKeys[setId];
			const auto & descriptorSetBindings = dc.getDescriptorSetData( setId ).descriptorBindings;

			// calculate hash of descriptorset, combined with descriptor set sampler state
			uint64_t descriptorSetHash = SpookyHash::Hash64(
				descriptorSetBindings.data(),
				descriptorSetBindings.size() * sizeof( DrawCommand::DescriptorSetData_t::DescriptorData_t ),
				setLayoutKey );

			// Receive a descriptorSet from the renderContext's cache.
			// The renderContext will allocate and initialise a DescriptorSet if none has been found.
			const ::vk::DescriptorSet& descriptorSet = mRenderContext->getDescriptorSet( descriptorSetHash, setId, dc );

			boundVkDescriptorSets.emplace_back( descriptorSet );

			const auto & offsetMap  = dc.getDescriptorSetData( setId ).dynamicBindingOffsets;
			
			dynamicBindingOffsets.reserve( dynamicBindingOffsets.size() + offsetMap.size() );

			// now append dynamic binding offsets for this set to vector of dynamic offsets for this draw call
			std::transform( offsetMap.begin(), offsetMap.end(), std::back_inserter( dynamicBindingOffsets ), [](const std::pair<uint32_t, uint32_t>&rhs){
				return rhs.second;
			} );

		}

		// We always bind the full descriptor set.
		// Bind uniforms (the first set contains the matrices)

		// bind dc descriptorsets to current pipeline descriptor sets
		// make sure dynamic ubos have the correct offsets

		mVkCmd.bindDescriptorSets(
			::vk::PipelineBindPoint::eGraphics,	                           // use graphics, not compute pipeline
			*dc.getInfo().getPipeline().getShader()->getPipelineLayout(), // VkPipelineLayout object used to program the bindings.
			0,                                                             // firstset: first set index (of the above) to bind to - mDescriptorSet[0] will be bound to pipeline layout [firstset]
			boundVkDescriptorSets.size(),                                  // setCount: how many sets to bind
			boundVkDescriptorSets.data(),                                  // the descriptor sets to match up with our mPipelineLayout (need to be compatible)
			dynamicBindingOffsets.size(),                                  // dynamic offsets count how many dynamic offsets
			dynamicBindingOffsets.data()                                   // dynamic offsets for each descriptor
		);


		{

			const auto & vertexOffsets = dc.getVertexOffsets();
			const auto & indexOffsets  = dc.getIndexOffsets();

			const auto & vertexBuffers = dc.getVertexBuffers();
			const auto & indexBuffer   = dc.getIndexBuffer();

			//// Store vertex data using Context.
			//// - this uses Allocator to store mesh data in the current frame' s dynamic memory
			//// Context will return memory offsets into vertices, indices, based on current context memory buffer
			//// 
			// CONSIDER: check if it made sense to cache already stored meshes, 
			////       so that meshes which have already been stored this frame 
			////       may be re-used.
			//storeMesh( mesh_, vertexOffsets, indexOffsets );

			// CONSIDER: cull vertexOffsets which refer to empty vertex attribute data
			//       make sure that a pipeline with the correct bindings is bound to match the 
			//       presence or non-presence of mesh data.

			// Bind vertex data buffers to current pipeline. 
			// The vector indices into bufferRefs, vertexOffsets correspond to [binding numbers] of the currently bound pipeline.
			// See Shader.h for an explanation of how this is mapped to shader attribute locations

			mVkCmd.bindVertexBuffers( 0, vertexBuffers, vertexOffsets );

			if ( indexBuffer.empty() ){
				// non-indexed draw
				mVkCmd.draw( uint32_t( dc.getNumVertices() ), 1, 0, 0 ); //last param was 1
			} else{
				// indexed draw
				mVkCmd.bindIndexBuffer( indexBuffer[0], indexOffsets[0], ::vk::IndexType::eUint32 );
				mVkCmd.drawIndexed( dc.getNumIndices(), 1, 0, 0, 0 ); // last param was 1
			}
		}

	}

	endRenderPass();

}