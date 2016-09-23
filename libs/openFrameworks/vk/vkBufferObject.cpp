#include "ofLog.h"
#include "vk/vkBufferObject.h"

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


*/

bool of::vk::BufferObject::setData( void *& pData, ::vk::DeviceSize numBytes ){

	// Writes always go to transient memory
	if ( numBytes > mRange ){
		ofLogError() << "Cannot write " << numBytes << " bytes into buffer of size: " << mRange;
		return;
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
				// Or it could be that we are updating a buffer.
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

const ::vk::Buffer& of::vk::BufferObject::getBuffer(){

	auto & device = mTransientAllocator->getSettings().device;

	if ( mTransferFence.get() != nullptr && ::vk::Result::eSuccess == device.getFenceStatus( *mTransferFence ) ){
		
		// buffer has been successfully transferred to static memory. 
		
		mTransferFence.reset();

		::vk::BufferCreateInfo bufferCreateInfo;
		bufferCreateInfo
			.setSize( mRange )
			.setUsage(
				::vk::BufferUsageFlagBits::eIndexBuffer
				| ::vk::BufferUsageFlagBits::eUniformBuffer
				| ::vk::BufferUsageFlagBits::eVertexBuffer )
			.setSharingMode( ::vk::SharingMode::eExclusive )
			;

		auto buffer = device.createBuffer( bufferCreateInfo );

		device.bindBufferMemory( buffer, mPersistentAllocator->getDeviceMemory(), mPersistentOffset );
		mOffset = 0;

		

		mState = Usage::eStatic;
	}

	return mBuffer;
}

// ----------------------------------------------------------------------

bool of::vk::BufferObject::needsTransfer(){
	return ( mPersistentAllocator.get() != nullptr && mState == Usage::eDynamic );
}

// ----------------------------------------------------------------------

bool of::vk::TransferBatch::add( std::shared_ptr<BufferObject>& buffer ){

	// check if buffer can be added

	if ( !buffer->needsTransfer() ){
		ofLogVerbose() << "TransferBatch: Buffer does not need transfer.";
		return false;
	}

	// --------| invariant: buffer needs transfer.

	// find the first element in the batch that matches the transient and 
	// persistent buffer targets of the current buffer - if nothing found,
	// return last element.
	auto it = std::find_if( mBatch.begin(), mBatch.end(), [buffer]( std::shared_ptr<BufferObject>*lhs ){
		return buffer->getTransientAllocator()->getBuffer() == ( *lhs )->getTransientAllocator()->getBuffer()
			&& buffer->getPersistentAllocator()->getBuffer() == ( *lhs )->getPersistentAllocator()->getBuffer();
	} );

	mBatch.insert( it, buffer );

	return true;
}

// ----------------------------------------------------------------------

void of::vk::TransferBatch::submitTransferBuffers( const ::vk::Device& device, const ::vk::CommandPool& cmdPool, const ::vk::Queue& transferQueue ){

	if ( mBatch.empty() ){
		return;
	}

	//auto renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

	//// get device
	//mDevice = renderer->getVkDevice();

	//// get command pool
	//const auto& cmdPool = renderer->getSetupCommandPool();

	//// get queue
	//auto& queue = renderer->getQueue();


	// First, we need a command buffer where we can record a pipeline barrier command into.
	// This command - the pipeline barrier with an image barrier - will transfer the 
	// image resource from its original layout to a layout that the gpu can use for 
	// sampling.
	::vk::CommandBuffer cmd = nullptr;
	{
		::vk::CommandBufferAllocateInfo cmdBufAllocInfo;
		cmdBufAllocInfo
			.setCommandPool( cmdPool )
			.setLevel( ::vk::CommandBufferLevel::ePrimary )
			.setCommandBufferCount( 1 )
			;
		cmd = device.allocateCommandBuffers( cmdBufAllocInfo ).front();
	}

	std::vector<::vk::BufferCopy> bufferCopies;
	bufferCopies.reserve( mBatch.size() );

	::vk::Buffer srcBuf = nullptr;
	::vk::Buffer dstBuf = nullptr;
	
	cmd.begin( { ::vk::CommandBufferUsageFlagBits::eOneTimeSubmit } );

	for ( auto it = mBatch.cbegin(); it != mBatch.cend(); ++it ){
		const auto & bufferObject = *it;

		::vk::BufferCopy bufferCopy;
		bufferCopy
			.setSize     ( bufferObject->mRange            )
			.setSrcOffset( bufferObject->mOffset           )
			.setDstOffset( bufferObject->mPersistentOffset )
			;

		// if src or dst are different from last buffer, flush, and enqueue next one.
		if ( bufferObject->getTransientAllocator()->getBuffer() != srcBuf
			|| bufferObject->getPersistentAllocator()->getBuffer() != dstBuf ){

			if ( bufferCopies.empty() == false ){
				// submit buffer copies.
				cmd.copyBuffer( srcBuf, dstBuf, bufferCopies );
				bufferCopies.clear();
				bufferCopies.reserve( mBatch.size() );
			}
			srcBuf = bufferObject->getTransientAllocator()->getBuffer();
			dstBuf = bufferObject->getPersistentAllocator()->getBuffer();
		} 
		
		bufferCopies.push_back(bufferCopy);

		if ( std::next( it ) == mBatch.cend() && !bufferCopies.empty()){
			// submit buffer copies
			cmd.copyBuffer( srcBuf, dstBuf, bufferCopies );
		}

	}

	// TODO: add transfer barrier

	cmd.end();


	// Submit the command buffer to the transfer queue

	::vk::SubmitInfo submitInfo;
	submitInfo
		.setWaitSemaphoreCount( 0 )
		.setPWaitSemaphores( nullptr )
		.setPWaitDstStageMask( nullptr )
		.setCommandBufferCount( 1 )
		.setPCommandBuffers( &cmd )
		.setSignalSemaphoreCount( 0 )
		.setPSignalSemaphores( nullptr )
		;

	transferQueue.submit( {submitInfo}, nullptr );

	mBatch.clear();
}