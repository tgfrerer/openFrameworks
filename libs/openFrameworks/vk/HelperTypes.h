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
	uint32_t                             graphicsFamilyIndex            = ~( uint32_t( 0 ) );
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