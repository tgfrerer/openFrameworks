#pragma once

#include <vulkan/vulkan.hpp>
#include "ofLog.h"

namespace of{
namespace vk{


struct RendererSettings
{
	uint32_t vkVersion = (1 << 22) | (0 << 12) | (39);                 // target version
	uint32_t numVirtualFrames = 3;                                     // number of virtual frames to allocate and to produce - set this through vkWindowSettings
	uint32_t numSwapchainImages = 3;                                   // number of swapchain images to aim for (api gives no guarantee for this.)
	::vk::PresentModeKHR presentMode = ::vk::PresentModeKHR::eFifo;	   // selected swapchain type (api only guarantees FIFO)
	std::vector<::vk::QueueFlags> requestedQueues = {                  // queues which will be created for this device, index will be queue index in mQueues
		::vk::QueueFlagBits::eGraphics | ::vk::QueueFlagBits::eCompute,
		::vk::QueueFlagBits::eCompute,
		::vk::QueueFlagBits::eTransfer,
	};
	bool useDepthStencil = true;
	bool useDebugLayers = false;                                       // whether to use vulkan debug layers

	void setVkVersion( int major, int minor, int patch ){
		vkVersion = ( major << 22 ) | ( minor << 12 ) | patch;
	}

	int getVkVersionMajor(){
		return ( ( vkVersion >> 22 ) & ( 0x3ff ) ); // 10 bit
	}

	int getVersionMinor(){
		return ( ( vkVersion >> 12 ) & ( 0x3ff ) ); // 10 bit
	}

	int getVersionPatch(){
		return ( ( vkVersion >> 0 ) & ( 0xfff ) );
	}
};

struct RendererProperties
{
	::vk::Instance                       instance                       = nullptr;  // vulkan loader instance
	::vk::Device                         device                         = nullptr;  // virtual device
	::vk::PhysicalDevice                 physicalDevice                 = nullptr;  // actual GPU
	::vk::PhysicalDeviceProperties       physicalDeviceProperties       = {};
	::vk::PhysicalDeviceMemoryProperties physicalDeviceMemoryProperties = {};
	std::vector<::vk::QueueFlags>        queueFlags;                    // << Flags used for requested queue n
	std::vector<uint32_t>                queueFamilyIndices;            // << Queue family index for requested queue n
	uint32_t                             graphicsFamilyIndex            = ~( uint32_t( 0 ) );
	uint32_t                             transferFamilyIndex            = ~( uint32_t( 0 ) );
	uint32_t                             computeFamilyIndex             = ~( uint32_t( 0 ) );
	uint32_t                             sparseBindingFamilyIndex       = ~( uint32_t( 0 ) );
};

struct TransferSrcData
{
	void * pData ;
	::vk::DeviceSize numElements;
	::vk::DeviceSize numBytesPerElement;
};

struct ImageTransferSrcData 
{
	void * pData;             //< pointer to pixel data
	::vk::DeviceSize          numBytes;
	::vk::ImageType           imageType   { ::vk::ImageType::e2D };
	::vk::Format              format      { ::vk::Format::eR8G8B8A8Unorm };
	::vk::Extent3D            extent      { 0, 0, 1 };
	uint32_t                  mipLevels   { 1 };
	uint32_t                  arrayLayers { 1 };
	::vk::SampleCountFlagBits samples     { ::vk::SampleCountFlagBits::e1 };
};

struct BufferRegion
{
	::vk::Buffer buffer = nullptr;
	::vk::DeviceSize offset = 0;
	::vk::DeviceSize range = VK_WHOLE_SIZE;
	uint64_t numElements = 0;
};

// get memory allocation info for best matching memory type that matches any of the type bits and flags
static inline bool getMemoryAllocationInfo(
	const ::vk::MemoryRequirements& memReqs,
	::vk::MemoryPropertyFlags memProps,
	::vk::PhysicalDeviceMemoryProperties physicalMemProperties,
	::vk::MemoryAllocateInfo& memInfo ) {
	if ( !memReqs.size ){
		memInfo.allocationSize = 0;
		memInfo.memoryTypeIndex = ~0;
		return true;
	}

	// Find an available memory type that satisfies the requested properties.
	uint32_t memoryTypeIndex;
	for ( memoryTypeIndex = 0; memoryTypeIndex < physicalMemProperties.memoryTypeCount; ++memoryTypeIndex ){
		if ( ( memReqs.memoryTypeBits & ( 1 << memoryTypeIndex ) ) &&
			( physicalMemProperties.memoryTypes[memoryTypeIndex].propertyFlags & memProps ) == memProps ){
			break;
		}
	}
	if ( memoryTypeIndex >= physicalMemProperties.memoryTypeCount ){
		ofLogError() << "memorytypeindex not found" ;
		return false;
	}

	memInfo.allocationSize = memReqs.size;
	memInfo.memoryTypeIndex = memoryTypeIndex;

	return true;
}

struct DescriptorSetData_t
{
	// Everything a possible descriptor binding might contain.
	// Type of decriptor decides which values will be used.
	struct DescriptorData_t
	{
		::vk::Sampler        sampler;                                                 // |
		::vk::ImageView      imageView;                                               // | > keep in this order, so we can pass address for sampler as descriptorImageInfo
		::vk::ImageLayout    imageLayout = ::vk::ImageLayout::eShaderReadOnlyOptimal; // |
		::vk::DescriptorType type = ::vk::DescriptorType::eUniformBufferDynamic;
		::vk::Buffer         buffer;                                                  // |
		::vk::DeviceSize     offset = 0;                                              // | > keep in this order, as we can cast this to a DescriptorBufferInfo
		::vk::DeviceSize     range = 0;                                               // |
		uint32_t             bindingNumber = 0; // <-- may be sparse, may repeat (for arrays of images bound to the same binding), but must increase be monotonically (may only repeat or up over the series inside the samplerBindings vector).
		uint32_t             arrayIndex = 0;    // <-- must be in sequence for array elements of same binding
	};


