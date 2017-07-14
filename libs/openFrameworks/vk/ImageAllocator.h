#pragma once

#include "vk/Allocator.h"
#include "vk/HelperTypes.h"

namespace of{
namespace vk{

// ----------------------------------------------------------------------


/*
	BufferAllocator is a simple linear allocator.

	Allocator may have more then one virtual frame,
	and only allocations from the current virutal 
	frame are performed until swap(). 

	Allocator may be for transient memory or for 
	static memory.

	If allocated from Host memory, the allocator 
	maps a buffer to CPU visible memory for its 
	whole lifetime. 

*/


class ImageAllocator : public AbstractAllocator
{

public:

	struct Settings : public AbstractAllocator::Settings
	{
		::vk::ImageUsageFlags imageUsageFlags = (
			::vk::ImageUsageFlagBits::eTransferDst
			| ::vk::ImageUsageFlagBits::eSampled
			);

		::vk::ImageTiling imageTiling = ::vk::ImageTiling::eOptimal;

		// ----- convenience methods 

		Settings & setSize( ::vk::DeviceSize size_ ){
			AbstractAllocator::Settings::size = size_;
			return *this;
		}
		Settings & setMemFlags( ::vk::MemoryPropertyFlags flags_ ){
			AbstractAllocator::Settings::memFlags = flags_;
			return *this;
		}
		Settings & setQueueFamilyIndices( const std::vector<uint32_t> indices_ ){
			AbstractAllocator::Settings::queueFamilyIndices = indices_;
			return *this;
		}
		Settings & setRendererProperties( const of::vk::RendererProperties& props ){
			AbstractAllocator::Settings::device = props.device;
			AbstractAllocator::Settings::physicalDeviceMemoryProperties = props.physicalDeviceMemoryProperties;
			AbstractAllocator::Settings::physicalDeviceProperties = props.physicalDeviceProperties;
			return *this;
		}
		Settings & setImageUsageFlags( const ::vk::ImageUsageFlags& flags_ ){
			imageUsageFlags = flags_;
			return *this;
		}
		Settings & setImageTiling( const ::vk::ImageTiling & tiling_ ){
			imageTiling = tiling_;
			return *this;
		}
	};

	ImageAllocator(  )
		: mSettings(){};

	~ImageAllocator(){
		mSettings.device.waitIdle();
		reset();
	};

	/// @detail set up allocator based on Settings and pre-allocate 
	///         a chunk of GPU memory, and attach a buffer to it 
	void setup(const ImageAllocator::Settings& settings) ;

	/// @brief  free GPU memory and de-initialise allocator
	void reset() override;

	/// @brief  sub-allocate a chunk of memory from GPU
	/// 
	bool allocate( ::vk::DeviceSize byteCount_, ::vk::DeviceSize& offset ) override;

	void swap() override;

	const ::vk::DeviceMemory& getDeviceMemory() const override;

	/// @brief  remove all sub-allocations within the given frame
	/// @note   this does not free GPU memory, it just marks it as unused
	void free();

	// jump to use next segment assigned to next virtual frame

	const AbstractAllocator::Settings& getSettings() const override{
		return mSettings;
	}

private:
	const ImageAllocator::Settings     mSettings;
	const ::vk::DeviceSize             mImageGranularity = (1UL << 10);  // granularity is calculated on setup. must be power of two.

	::vk::DeviceSize                   mOffsetEnd = 0;            // next free location for allocations
	::vk::DeviceMemory                 mDeviceMemory = nullptr;	  // owning

};

// ----------------------------------------------------------------------

inline const ::vk::DeviceMemory & of::vk::ImageAllocator::getDeviceMemory() const{
	return mDeviceMemory;
}

// ----------------------------------------------------------------------


} // namespace of::vk
} // namespace of