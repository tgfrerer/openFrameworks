#pragma once

#include <vulkan\vulkan.h>
#include "ofVkRenderer.h"

namespace of{
namespace vk{

class Allocator
{

public:
	struct Settings
	{
		//enum class Type
		//{
		//	Dynamic,
		//	Static,
		//	Uniform,
		//	Texture,
		//} mAllocatorType;
		uint32_t                         size                           = 0; // how much memory to reserve on hardware for this allocator
		ofVkRenderer                    *renderer                       = nullptr;
		VkDevice                         device                         = nullptr;
	};

	Allocator( const Settings& settings )
	: mSettings(settings)
	{};

	~Allocator(){
		reset();
	};

	/// \detail set up allocator based on Settings and pre-allocate 
	///         a chunk of GPU memory, and attach a buffer to it 
	void setup();

	/// \brief  free GPU memory and de-initialise allocator
	void reset();

	/// \brief  sub-allocate a chunk of memory from GPU
	/// 
	bool allocate( size_t byteCount_, void*& pAddr, uint32_t& offset );
	
	/// \brief  remove all sub-allocations
	/// \note   this does not free GPU memory, it just marks it as unused
	bool free();


private:
	const Settings mSettings;

	uint32_t	         mOffset     = 0;        // next free location for allocations
	const uint32_t       mAlignment   = 0;        // alignment is calculated on setup
	uint8_t*             mBaseAddress = nullptr;  // base address for mapped memory

	VkBuffer       mBuffer;			  // owning these.
	VkDeviceMemory mDeviceMemory;	  // owning these

};


} // namespace vk
} // namespace of