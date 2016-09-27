#pragma once

#include <vulkan/vulkan.hpp>
#include "vk/RenderContext.h"

namespace of{
namespace vk{

// TODO: transfer batch needs to have virtual frames as well, 
// And must keep transfers (and transfer command buffers!) alife until the 
// context signals that the transfer for a part	is complete.
// as the command buffer is allocated from the virtual frame, resetting 
// the virtual frame command pool will take care of removing the command
// buffer - so we don't have trouble with that.

class BufferObject;

class TransferBatch 
{
	friend RenderContext;

	// These methods may only be called by RenderContext
	void submit( const ::vk::Queue& transferQueue );
	void signalTransferComplete();


private:

	RenderContext *                    mRenderContext;
	
	// batch which accumulates all submitted batches whilst the frame is in flight
	std::list<std::shared_ptr<BufferObject>> mInflightBatch;
	// batch which accumulates transfers until submit
	std::list<std::shared_ptr<BufferObject>> mBatch;

	TransferBatch() = delete;

public:
	
	TransferBatch( RenderContext* context_ )
	: mRenderContext(context_)
	{
		
	};

	~TransferBatch(){
		// as we don't own anything, there is nothing to destroy.
	};

	bool add( std::shared_ptr<BufferObject>& buffer );


};


} // end namespace of::vk
} // end namespace of
