#pragma once

#include <vulkan/vulkan.hpp>
#include "vk/RenderContext.h"
//#include "vk/TransferBatch.h"
/*

A vkBufferObject is a static buffer of memory that is resident
in device-only memory, ideally. Many buffers should be backed 
by the same allocator of such device-only memory.

Buffer first needs to be stored(staged) in dynamic memory,
then transferred to static memory using vkCmdBufferCopy.

Buffer Copies are only allowed inside of a command buffer,
but outside of a renderpass.

We need to synchronise the buffer transfer so that the memory
is not overwritten or freed before the buffer transfer is 
completed, and so that the transfer operation has been completed
by the time the buffer is used.

Many BufferObjects may be backed by the same buffer, as the 
Allocator owns the respective buffer objects.

+ We need to make sure that transfers happen in bulk.

*/


namespace of {
namespace vk {

// ----------------------------------------------------------------------

class BufferObject_base
{
	::vk::Buffer	 mVkBufferOwnedByContext;
	::vk::DeviceSize offset;
	::vk::DeviceSize range;

	std::shared_ptr<of::vk::RenderContext> mOwningContext;

	//friend // moves following method into next higher scope
	//void copy( BufferObject_base &lhs, BufferObject_base &rhs ){
	//	lhs.copy_to( rhs );
	//};

	//void copy_to( BufferObject_base &rhs ){
	//	rhs.offset = offset;
	//	rhs.range  = range;
	//}

};




}  // end namespace of::vk
}  // end namespace of


// ----------------------------------------------------------------------



  