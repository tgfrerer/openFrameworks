#pragma once

#include <memory>
#include "ofLog.h"
#include "vk/Pipeline.h"
#include "vk/vkAllocator.h"
#include "vk/DrawCommand.h"
#include "vk/RenderContext.h"

namespace of {
namespace vk{

// ------------------------------------------------------------

class RenderBatch
{
	/*

	Batch is an object which processes draw instructions
	received through draw command objects.

	Batch's mission is to create a command buffer where
	the number of pipeline changes is minimal.

	*/
	RenderContext * mRenderContext;

	RenderBatch() = delete;

public:

	RenderBatch( RenderContext& rpc );

	~RenderBatch(){
		// todo: check if batch was submitted already - if not, submit.
		// submit();
	}

private:

	// current draw state for building command buffer - this is based on parsing the drawCommand list
	std::unique_ptr<GraphicsPipelineState>                           mCurrentPipelineState;

	uint32_t            mVkSubPassId = 0;

	::vk::RenderPass    mVkRenderPass;  // current renderpass

	std::list<DrawCommand> mDrawCommands;

	void processDrawCommands(const ::vk::CommandBuffer& cmd);
	void beginRenderPass( const ::vk::CommandBuffer& cmd, const ::vk::RenderPass& vkRenderPass_, const ::vk::Framebuffer& vkFramebuffer_, const ::vk::Rect2D& renderArea_ );
	void endRenderPass( const ::vk::CommandBuffer& cmd );

public:
	uint32_t nextSubPass();
	void draw( const DrawCommand& dc );
	void submit();
};

// ----------------------------------------------------------------------

inline void RenderBatch::beginRenderPass(const ::vk::CommandBuffer& cmd, const ::vk::RenderPass& vkRenderPass_, const ::vk::Framebuffer& vkFramebuffer_, const ::vk::Rect2D& renderArea_ ){

	//ofLog() << "begin renderpass";

	mVkSubPassId = 0;

	if ( mVkRenderPass ){
		ofLogError() << "cannot begin renderpass whilst renderpass already open.";
		return;
	}

	mVkRenderPass = vkRenderPass_;

	//!TODO: get correct clear values, and clear value count
	std::array<::vk::ClearValue, 2> clearValues;
	clearValues[0].setColor( reinterpret_cast<const ::vk::ClearColorValue&>( ofFloatColor::blueSteel ) );
	clearValues[1].setDepthStencil( { 1.f, 0 } );

	::vk::RenderPassBeginInfo renderPassBeginInfo;
	renderPassBeginInfo
		.setRenderPass( vkRenderPass_ )
		.setFramebuffer( vkFramebuffer_ )
		.setRenderArea( renderArea_ )
		.setClearValueCount( clearValues.size() )
		.setPClearValues( clearValues.data() )
		;

	cmd.beginRenderPass( renderPassBeginInfo, ::vk::SubpassContents::eInline );
}

// ----------------------------------------------------------------------
// Inside of a renderpass, draw commands may be sorted, to minimize pipeline and binding swaps.
// so endRenderPass should be the point at which the commands are recorded into the command buffer
// If the renderpass allows re-ordering.
inline uint32_t RenderBatch::nextSubPass(){
	return ++mVkSubPassId;
}

// ----------------------------------------------------------------------

inline void RenderBatch::endRenderPass( const ::vk::CommandBuffer& cmd ){
	cmd.endRenderPass();
}

// ----------------------------------------------------------------------

} // end namespce of::vk
} // end namespace of