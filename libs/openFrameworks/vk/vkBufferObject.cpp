#include "ofLog.h"
#include "vk/vkBufferObject.h"

using namespace of::vk;
/*

	We assume a transfer batch is issued *before* the render batch that might use the buffers for the first time, 
	from dynamic memory.

	We can ensure this by requesting a transfer batch from a context.

	This means, after the draw batch has been submitted, the draw batch fence being signalled means that the 
	command buffer used for the batch has completed execution.

	Command buffers execute in sequence - so adding a transfer barrier into the copy command buffer means 
	that the copy command must have finished executing by the time the draw command buffer is executing.

	Q: How do we tell BufferObjects that their transfer has concluded?
	A: They could be added to a per-virtual frame list of buffers which are in transition.
	Q: But what happens if a BufferObject is again changed whilst in transition?

	Transfer batch needs to be attached to context.
	Context can signal that virtual frame fence has been reached. 
	Once virtual frame fence was reached, we can dispose of dynamic data.

*/

bool BufferObject::setData( void *& pData, ::vk::DeviceSize numBytes ){

	// Writes always go to transient memory
	if ( numBytes > mRange ){
		ofLogError() << "Cannot write " << numBytes << " bytes into buffer of size: " << mRange;
		return false;
	}

	if ( mTransientAllocator->allocate( mRange, mOffset ) ){
		
		const_cast<::vk::Buffer&>(mBuffer) = mTransientAllocator->getBuffer();

		void* writeAddr = nullptr;
		mTransientAllocator->map( writeAddr );
		memcpy( writeAddr, pData, mRange );

		if ( mPersistentAllocator ){
			if ( mHasPersistentMemory = false){
				// try to allocate persistent memory
				mHasPersistentMemory = mPersistentAllocator->allocate( mRange, mPersistentOffset );
			} else{
				// Buffer has already got persistent memory - this means that 
				// the buffer could either be in-flight - 
				// Or it could be that we are updating a buffer, which was made static some frames ago.
			}
			mState = Usage::eDynamic;
		} else{
			mState = Usage::eStream;
		}

		return  true;
	};

	return false;
}

// ----------------------------------------------------------------------

const ::vk::Buffer& BufferObject::getBuffer(){

	auto & device = mTransientAllocator->getSettings().device;

	//!TODO: use check if transfer complete.
	if ( false ){
		
		mOffset = mPersistentOffset;
		const_cast<::vk::Buffer&>( mBuffer ) = mPersistentAllocator->getBuffer();
		mState = Usage::eStatic;
	}

	return mBuffer;
}

// ----------------------------------------------------------------------

bool BufferObject::needsTransfer(){
	return ( mPersistentAllocator.get() != nullptr && mState == Usage::eDynamic );
}

