#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>

class ofVkRenderer; // ffdecl.

namespace of{
namespace vk{


class Allocator
{

public:
	struct Settings
	{
		::vk::PhysicalDeviceProperties       physicalDeviceProperties;
		::vk::PhysicalDeviceMemoryProperties physicalDeviceMemoryProperties;
		::vk::Device                         device     = nullptr;
		::vk::DeviceSize                     size       = 0; // how much memory to reserve on hardware for this allocator
		::vk::MemoryPropertyFlags            memFlags = ( ::vk::MemoryPropertyFlagBits::eHostVisible | ::vk::MemoryPropertyFlagBits::eHostCoherent );
		uint32_t                             frameCount     = 1; // number of frames to reserve within this allocator

		Settings& setPhysicalDeviceProperties( ::vk::PhysicalDeviceProperties physicalDeviceProperties_ ){
			physicalDeviceProperties = physicalDeviceProperties_;
			return *this;
		}
		Settings& setPhysicalDeviceMemoryProperties( ::vk::PhysicalDeviceMemoryProperties physicalDeviceMemoryProperties_ ){
			physicalDeviceMemoryProperties = physicalDeviceMemoryProperties_;
			return *this;
		}
		Settings& setDevice( ::vk::Device device_ ){
			device = device_; 
			return *this;
		}
		Settings& setSize( ::vk::DeviceSize size_ ){
			size = size_;
			return *this;
		}
		Settings& setFrameCount( uint32_t frameCount_ ){
			frameCount = frameCount_;
			return *this;
		}
		Settings& setMemoryPropertyFlags( ::vk::MemoryPropertyFlags flags ){
			memFlags = flags;
			return *this;
		}

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
	bool allocate(::vk::DeviceSize byteCount_, ::vk::DeviceSize& offset);
	
	// return address to writeable memory, if this allocator is ready to write.
	bool map( void*& pAddr ){
		pAddr = mCurrentMappedAddress;
		return ( mCurrentMappedAddress != nullptr );
	};

	/// @brief  remove all sub-allocations within the given frame
	/// @note   this does not free GPU memory, it just marks it as unused
	void free();

	// jump to use next segment assigned to next virtual frame
	void swap();

	const ::vk::Buffer& getBuffer() const{
		return mBuffer;
	};

	bool  getMemoryAllocationInfo( const ::vk::MemoryRequirements& memReqs, ::vk::MemoryPropertyFlags memProps, ::vk::MemoryAllocateInfo& memInfo ) const;

private:
	const Settings                 mSettings;
	const ::vk::DeviceSize         mAlignment = 256;  // alignment is calculated on setup - but 256 is a sensible default as it is the largest possible according to spec

	std::vector<::vk::DeviceSize>  mOffsetEnd;        // next free location for allocations
	std::vector<uint8_t*>          mBaseAddress;      // base address for mapped memory

	::vk::Buffer                   mBuffer;			  // owning
	::vk::DeviceMemory             mDeviceMemory;	  // owning

	void*                          mCurrentMappedAddress = nullptr; // current address for writing
	size_t                         mCurrentVirtualFrameIdx = 0; // currently mapped segment
};

} // namespace vk
} // namespace of


inline bool of::vk::Allocator::getMemoryAllocationInfo( const::vk::MemoryRequirements & memReqs, ::vk::MemoryPropertyFlags memProps, ::vk::MemoryAllocateInfo & memInfo ) const{
	if ( !memReqs.size ){
		memInfo.allocationSize = 0;
		memInfo.memoryTypeIndex = ~0;
		return true;
	}

	// Find an available memory type that satifies the requested properties.
	uint32_t memoryTypeIndex;
	for ( memoryTypeIndex = 0; memoryTypeIndex < mSettings.physicalDeviceMemoryProperties.memoryTypeCount; ++memoryTypeIndex ){
		if ( ( memReqs.memoryTypeBits & ( 1 << memoryTypeIndex ) ) &&
			( mSettings.physicalDeviceMemoryProperties.memoryTypes[memoryTypeIndex].propertyFlags & memProps ) == memProps ){
			break;
		}
	}
	if ( memoryTypeIndex >= mSettings.physicalDeviceMemoryProperties.memoryTypeCount ){
		assert( 0 && "memorytypeindex not found" );
		return false;
	}

	memInfo.allocationSize = memReqs.size;
	memInfo.memoryTypeIndex = memoryTypeIndex;

	return true;
}