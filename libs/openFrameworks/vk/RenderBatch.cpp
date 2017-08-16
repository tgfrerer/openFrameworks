#include "vk/RenderBatch.h"
#include "vk/spooky/SpookyV2.h"
#include "vk/Shader.h"

using namespace of::vk;

// ------------------------------------------------------------

RenderBatch::RenderBatch( RenderBatch::Settings& settings )
	: mSettings( settings )
{
	auto & context = *mSettings.context;
	// Allocate a new command buffer for this batch.
	mVkCmd = context.allocateCommandBuffer( ::vk::CommandBufferLevel::ePrimary );
	mVkCmd.begin( { ::vk::CommandBufferUsageFlagBits::eOneTimeSubmit } );
}

// ------------------------------------------------------------

of::vk::RenderBatch& RenderBatch::draw( const DrawCommand& dc_ ){

	// local copy of draw command.
	DrawCommand dc = dc_;

	finalizeDrawCommand( dc );

	mDrawCommands.emplace_back( std::move(dc) );
	
	return *this;
}

// ----------------------------------------------------------------------

RenderBatch & of::vk::RenderBatch::draw( const DrawCommand & dc_, uint32_t vertexCount_, uint32_t instanceCount_, uint32_t firstVertex_, uint32_t firstInstance_ ){
	
	// local copy of draw command.
	DrawCommand dc = dc_;

	finalizeDrawCommand( dc );

	dc.mDrawMethod    = DrawCommand::DrawMethod::eDraw;
	dc.mNumVertices   = vertexCount_;
	dc.mInstanceCount = instanceCount_;
	dc.mFirstVertex   = firstVertex_;
	dc.mFirstInstance = firstInstance_;

	mDrawCommands.emplace_back( std::move( dc ) );

	return *this;
}

// ----------------------------------------------------------------------

RenderBatch & of::vk::RenderBatch::draw( const DrawCommand & dc_, uint32_t indexCount_, uint32_t instanceCount_, uint32_t firstIndex_, int32_t vertexOffset_, uint32_t firstInstance_ ){

	// local copy of draw command.
	DrawCommand dc = dc_;

	finalizeDrawCommand( dc );

	dc.mDrawMethod    = DrawCommand::DrawMethod::eIndexed;
	dc.mNumIndices    = indexCount_;
	dc.mInstanceCount = instanceCount_;
	dc.mFirstIndex    = firstIndex_;
	dc.mVertexOffset  = vertexOffset_;
	dc.mFirstInstance = firstInstance_;

	mDrawCommands.emplace_back( std::move( dc ) );

	return *this;
}

// ----------------------------------------------------------------------

void of::vk::RenderBatch::finalizeDrawCommand( of::vk::DrawCommand &dc ){
	// Commit draw command memory to gpu
	// This will update dynamic offsets as a side-effect, 
	// and will also update the buffer ID for the bindings affected.
	dc.commitUniforms( mSettings.context->getAllocator() );
	dc.commitMeshAttributes( mSettings.context->getAllocator() );

	// Renderpass is constant over a RenderBatch, as a RenderBatch encapsulates 
	// a renderpass with all its subpasses.
	dc.mPipelineState.setRenderPass( mSettings.renderPass );
	dc.mPipelineState.setSubPass( mVkSubPassId );
}

// ----------------------------------------------------------------------

