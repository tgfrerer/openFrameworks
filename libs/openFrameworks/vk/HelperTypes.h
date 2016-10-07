#pragma once

#include <vulkan/vulkan.hpp>

namespace of{
namespace vk{

struct TransferSrcData
{
	void * pData;
	::vk::DeviceSize numElements;
	::vk::DeviceSize numBytesPerElement;
};

struct BufferRegion
{
	::vk::Buffer buffer = nullptr;
	::vk::DeviceSize offset = 0;
	::vk::DeviceSize range = VK_WHOLE_SIZE;
	uint64_t numElements = 0;
};

} // end namespace of::vk
} // end namespace of