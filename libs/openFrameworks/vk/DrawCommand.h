#pragma once
#include "vulkan/vulkan.hpp"
#include "vk/Shader.h"
#include "vk/Pipeline.h"



namespace of {

class BufferObject;		 // ffdecl.
class IndexBufferObject; // ffdecl.
class DrawCommand;
class RenderBatch;

// ----------------------------------------------------------------------

class DrawCommandInfo {
	
	friend class DrawCommand;
	friend class RenderBatch;
	
	struct DescriptorSetData
	{
		// Sparse map from binding number to blob of UBO data
		// Each vector serialises data values of all members within same UBO
		std::map<uint32_t, std::vector<uint8_t>>       uboData;
		
		// map everything needed to bind an image
		// to sparse binding number and binding array index
		struct SamplerBindings
		{
			::vk::Sampler        sampler       = 0;
			::vk::ImageView      imageView     = 0;
			::vk::ImageLayout    imageLayout   = ::vk::ImageLayout::eUndefined;
			uint32_t             bindingNumber = 0; // <-- may be sparse, may repeat (for arrays of images bound to the same binding), but must increase be monotonically (may only repeat or up over the series inside the samplerBindings vector).
			uint64_t             arrayIndex    = 0;
		};
		
		// Sparse list of bindings 
		std::vector<SamplerBindings> samplerBindings; // must be tightly packed.

		static_assert( (
			+ sizeof( SamplerBindings::sampler )
			+ sizeof( SamplerBindings::imageView )
			+ sizeof( SamplerBindings::imageLayout )
			+ sizeof( SamplerBindings::bindingNumber )
			+ sizeof( SamplerBindings::arrayIndex )
			) == sizeof( SamplerBindings ), "SamplerBindings layout is not tightly packed, but that's needed for hash calc." );

	};

	// Bindings data, (vector index == set number) -- indices must not be sparse!
	std::vector<DescriptorSetData> descriptorSetState;

	// Map from attribute location to buffer of attribute data
	std::map<uint32_t, BufferObject * >                attributeData;
	IndexBufferObject *                                indexData = nullptr;

	vk::GraphicsPipelineState                          pipeline;

public:

	vk::GraphicsPipelineState& getPipeline(){
		pipeline.mDirty = true; // invalidate hash
		return pipeline;
	}

	const vk::GraphicsPipelineState& getPipelineC(){
		return pipeline;
	}

	const std::vector<DescriptorSetData>& getDescriptorSetState(){
		return descriptorSetState;
	}
};

// ----------------------------------------------------------------------

class DrawCommand
{
	// a draw command has everything needed to draw an object
	const DrawCommandInfo mDrawCommandInfo;
	// map from binding number to ubo data state

	DrawCommand() = delete;
	
	uint64_t mPipelineHash;

public:

	const DrawCommandInfo& getInfo(){
		return mDrawCommandInfo;
	}

	// setup all non-transient state for this draw object
	DrawCommand( const DrawCommandInfo& dcs );

	// set data for upload to ubo
	void setUboData( const std::string uboName, const std::vector<uint8_t> data ){};

};




} // namespace of