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
	const RenderContext * mRenderContext;

	std::list<std::shared_ptr<BufferObject>> mBatch;

public:
	
	TransferBatch( RenderContext* context_ )
	: mRenderContext(context_)
	{
		
	};

	~TransferBatch(){};

	bool add( std::shared_ptr<BufferObject>& buffer );

	void submit( const ::vk::Device& device, const ::vk::CommandPool& cmdPool, const ::vk::Queue& transferQueue );
	void signalTransferComplete();
};


} // end namespace of::vk
} // end namespace of
