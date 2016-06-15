#include "vk/vkAllocator.h"
#include "ofLog.h"

// ----------------------------------------------------------------------

void of::vk::Allocator::setup(){
	
	
	if ( mSettings.renderer == nullptr ){
		ofLogFatalError() << "Allocator: No renderer specified.";
		ofExit();
	}

	if ( mSettings.device != mSettings.renderer->getVkDevice() ){

		ofLogWarning() << "of::vk::Allocator::setup : Settings.device must match Settings.renderer->getVkDevice()";
		// error checking: make sure mSettings.device == mSettings.renderer->getVkDevice()
		const_cast<VkDevice&>( mSettings.device ) = mSettings.renderer->getVkDevice();
	}

	// we need to find out the min buffer uniform alignment from the 
	// physical device.

	// make this dependent on the type of buffer this allocator stands for 
	const_cast<uint32_t&>( mAlignment ) = mSettings.renderer->getVkPhysicalDeviceProperties().limits.minUniformBufferOffsetAlignment;

	// make sure reserved memory is multiple of alignment	
	const_cast<uint32_t&>( mSettings.size ) = mAlignment * ( ( mSettings.size + mAlignment - 1 ) / mAlignment );

	// Vertex shader uniform buffer block
	VkBufferCreateInfo bufferInfo{
		VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,                   // VkStructureType        sType;
		nullptr,                                                // const void*            pNext;
		0,                                                      // VkBufferCreateFlags    flags;
		mSettings.size,                                         // VkDeviceSize           size;
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,                     // VkBufferUsageFlags     usage;
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

	// Allocate memory for the uniform buffer
	// todo: check for and recover from allocation errors
	vkAllocateMemory( mSettings.device, &allocationInfo, nullptr, &mDeviceMemory );

	// back buffer with memory
	vkBindBufferMemory( mSettings.device, mBuffer, mDeviceMemory, 0 );

	vkMapMemory(
		mSettings.device,
		mDeviceMemory,
		0,
		VK_WHOLE_SIZE, 0, (void**)&mBaseAddress
	);

}

// ----------------------------------------------------------------------

void of::vk::Allocator::reset(){
	vkUnmapMemory( mSettings.device, mDeviceMemory);
	mDeviceMemory = nullptr;

	vkFreeMemory( mSettings.device, mDeviceMemory, nullptr );
	vkDestroyBuffer( mSettings.device, mBuffer, nullptr );
}

// ----------------------------------------------------------------------

bool of::vk::Allocator::allocate( size_t byteCount_, void*& pAddr, uint32_t& offset ){
	uint32_t alignedByteCount = mAlignment * ( ( byteCount_ + mAlignment - 1 ) / mAlignment );

	if ( mOffset + alignedByteCount <= mSettings.size ){
		// write out memory address
		pAddr = mBaseAddress + mOffset;
		// write out offset 
		offset = mOffset;
		mOffset += alignedByteCount;
		return true;
	} else{
		ofLogError() << "Allocator: out of memory";
		// TODO: try to recover by re-allocating or soemthing.
		// Whatever we do here, it will be very costly and should 
		// be avoided.
	}
	return false;
}

// ----------------------------------------------------------------------
bool of::vk::Allocator::free(){
	mOffset = 0;

	return false;
}

// ----------------------------------------------------------------------
