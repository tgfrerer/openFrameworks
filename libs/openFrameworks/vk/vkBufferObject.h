#pragma once

/// A buffer object keeps track of gpu memory


#include <vulkan/vulkan.h>

class ofVkRenderer;

namespace of {
namespace vk {

// we need a reference to the renderer
// since the renderer has a reference to the current physical device
// and we need to read information from the physical device so 
// we know which memory type to allocate from.
// 
// but: we only want to do this once.

class vkBufferObject
{
public:

	const size_t& getAlignment() const;

	// associate a buffer object with a renderer
	void setup( const ofVkRenderer* renderer_, const VkBufferCreateInfo & createInfo_ );
	void reset();

private:
	const ofVkRenderer *	  mRenderer  = nullptr;
	VkBufferCreateInfo        mBufferCreateInfo {};
	VkDevice                  mDevice    = nullptr;

	VkBuffer                  mBuffer    = nullptr;
	VkDeviceMemory            mDeviceMem = nullptr;
	uint8_t*                  mMapping   = nullptr;		 // pinned RAM address 

	size_t			          mAlignment = 0; // alignment (in Bytes) once queried from memory requirements
	size_t                    mUsed      = 0;
	size_t                    mAllocated = 0;

	VkResult allocateMemoryAndBindBuffer();
	void unbindAndFreeBufferMemory();

	VkResult createBuffer( );
	void     destroyBuffer();


};



} // namespace vk
} // namespace of

