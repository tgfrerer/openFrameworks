#pragma once
#include "vulkan/vulkan.hpp"
#include "vk/Shader.h"
#include "vk/Pipeline.h"



namespace of {

class DrawCommand;
class RenderBatch;

// ----------------------------------------------------------------------

class DrawCommandInfo {

	friend class DrawCommand;
	friend class RenderBatch;
	friend class RenderContext;


	// pipeline state for a draw command
	// this also contains the shader
	vk::GraphicsPipelineState pipeline;

public:

	// Get a reference to pipeline for modifying it
	// Only friends should be allowed to do this.
	vk::GraphicsPipelineState& modifyPipeline(){
		pipeline.mDirty = true; // invalidate hash
		return pipeline;
	}

	const vk::GraphicsPipelineState& getPipeline() const {
		return pipeline;
	}
	const std::vector<uint64_t>& getSetLayoutKeys() const{
		return pipeline.getShader()->getDescriptorSetLayoutKeys();
	}

	const std::vector<std::shared_ptr<::vk::DescriptorSetLayout>>& getDescriptorSetLayouts() const{
		return pipeline.getShader()->getDescriptorSetLayouts();
	}
};

// ----------------------------------------------------------------------

class DrawCommand
{

public:

	struct DescriptorSetData_t
	{
		// Everything a possible descriptor binding might contain.
		// Type of decriptor decides which values will be used.
		struct DescriptorData_t
		{
			::vk::Sampler        sampler = 0;								  // |
			::vk::ImageView      imageView = 0;								  // | > keep in this order, so we can pass address for sampler as descriptorImageInfo
			::vk::ImageLayout    imageLayout = ::vk::ImageLayout::eUndefined; // |
			::vk::DescriptorType type = ::vk::DescriptorType::eUniformBufferDynamic;
			::vk::Buffer         buffer = 0;								  // |
			uint32_t             offset = 0;								  // | > keep in this order, as we can cast this to a DescriptorBufferInfo
			uint32_t             range = 0;									  // |
			uint32_t             bindingNumber = 0; // <-- may be sparse, may repeat (for arrays of images bound to the same binding), but must increase be monotonically (may only repeat or up over the series inside the samplerBindings vector).
			uint32_t             arrayIndex = 0;	// <-- must be in sequence for array elements of same binding
		};


		// Sparse list of all bindings belonging to this descriptor set
		// We use this to calculate a hash of descriptorState. This must 
		// be tightly packed.
		std::vector<DescriptorData_t> descriptorBindings;

		// Compile-time error checking to make sure DescriptorData can be
		// successfully hashed.
		static_assert( (
			+sizeof( DescriptorData_t::type )
			+ sizeof( DescriptorData_t::sampler )
			+ sizeof( DescriptorData_t::imageView )
			+ sizeof( DescriptorData_t::imageLayout )
			+ sizeof( DescriptorData_t::bindingNumber )
			+ sizeof( DescriptorData_t::buffer )
			+ sizeof( DescriptorData_t::offset )
			+ sizeof( DescriptorData_t::range )
			+ sizeof( DescriptorData_t::arrayIndex )
			) == sizeof( DescriptorData_t ), "DescriptorData_t is not tightly packed. It must be tightly packed for hash calculations." );

		std::vector<uint32_t> dynamicBindingOffsets; // dynamic binding offsets for ubo bindings within this descriptor set

		// !TODO we probably need temporary byte storage in here for data to be 
		// committed to dynamic ubos... This data will be uploaded to GPU 
		// when the draw call gets submitted to the batch - and this is 
		// when dynamicBindingOffsets will get set. There needs to be one
		// vector entry for each eUniformBufferDynamic binding.
		std::vector<std::vector<uint8_t>> dynamicUboData;

	};

private:
	
	// a draw command has everything needed to draw an object
	const DrawCommandInfo mDrawCommandInfo;
	// map from binding number to ubo data state

	DrawCommand() = delete;
	
	uint64_t mPipelineHash = 0;

	// Bindings data, (vector index == set number) -- indices must not be sparse!
	std::vector<DescriptorSetData_t> mDescriptorSetData;

	// offsets into buffer for vertex attribute data
	std::vector<::vk::DeviceSize> vertexOffsets;
	// offsets into buffer for index data - this is optional
	std::vector<::vk::DeviceSize> indexOffsets;


public:

	const DrawCommandInfo& getInfo(){
		return mDrawCommandInfo;
	}

	const DescriptorSetData_t& getDescriptorSetData(size_t setId_){
		return mDescriptorSetData[setId_];
	}

	// setup all non-transient state for this draw object
	DrawCommand( const DrawCommandInfo& dcs );

	// set data for upload to ubo - data is stored locally 
	// until draw command is submitted
	
	//!TODO: implement ubo upload
	void setUboData( const std::string uboName, const std::vector<uint8_t> data ){};

};




} // namespace of