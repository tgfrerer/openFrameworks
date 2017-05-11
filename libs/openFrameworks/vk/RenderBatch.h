#pragma once

#include <memory>
#include "ofLog.h"
#include "vk/Pipeline.h"
#include "vk/Allocator.h"
#include "vk/DrawCommand.h"
#include "vk/Context.h"

namespace of {
namespace vk{

// ------------------------------------------------------------

class RenderBatch
{
	/*
	
	A Batch maps to a Primary Command buffer which begins and ends a RenderPass

	Batch is an object which processes draw instructions
	received through draw command objects.

	Batch's mission is to create a single command buffer from all draw commands
	it accumulates, and it aims to minimize the number of pipeline switches
	between draw calls.

	*/

	Context *              mRenderContext;
	uint32_t               mVkSubPassId = 0;
	std::list<DrawCommand> mDrawCommands;

	// vulkan command buffer mapped to this batch.
	::vk::CommandBuffer mVkCmd;

	// renderbatch must be constructed from a valid context.
	RenderBatch() = delete;

public:

	RenderBatch( Context& rc );

	~RenderBatch(){
		if ( !mDrawCommands.empty() ){
			ofLogWarning() << "Unsubmitted draw commands leftover in RenderBatch";
		}
	}

	uint32_t nextSubPass();
	
	RenderBatch & draw( const DrawCommand& dc);

	// explicit draw - parameters override DrawCommand State
	RenderBatch & draw( const DrawCommand& dc, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance );
	
	// explicit indexed draw - parameters override DrawCommand State
	RenderBatch & draw( const DrawCommand& dc, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance );

	//RenderBatch & drawIndirect( const DrawCommand& dc );
	//RenderBatch & drawIndexedIndirect( const DrawCommand& dc );
	
	// Begin command buffer, begin renderpass, 
	// and also setup default values for scissor and viewport.
	void begin();

	// End renderpass, processes draw commands added to batch, and submits batch translated into commandBuffer to 
	// context's command buffer queue.
	void end();

	// Return vulkan command buffer mapped to this batch
	// n.b. this flushes, i.e. processes all draw commands queued up until this command is called.
	::vk::CommandBuffer& getVkCommandBuffer();

private:

	void finalizeDrawCommand( of::vk::DrawCommand &dc );
	void processDrawCommands( );

};


// ----------------------------------------------------------------------
// Inside of a renderpass, draw commands may be sorted, to minimize pipeline and binding swaps.
// so endRenderPass should be the point at which the commands are recorded into the command buffer
// If the renderpass allows re-ordering.
inline uint32_t RenderBatch::nextSubPass(){
	return ++mVkSubPassId;
}


// ----------------------------------------------------------------------

} // end namespce of::vk
} // end namespace of