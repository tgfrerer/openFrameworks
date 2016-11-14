#pragma once

#include <memory>
#include "ofLog.h"
#include "vk/Pipeline.h"
#include "vk/Allocator.h"
#include "vk/DrawCommand.h"
#include "vk/RenderContext.h"

namespace of {
namespace vk{

// ------------------------------------------------------------

class RenderBatch
{
	/*
	
	A Batch maps to a Primary Command buffer which begins and ends a RenderPass

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

	uint32_t            mVkSubPassId = 0;

	std::list<DrawCommand> mDrawCommands;

	void processDrawCommands(const ::vk::CommandBuffer& cmd);

public:
	uint32_t nextSubPass();
	void draw( const DrawCommand& dc );
	void submit();
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