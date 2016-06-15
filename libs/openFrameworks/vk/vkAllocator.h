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
		uint32_t                         size       = 0; // how much memory to reserve on hardware for this allocator
		ofVkRenderer                    *renderer   = nullptr;
		VkDevice                         device     = nullptr;
		uint32_t                         frames     = 1; // number of frames to reserve within this allocator
	};

	Allocator( const Settings& settings )
	: mSettings(settings)
	{};

	~Allocator(){
		reset();
	};

	/// @detail set up allocator based on Settings and pre-allocate 
	///         a chunk of GPU memory, and attach a buffer to it 
	void setup();

	/// @brief  free GPU memory and de-initialise allocator
	void reset();

	/// @brief  sub-allocate a chunk of memory from GPU
	/// 
	bool allocate(size_t byteCount_, void*& pAddr, uint32_t& offset, size_t frame_);
	
	/// @brief  remove all sub-allocations within the given frame
	/// @note   this does not free GPU memory, it just marks it as unused
	void free(size_t frame_);

	const VkBuffer&		getBuffer(){
		return mBuffer;
	};

private:
	const Settings         mSettings;
	const uint32_t         mAlignment = 0;    // alignment is calculated on setup

	std::vector<uint32_t>  mOffset;           // next free location for allocations
	std::vector<uint8_t*>  mBaseAddress;      // base address for mapped memory

	VkBuffer               mBuffer;			  // owning
	VkDeviceMemory         mDeviceMemory;	  // owning

};


} // namespace vk
} // namespace of