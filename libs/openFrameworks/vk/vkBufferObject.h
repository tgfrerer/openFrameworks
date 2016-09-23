#pragma once

#include <vulkan/vulkan.hpp>
#include "vk/vkAllocator.h"

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

class BufferObject
{
	friend class TransferBatch;
	// buffer will point to dynamic allocator's main buffer
	const ::vk::Buffer&                             mBuffer ;
	const ::vk::DeviceSize                    mRange  = 0;
	::vk::DeviceSize                          mOffset = 0;
	
	::vk::DeviceSize                          mPersistentOffset = 0;
	bool                                      mHasPersistentMemory = false;

	enum class Usage
	{
		eStream,
		eDynamic,
		eStatic
	} mState = Usage::eStream;

	const std::shared_ptr<of::vk::Allocator> mTransientAllocator;
	const std::shared_ptr<of::vk::Allocator> mPersistentAllocator;
	// where buffer is located in dynamic memory

public:

	BufferObject(::vk::DeviceSize numBytes_, std::shared_ptr<of::vk::Allocator>& transientAllocator_, const std::shared_ptr<of::vk::Allocator> persistentAllocator_ = nullptr )
		: mRange( numBytes_ )
		, mTransientAllocator(transientAllocator_)
		, mPersistentAllocator(persistentAllocator_)
		, mBuffer(transientAllocator_->getBuffer())
	{
	};

	// write number of bytes (read from &pData) to buffer memory
	bool setData( void*&pData, ::vk::DeviceSize numBytes );

	const ::vk::Buffer& getBuffer();
	const ::vk::DeviceSize getRange() const;;
	const ::vk::DeviceSize getOffset() const;;

	const std::shared_ptr<of::vk::Allocator>& getPersistentAllocator(){
		return mPersistentAllocator;
	}

	const std::shared_ptr<of::vk::Allocator>& getTransientAllocator(){
		return mTransientAllocator;
	}

	bool needsTransfer();
};


// ----------------------------------------------------------------------

class TransferBatch
{
	std::list<std::shared_ptr<BufferObject>> mBatch;

public:

	bool add( std::shared_ptr<BufferObject>& buffer );

	void submitTransferBuffers( const ::vk::Device& device, const ::vk::CommandPool& cmdPool, const ::vk::Queue& transferQueue );
};

}  // end namespace of::vk
}  // end namespace of


   // ----------------------------------------------------------------------


inline const::vk::DeviceSize of::vk::BufferObject::getRange() const{
	return mRange;
}

inline const::vk::DeviceSize of::vk::BufferObject::getOffset() const{
	return mOffset;
}

  