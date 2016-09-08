#include "vk/RenderBatch.h"
#include "vk/DrawCommand.h"


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

	// upload dc. ubo state to transient GPU memory

	// match currently bound DescriptorSetLayouts against 
	// dc pipeline DescriptorSetLayouts
	{
		// bind dc descriptorsets to current pipeline descriptor sets
		// make sure dynamic ubos have the correct offsets
	}

	

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
