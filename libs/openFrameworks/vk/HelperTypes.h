#pragma once

#include <vulkan/vulkan.hpp>
#include "ofLog.h"

namespace of{
namespace vk{

struct RendererProperties
{
	::vk::Instance                       instance                       = nullptr;  // vulkan loader instance
	::vk::Device                         device                         = nullptr;  // virtual device
	::vk::PhysicalDevice                 physicalDevice                 = nullptr;  // actual GPU
	::vk::PhysicalDeviceProperties       physicalDeviceProperties       = {};
	::vk::PhysicalDeviceMemoryProperties physicalDeviceMemoryProperties = {};
	uint32_t                             graphicsFamilyIndex            = 0;
};

struct TransferSrcData
{
	void * pData ;
	::vk::DeviceSize numElements;
	::vk::DeviceSize numBytesPerElement;
};

struct ImageTransferSrcData 
{
	void * pData;
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
static bool getMemoryAllocationInfo(
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

} // end namespace of::vk
} // end namespace of