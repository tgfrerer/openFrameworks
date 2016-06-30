#include "vk/vkAllocator.h"
#include "ofLog.h"

// ----------------------------------------------------------------------

void of::vk::Allocator::setup(){
	
	if ( mSettings.renderer == nullptr ){
		ofLogFatalError() << "Allocator: No renderer specified.";
		ofExit();
	}

	if ( mSettings.frames < 1 ){
		ofLogWarning() << "Allocator: Must have a minimum of 1 frame. Setting frames to 1.";
		const_cast<uint32_t&>( mSettings.frames ) = 1;
	}

	if ( mSettings.device != mSettings.renderer->getVkDevice() ){

		ofLogWarning() << "of::vk::Allocator::setup : Settings.device must match Settings.renderer->getVkDevice()";
		// error checking: make sure mSettings.device == mSettings.renderer->getVkDevice()
		const_cast<VkDevice&>( mSettings.device ) = mSettings.renderer->getVkDevice();
	}

	// we need to find out the min buffer uniform alignment from the 
	// physical device.

	// make this dependent on the type of buffer this allocator stands for 
	const_cast<VkDeviceSize&>( mAlignment )     = mSettings.renderer->getVkPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment;

	// make sure reserved memory is multiple of alignment, and that we can fit in the number of requested frames.	
	const_cast<VkDeviceSize&>( mSettings.size ) = mSettings.frames * mAlignment * ( ( mSettings.size / mSettings.frames + mAlignment - 1 ) / mAlignment );

	// Vertex shader uniform buffer block
	VkBufferCreateInfo bufferInfo{
		VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,                   // VkStructureType        sType;
		nullptr,                                                // const void*            pNext;
		0,                                                      // VkBufferCreateFlags    flags;
		mSettings.size,                                         // VkDeviceSize           size;
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT 
		| VK_BUFFER_USAGE_INDEX_BUFFER_BIT
		| VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,                    // VkBufferUsageFlags     usage;
		VK_SHARING_MODE_EXCLUSIVE,                              // VkSharingMode          sharingMode;
		0,                                                      // uint32_t               queueFamilyIndexCount;
		nullptr,                                                // const uint32_t*        pQueueFamilyIndices;
	};
	
	// allocate physical memory from device
	
	// 1. create a buffer 
	vkCreateBuffer( mSettings.device, &bufferInfo, nullptr, &mBuffer );

	// 2. add backing memory to buffer

	// 2.1 Get memory requirements including size, alignment and memory type 
	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements( mSettings.device, mBuffer, &memReqs );

	// 2.2 Get the appropriate memory type for this type of buffer allocation
	// Only memory types that are visible to the host and coherent (coherent means they
	// appear to the GPU without the need of explicit range flushes)
	// Vulkan 1.0 guarantees the presence of at least one host-visible+coherent memory heap.
	VkMemoryAllocateInfo allocationInfo {};
	
	bool result = mSettings.renderer->getMemoryAllocationInfo(
		memReqs,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		allocationInfo
	);

	// 2.3 Finally, to the allocation
	// todo: check for and recover from allocation errors
	vkAllocateMemory( mSettings.device, &allocationInfo, nullptr, &mDeviceMemory );

	// 2.4 Attach memory to buffer (buffer must not be already backed by memory)
	vkBindBufferMemory( mSettings.device, mBuffer, mDeviceMemory, 0 );

	mOffsetEnd.clear();
	mOffsetEnd.resize( mSettings.frames, 0 );

	mBaseAddress.clear();
	mBaseAddress.resize( mSettings.frames, 0 );

	// Map full memory range for CPU write access
	vkMapMemory(
		mSettings.device,
		mDeviceMemory,
		0,
		VK_WHOLE_SIZE, 0, (void**)&mBaseAddress[0]
	);

	for ( uint32_t i = 1; i != mBaseAddress.size(); ++i ){
		// offset the pointer by full frame sizes
		// for base addresses above frame 0
		mBaseAddress[i] = mBaseAddress[0] + i * ( mSettings.size / mSettings.frames );
	}

}

// ----------------------------------------------------------------------

void of::vk::Allocator::reset(){

	if ( mDeviceMemory ){
		vkUnmapMemory( mSettings.device, mDeviceMemory );
		vkFreeMemory( mSettings.device, mDeviceMemory, nullptr );
		mDeviceMemory = nullptr;
	}

	if ( mBuffer ){
		vkDestroyBuffer( mSettings.device, mBuffer, nullptr );
		mBuffer = nullptr;
	}

	mOffsetEnd.clear();
	mBaseAddress.clear();
}

// ----------------------------------------------------------------------
// brief   linear allocator
// param   byteCount number of bytes to allocate
// param   current swapchain image index
// returns pAddr writeable memory address
// returns offsset memory offset in bytes relative to start of buffer to reach address
bool of::vk::Allocator::allocate( VkDeviceSize byteCount_, void*& pAddr, VkDeviceSize& offset, size_t swapIdx ){
	uint32_t alignedByteCount = mAlignment * ( ( byteCount_ + mAlignment - 1 ) / mAlignment );

	if ( mOffsetEnd[swapIdx] + alignedByteCount <= (mSettings.size / mSettings.frames) ){
		// write out memory address
		pAddr = mBaseAddress[swapIdx] + mOffsetEnd[swapIdx];
		// write out offset 
		offset = mOffsetEnd[swapIdx];
		mOffsetEnd[swapIdx] += alignedByteCount;
		// TODO: if you use non-coherent memory you need to invalidate the 
		// cache for the memory that has been written to.
		// What we will realistically do is to flush the full memory range occpuied by a frame
		// instead of the list of sub-allocations.
		return true;
	} else{
		ofLogError() << "Allocator: out of memory";
		// TODO: try to recover by re-allocating or something.
		// Whatever we do here, it will be very costly and should 
		// be avoided.
	}
	return false;
}

// ----------------------------------------------------------------------
void of::vk::Allocator::free(size_t frame){
	mOffsetEnd[frame] = 0;
}

// ----------------------------------------------------------------------
