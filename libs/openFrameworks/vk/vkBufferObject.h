#pragma once

#include <vulkan/vulkan.hpp>
#include "vk/vkAllocator.h"
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

class BufferObject
{
	friend class TransferBatch;
	
	const ::vk::Buffer&                       mBuffer ;
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

public:

	BufferObject(::vk::DeviceSize numBytes_, std::shared_ptr<Allocator>& transientAllocator_, const std::shared_ptr<Allocator> persistentAllocator_ = nullptr )
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

	void setTransferComplete();

	const std::shared_ptr<Allocator>& getPersistentAllocator(){
		return mPersistentAllocator;
	}

	const std::shared_ptr<Allocator>& getTransientAllocator(){
		return mTransientAllocator;
	}

	bool needsTransfer();
};


// ----------------------------------------------------------------------

inline const ::vk::DeviceSize BufferObject::getRange() const{
	return mRange;
}

inline const ::vk::DeviceSize BufferObject::getOffset() const{
	return mOffset;
}

inline void BufferObject::setTransferComplete(){
	if ( mState == Usage::eDynamic ){
		mState  = Usage::eStatic;
		mOffset = mPersistentOffset;
		const_cast<::vk::Buffer&>( mBuffer ) = mPersistentAllocator->getBuffer();
	}
}

}  // end namespace of::vk
}  // end namespace of


// ----------------------------------------------------------------------



  