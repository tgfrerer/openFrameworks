#include "vk/RenderBatch.h"
#include "vk/DrawCommand.h"
#include "vk/spooky/SpookyV2.h"



// ------------------------------------------------------------
of::RenderContext::RenderContext( const Settings & settings ){

}

// ------------------------------------------------------------

void of::RenderContext::begin(){
	
	// re-create descriptor pool for current virtual frame if necessary
	updateDescriptorPool();

}

// ------------------------------------------------------------

void of::RenderContext::end(){
	mCurrentVirtualFrame = ( mCurrentVirtualFrame + 1 ) % mSettings.transientMemoryAllocatorSettings.frameCount;
}

// ------------------------------------------------------------

void of::RenderContext::updateDescriptorPool(){

	// if current virtual frame descriptorpool is dirty,
	// re-allocate frame descriptorpool based on total number
	// of descriptorsets enumerated in mDescriptorPoolSizes
	// and mDescriptorPoolMaxsets.

	if ( (1ULL << mCurrentVirtualFrame) & mDescriptorPoolsDirty ){
		// pool is dirty.

		// destroy all cached descriptorSets for the current virtual frame, if any
		mVirtualFrames[mCurrentVirtualFrame].descriptorSetCache.clear();

		// destroy all overspill pools for the current virtual frame, if any.
		// This will free any descriptorSets allocated from these pools.
		for ( const auto& d : mVirtualFrames[mCurrentVirtualFrame].overSpillPools ){
			mDevice.destroyDescriptorPool( d );
		}
		mVirtualFrames[mCurrentVirtualFrame].overSpillPools.clear();

		// destroy main descriptor pool for current virtual frame.
		mDevice.destroyDescriptorPool( mVirtualFrames[mCurrentVirtualFrame].descriptorPool );

		// re-create descriptor pool for current virtual frame
		// based on number of max descriptor pool count

		::vk::DescriptorPoolCreateInfo descriptorPoolCreateInfo;
		descriptorPoolCreateInfo
			.setMaxSets( mDescriptorPoolMaxSets )
			.setPoolSizeCount( mDescriptorPoolSizes.size() )
			.setPPoolSizes( mDescriptorPoolSizes.data() )
			;

		mVirtualFrames[mCurrentVirtualFrame].descriptorPool = mDevice.createDescriptorPool( descriptorPoolCreateInfo );
	
		// Update number of descriptors available for allocation from the main descriptor pool
		mAvailableDescriptorCounts.fill( 0 );
		for ( const auto ps : mDescriptorPoolSizes ){
			mAvailableDescriptorCounts[reinterpret_cast<const uint32_t&>(ps.type)] += ps.descriptorCount;
		}

		// mark this descriptor pool as not dirty
		mDescriptorPoolsDirty ^= (1ULL << mCurrentVirtualFrame);
	} else{
		// pool is not dirty
	}

}

// ------------------------------------------------------------

of::CommandBufferContext::CommandBufferContext( of::RenderBatch & batch_ )
	:batch( &batch_ ){
	batch->beginCommandBuffer();
}

// ------------------------------------------------------------

of::CommandBufferContext::~CommandBufferContext(){
	batch->endCommandBuffer();
}

// ------------------------------------------------------------

of::RenderPassContext::RenderPassContext( of::CommandBufferContext & cmdCtx_, const ::vk::RenderPass vkRenderPass_, const ::vk::Framebuffer vkFramebuffer_ )
	:batch( cmdCtx_.batch ){
	batch->beginRenderPass(vkRenderPass_, vkFramebuffer_ );
}

// ------------------------------------------------------------

of::RenderPassContext::~RenderPassContext(){
	batch->endRenderPass();
}

// ------------------------------------------------------------

void of::RenderPassContext::draw( const std::unique_ptr<of::DrawCommand>& dc ){
	batch->draw( dc );
}

// ------------------------------------------------------------

uint32_t of::RenderPassContext::nextSubpass(){
	return batch->nextSubPass();
}

// ------------------------------------------------------------