	// Ordered list of all bindings belonging to this descriptor set
	// We use this to calculate a hash of descriptorState. This must 
	// be tightly packed - that's why we use a vector.
	// 
	// !!!! index is not binding number, as arrayed bindings will be serialized.
	std::vector<DescriptorData_t> descriptors;

	// Compile-time static assert makes sure DescriptorData can be
	// successfully hashed.
	static_assert( (
		+ sizeof( DescriptorData_t::type )
		+ sizeof( DescriptorData_t::sampler )
		+ sizeof( DescriptorData_t::imageView )
		+ sizeof( DescriptorData_t::imageLayout )
		+ sizeof( DescriptorData_t::bindingNumber )
		+ sizeof( DescriptorData_t::buffer )
		+ sizeof( DescriptorData_t::offset )
		+ sizeof( DescriptorData_t::range )
		+ sizeof( DescriptorData_t::arrayIndex )
		) == sizeof( DescriptorData_t ), "DescriptorData_t is not tightly packed. It must be tightly packed for hash calculations." );


	std::vector<std::vector<uint8_t>>      dynamicUboData;        // temp storage for uniform data, one vector of bytes per ubo
	std::vector<uint32_t>                  dynamicBindingOffsets;
	std::vector<::vk::DescriptorImageInfo> imageAttachment;
	std::vector<of::vk::BufferRegion>      bufferAttachment;
};

// ----------

struct UniformId_t
{
	/*

	A Uniform Id is a unique key to identify a uniform.
	Multiple uniform Ids may point to the same descriptor in case the descriptor is an ubo and has members, for example

	It contains information that tells you where to find corresponding data.

	* setIndex: index into a vector of DescriptorSetData_t for this shader
	* descriptorIndex: index into the vector of descriptors of the above DescriptorSetData_t
	* auxIndex: index into vector of auxiliary data, which vector is depending on the type of the descriptor returned above
	* dataRange:  in case the descriptor is an UBO, max size in bytes for the ubo member field
	* dataOffset: in case the descriptor is an UBO, offset into the Ubo's memory to get to the first byte owned by the member field

	The shader's mUniformDictionary has a mapping between uniform names and uniform IDs


	*/

	union
	{
		// This is currently tightly packed to span 64 bits, but it should be possible to 
		// make it span 128 bits if necessary.

		uint64_t id = 0;
		struct
		{
			uint64_t setIndex        :  3;    // 0 ..      7 (maxBoundDescriptorSets is 8)
			uint64_t descriptorIndex : 14;    // 0 .. 16'383 (index into DescriptorData_t::descriptors, per set)
			uint64_t dataOffset      : 16;    // 0 .. 65'536 (offset within range for ubo members, will always be smaller or equal range)
			uint64_t dataRange       : 16;    // 0 .. 65'536 (max number of bytes per ubo)
			uint64_t auxDataIndex    : 15;    // 0 .. 32'767 (index into helper data vectors per descriptor type per set)
		};
	};

	friend
		inline bool operator < ( UniformId_t const & lhs, UniformId_t const & rhs ){
		return lhs.id < rhs.id;
	};
};

static_assert( sizeof( UniformId_t ) == sizeof( uint64_t ), "UniformId_t is not proper size." );


} // end namespace of::vk
} // end namespace of
