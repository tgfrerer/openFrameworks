#include "vkBufferObject.h"
#include "ofMatrix4x4.h"
#include "ofVkRenderer.h"


// let's say you wanted to create a buffer from a 
// vector 


void of::vk::vkBufferObject::setup( const ofVkRenderer* renderer_, const VkBufferCreateInfo & createInfo_ ){
	if ( mRenderer != nullptr || mDevice != nullptr ){
		reset();
	}

	mRenderer = const_cast<ofVkRenderer*>(renderer_);
	mBufferCreateInfo =  createInfo_ ;
	mDevice = renderer_->getVkDevice();

	// todo: check alignment based on createInfo.usage
	// storage buffers and uniform buffers have different alignment limits

	createBuffer();
}

// ----------------------------------------------------------------------

VkResult of::vk::vkBufferObject::createBuffer( ){
	auto res = VK_SUCCESS;

	res = vkCreateBuffer( mDevice, &mBufferCreateInfo, nullptr, &mBuffer );

	return res;
}

// ----------------------------------------------------------------------

void of::vk::vkBufferObject::destroyBuffer() {

	// TODO: call deinit: check for pointer validity before destruction

	vkDestroyBuffer( mDevice, mBuffer, nullptr );

	mAllocated = 0;
	mAlignment = 0;
}

// ----------------------------------------------------------------------

void of::vk::vkBufferObject::unbindAndFreeBufferMemory(){
	
	// TODO: check pre-condition: device must be unmapped

	vkFreeMemory( mRenderer->getVkDevice(), mDeviceMem, nullptr );
	mDeviceMem = nullptr;
	mMapping = nullptr;
	
}

// ----------------------------------------------------------------------

VkResult of::vk::vkBufferObject::allocateMemoryAndBindBuffer(){
	auto res = VK_SUCCESS;

	// buffer must have been created at this point!

	// based on what is known on the buffer, find out what memory 
	// requirements would exist for it to live in GPU accessible memory
	VkMemoryRequirements memReqs {};
	vkGetBufferMemoryRequirements( mDevice, mBuffer, &memReqs );
	
	VkMemoryAllocateInfo allocInfo;
	mRenderer->getMemoryAllocationInfo( memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, allocInfo );

	res = vkAllocateMemory( mDevice, &allocInfo, nullptr, &mDeviceMem );

	if ( res != VK_SUCCESS ){
		ofLogError() << "Could not allocate memory : " << res;
		return res;
	}

	mAllocated = memReqs.size;
	mAlignment = memReqs.alignment;

	res = vkBindBufferMemory( mDevice, mBuffer, mDeviceMem, 0 );
	

	if ( res != VK_SUCCESS ){
		ofLogError() << "Could not bind memory";
	}

	return res;
}

// ----------------------------------------------------------------------

const size_t& of::vk::vkBufferObject::getAlignment() const {
	return mAlignment;
}

// ----------------------------------------------------------------------

void of::vk::vkBufferObject::reset(){

	unbindAndFreeBufferMemory();
	destroyBuffer();

	mRenderer = nullptr;
	mDevice = nullptr;
}
// ----------------------------------------------------------------------