void of::RenderBatch::draw( const std::unique_ptr<of::DrawCommand>& dc ){

	auto info = dc->getInfo();

	// find out pipeline state needed for this draw command
	
	info.getPipeline().setRenderPass(mVkRenderPass);
	info.getPipeline().setSubPass(mVkSubPassId);

	if ( !currentPipelineState || *currentPipelineState != info.getPipelineC() ){
		// look up pipeline in pipeline cache
		// otherwise, create a new pipeline
		// bind pipeline
		//mPipelineManager.bindPipeline( info.pipeline );

		currentPipelineState = std::make_unique<of::vk::GraphicsPipelineState>(info.getPipelineC());
		// pipelines are stored inside draw command
		// as shared_ptr, so that use_count of pointer may be used for garbage collection.
		// !TODO: mCurrentPipeline = dc->getPipeline(mVkRenderPass, mVkSubPassId, currentPipelineState );

		mVkCmd.bindPipeline( ::vk::PipelineBindPoint::eGraphics, *mCurrentPipeline );
	}

	// ----------| invariant: correct pipeline is bound

	// upload dc. ubo state to transient GPU memory - this should return binding offsets.
	
	std::vector<uint32_t> dynamicBindingOffsets;

	for ( const auto & descriptorSetState : info.getDescriptorSetState() ){
	// for each descriptor set, upload data for each ubo binding
	// and add to offsets list.
		for ( const auto& uboBinding : descriptorSetState.uboData ){
			void * pAddr;
			uint64_t offset;
			mRenderContext->mTransientMemory->allocate( uboBinding.second.size(), pAddr, offset, mRenderContext->mCurrentVirtualFrame );
			memcpy( pAddr, uboBinding.second.data(), uboBinding.second.size() );
			dynamicBindingOffsets.push_back( reinterpret_cast<uint32_t&>(offset) );
		}
	}

	// Match currently bound DescriptorSetLayouts against 
	// dc pipeline DescriptorSetLayouts
	std::vector<::vk::DescriptorSet> boundVkDescriptorSets;

	{
		const std::vector<uint64_t> & drawCommandDescriptorSetLayouts = info.getPipelineC().getShader()->getSetLayoutKeys();

		for ( size_t i = 0; i != drawCommandDescriptorSetLayouts.size(); ++i ){

			uint64_t descriptorSetLayoutHash = drawCommandDescriptorSetLayouts[i];
			auto & samplerBindings = info.getDescriptorSetState()[i].samplerBindings;
			
			// calculate hash of descriptorset, combined with descriptor set sampler state
			uint64_t descriptorSetHash = SpookyHash::Hash64( 
				samplerBindings.data(), 
				samplerBindings.size() * sizeof( DrawCommandInfo::DescriptorSetData::SamplerBindings ), 
				descriptorSetLayoutHash );

			// Receive a descriptorSet from the renderContext's cache.
			// The renderContext will allocate and initialise a DescriptorSet if none has been found.
			const ::vk::DescriptorSet& descriptorSet = mRenderContext->getDescriptorSet( descriptorSetHash, info.descriptorSetState[i] );

			boundVkDescriptorSets.emplace_back( descriptorSet );
			// look up descriptor in descriptor cache -- 
			// if in cache, use descriptor in cache -- otherwise allocate descriptor set from overspill pool.

		}

		// For each requested set, we look up the descriptorset cache 
		// if we already have it. 

		// The cache is indexed by a combination of setLayout hash, and set samplerInfo state.

		// If we don't already have it, the descriptorSet must be created from 
		// the descriptorSetLayout, and initialised with the data from DescriptorSetData
		// if we have it, we can bind the descriptorSet which is already in our cache. 

		// We then always bind the full descriptor set.

		// now append descriptorOffsets for this set to vector of descriptorOffsets for this layout

		// Bind uniforms (the first set contains the matrices)

	}

	// bind dc descriptorsets to current pipeline descriptor sets
	// make sure dynamic ubos have the correct offsets

	mVkCmd.bindDescriptorSets(
		::vk::PipelineBindPoint::eGraphics,	                   // use graphics, not compute pipeline
		*info.getPipelineC().getShader()->getPipelineLayout(), // VkPipelineLayout object used to program the bindings.
		0,                                                     // firstset: first set index (of the above) to bind to - mDescriptorSet[0] will be bound to pipeline layout [firstset]
		boundVkDescriptorSets.size(),                          // setCount: how many sets to bind
		boundVkDescriptorSets.data(),                          // the descriptor sets to match up with our mPipelineLayout (need to be compatible)
		dynamicBindingOffsets.size(),                          // dynamic offsets count how many dynamic offsets
		dynamicBindingOffsets.data()                           // dynamic offsets for each descriptor
	);


	// match current vertex input state against dc.vertex input state
	{
		// bind dc.buffers to current pipeline vertex input states
	}

	


	// record draw command 
	{
		// if index buffer present,
		// use indexed draw command

		// else 
		// use non-indexed draw command
	}



}

