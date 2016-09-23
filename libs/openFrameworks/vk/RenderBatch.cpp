#include "vk/RenderBatch.h"
#include "vk/spooky/SpookyV2.h"
#include "vk/Shader.h"

using namespace of::vk;

// ------------------------------------------------------------
RenderBatch::RenderBatch( RenderContext & rpc )
	:mRenderContext( &rpc ){
}

// ------------------------------------------------------------

void RenderBatch::draw( const DrawCommand& dc_ ){

	// local copy of draw command.
	DrawCommand dc = dc_;

	//!TODO: commit draw command memory to gpu-update
	// this will update dynamic offsets as a side-effect, 
	// and will also update the buffer 
	// for the bindings affected.
	dc.commitUniforms( mRenderContext->getAllocator() );
	dc.commitMeshAttributes( mRenderContext->getAllocator() );
	
	mDrawCommands.emplace_back( std::move(dc) );

}

// ----------------------------------------------------------------------

void RenderBatch::submit(){
	// submit command buffer to context.
	// context will submit command buffers batched to queue 
	// at its own pleasure, but in seqence.

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

void RenderBatch::processDrawCommands(){

	// first order draw commands

	// order them by renderpass, 
	// then pipeline,
	// then descriptor set usage

	// then process draw commands

	auto & renderPass = mDrawCommands.front().getInfo().pipeline.getRenderPass();
	
	beginRenderPass( renderPass, mRenderContext->getFramebuffer(), mRenderContext->getRenderArea() );


	for ( auto & dc : mDrawCommands ){

		auto & info = const_cast<DrawCommandInfo&>( dc.getInfo() );

		// find out pipeline state needed for this draw command

		//info.modifyPipeline().setRenderPass(mVkRenderPass);
		//info.modifyPipeline().setSubPass(mVkSubPassId);

		if ( !mCurrentPipelineState || *mCurrentPipelineState != dc.getInfo().getPipeline() ){
			// look up pipeline in pipeline cache
			// otherwise, create a new pipeline, then bind pipeline.

			mCurrentPipelineState = std::make_unique<GraphicsPipelineState>( dc.getInfo().getPipeline() );

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