void RenderBatch::begin(){
	
	// TODO: When runing in debug, check for RenderBatch state: cannot begin when not ended, or not initial

	auto & context = *mSettings.context;

	if ( mSettings.renderPass ){	
		
		// Begin Renderpass - 
		// This is only allowed if the context maps a primary renderpass!

		// Note that secondary command buffers inherit the renderpass 
		// from their primary.

		// Create a new Framebuffer. The framebuffer connects RenderPass with
		// ImageViews where results of this renderpass will be stored.
		//
		::vk::FramebufferCreateInfo framebufferCreateInfo;
		framebufferCreateInfo
			.setRenderPass( mSettings.renderPass )
			.setAttachmentCount( mSettings.framebufferAttachments.size() )
			.setPAttachments( mSettings.framebufferAttachments.data() )
			.setWidth( mSettings.framebufferAttachmentsWidth )
			.setHeight( mSettings.framebufferAttachmentsHeight )
			.setLayers( 1 )
			;

		// Framebuffers in Vulkan are very light-weight objects, whose main purpose 
		// is to connect RenderPasses to Image attachments. 
		//
		// Since the swapchain might have a different number of images than this context has virtual 
		// frames, and the swapchain may even acquire images out-of-sequence, we must re-create the 
		// framebuffer on each frame to make sure we're attaching the renderpass to the correct 
		// attachments.
		//
		// We create framebuffers through the context, so that the context
		// can free all old framebuffers once the frame has been processed.
		mFramebuffer = context.createFramebuffer( framebufferCreateInfo );

		::vk::RenderPassBeginInfo renderPassBeginInfo;
		renderPassBeginInfo
			.setRenderPass( mSettings.renderPass )
			.setFramebuffer( mFramebuffer )
			.setRenderArea( mSettings.renderArea )
			.setClearValueCount( mSettings.clearValues.size() )
			.setPClearValues( mSettings.clearValues.data() )
			;

		mVkCmd.beginRenderPass( renderPassBeginInfo, ::vk::SubpassContents::eInline );
	}

	// Set dynamic viewport
	// TODO: these dynamics may belong to the draw command
	::vk::Viewport vp;
	vp
		.setX( 0 )
		.setY( 0 )
		.setWidth(  mSettings.renderArea.extent.width )
		.setHeight( mSettings.renderArea.extent.height )
		.setMinDepth( 0.f )
		.setMaxDepth( 1.f )
		;
	mVkCmd.setViewport( 0, { vp } );
	mVkCmd.setScissor( 0, { mSettings.renderArea } );
}

// ----------------------------------------------------------------------

void RenderBatch::end(){
	// submit command buffer to context.
	// context will submit command buffers batched to queue 
	// at its own pleasure, but in seqence.

	processDrawCommands();

	if ( mSettings.renderPass ){
		// end renderpass if Context / CommandBuffer is Primary
		mVkCmd.endRenderPass();
	}

	mVkCmd.end();
	
	auto & context = const_cast<Context&>( *mSettings.context );

	// add command buffer to command queue of context.
	context.submit( std::move( mVkCmd ) );

	mVkCmd = nullptr;
	mDrawCommands.clear();
}

// ----------------------------------------------------------------------

::vk::CommandBuffer & RenderBatch::getVkCommandBuffer(){
	// Flush currently queued up draw commands so that command buffer
	// is in the right state
	processDrawCommands();
	return mVkCmd;
}


// ----------------------------------------------------------------------

void RenderBatch::processDrawCommands( ){
	
	auto & context = const_cast<Context&>( *mSettings.context );

	// CONSIDER: Order draw commands
	// Order by (using radix-sort)
	// 1) subpass id, 
	// 2) pipeline,
	// 3) descriptor set usage

	
	// current draw state for building command buffer - this is based on parsing the drawCommand list
	std::unique_ptr<GraphicsPipelineState> boundPipelineState;

	for ( auto & dc : mDrawCommands ){

		// find out pipeline state needed for this draw command

		if ( !boundPipelineState || *boundPipelineState != dc.mPipelineState ){
			// look up pipeline in pipeline cache
			// otherwise, create a new pipeline, then bind pipeline.

			boundPipelineState = std::make_unique<GraphicsPipelineState>( dc.mPipelineState );

			uint64_t pipelineStateHash = boundPipelineState->calculateHash();

			auto & currentPipeline = context.borrowPipeline( pipelineStateHash );

			if ( currentPipeline.get() == nullptr ){
				currentPipeline  =
					std::shared_ptr<::vk::Pipeline>( ( new ::vk::Pipeline ),
						[device = context.mDevice]( ::vk::Pipeline*rhs ){
					if ( rhs ){
						device.destroyPipeline( *rhs );
					}
					delete rhs;
				} );

				*currentPipeline = boundPipelineState->createPipeline( context.mDevice, context.mSettings.pipelineCache);
			}

			mVkCmd.bindPipeline( ::vk::PipelineBindPoint::eGraphics, *currentPipeline );
		}

		// ----------| invariant: correct pipeline is bound

		// Match currently bound DescriptorSetLayouts against 
		// dc pipeline DescriptorSetLayouts
		std::vector<::vk::DescriptorSet> boundVkDescriptorSets;
		std::vector<uint32_t> dynamicBindingOffsets;

		const std::vector<uint64_t> & setLayoutKeys = dc.mPipelineState.getShader()->getDescriptorSetLayoutKeys();
		
		dynamicBindingOffsets.reserve( setLayoutKeys.size() );
		boundVkDescriptorSets.reserve( setLayoutKeys.size() );

		for ( size_t setId = 0; setId != setLayoutKeys.size(); ++setId ){

			uint64_t setLayoutKey = setLayoutKeys[setId];
			auto & descriptors = dc.getDescriptorSetData( setId ).descriptors;
			const auto descriptorSetLayout = dc.mPipelineState.getShader()->getDescriptorSetLayout( setId );
			// calculate hash of descriptorset, combined with descriptor set sampler state
			// TODO: can we accelerate this by caching descriptorSet hash inside shader/draw command?
			uint64_t descriptorSetHash = SpookyHash::Hash64( descriptors.data(), descriptors.size() * sizeof( DescriptorSetData_t::DescriptorData_t ), setLayoutKey );

			// Receive a DescriptorSet from the RenderContext's cache.
			// The renderContext will allocate and initialise a DescriptorSet if none has been found.
			const ::vk::DescriptorSet& descriptorSet = context.getDescriptorSet( descriptorSetHash, setId, *descriptorSetLayout , descriptors );

			boundVkDescriptorSets.emplace_back( descriptorSet );

			const auto & offsets  = dc.getDescriptorSetData( setId ).dynamicBindingOffsets;
			
			// now append dynamic binding offsets for this set to vector of dynamic offsets for this draw call
			dynamicBindingOffsets.insert( dynamicBindingOffsets.end(), offsets.begin(), offsets.end() );

		}

		// Bind resources

		// Bind dc DescriptorSets to current pipeline descriptor sets
		// make sure dynamic UBOs have the correct offsets
		if ( !boundVkDescriptorSets.empty() ){
			mVkCmd.bindDescriptorSets(
				::vk::PipelineBindPoint::eGraphics,	                           // use graphics, not compute pipeline
				*dc.mPipelineState.getShader()->getPipelineLayout(),           // VkPipelineLayout object used to program the bindings.
				0,                                                             // firstset: first set index (of the above) to bind to - mDescriptorSet[0] will be bound to pipeline layout [firstset]
				boundVkDescriptorSets.size(),                                  // setCount: how many sets to bind
				boundVkDescriptorSets.data(),                                  // the descriptor sets to match up with our mPipelineLayout (need to be compatible)
				dynamicBindingOffsets.size(),                                  // dynamic offsets count how many dynamic offsets
				dynamicBindingOffsets.data()                                   // dynamic offsets for each descriptor
			);
		}

		// Bind attributes, and draw
		{

			const auto & vertexOffsets = dc.getVertexOffsets();
			const auto & indexOffset   = dc.getIndexOffsets();

			const auto & vertexBuffers = dc.getVertexBuffers();
			const auto & indexBuffer   = dc.getIndexBuffer();

			// TODO: cull vertexOffsets which refer to empty vertex attribute data
			//       make sure that a pipeline with the correct bindings is bound to match the 
			//       presence or non-presence of mesh data.

			// Bind vertex data buffers to current pipeline. 
			// The vector indices into bufferRefs, vertexOffsets correspond to [binding numbers] of the currently bound pipeline.
			// See Shader.h for an explanation of how this is mapped to shader attribute locations

			if ( !vertexBuffers.empty() ){
				mVkCmd.bindVertexBuffers( 0, vertexBuffers, vertexOffsets );
			}

			switch ( dc.mDrawMethod ){
			case DrawCommand::DrawMethod::eDraw: 
				// non-indexed draw
				mVkCmd.draw( dc.mNumVertices, dc.mInstanceCount, dc.mFirstVertex, dc.mFirstInstance );
				break;
			case DrawCommand::DrawMethod::eIndexed:
				// indexed draw
				mVkCmd.bindIndexBuffer( indexBuffer, indexOffset, ::vk::IndexType::eUint32 );
				mVkCmd.drawIndexed( dc.mNumIndices, dc.mInstanceCount, dc.mFirstIndex, dc.mVertexOffset, dc.mFirstInstance );
				break;
			case DrawCommand::DrawMethod::eIndirect:
				// TODO: implement
				break;
			case DrawCommand::DrawMethod::eIndexedIndirect:
				// TODO: implement
				break;
			}

		}

	}
	
	// remove processed draw commands from queue
	mDrawCommands.clear();

}

