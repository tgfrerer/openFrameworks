#pragma once
#include "vulkan/vulkan.hpp"
#include "vk/Shader.h"
#include "vk/Pipeline.h"
#include "ofMesh.h"

namespace of {

class DrawCommand;	   // ffdecl.
class RenderBatch;	   // ffdecl.

namespace vk{
class Allocator;	   // ffdecl.
}

// ----------------------------------------------------------------------

class DrawCommandInfo {

	friend class DrawCommand;
	friend class RenderBatch;
	friend class RenderContext;

	// Pipeline state for a draw command
	// This also contains the shader.
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
			::vk::DeviceSize     offset = 0;								  // | > keep in this order, as we can cast this to a DescriptorBufferInfo
			::vk::DeviceSize     range = 0;									  // |
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

		std::map<uint32_t, uint32_t> dynamicBindingOffsets; // dynamic binding offsets for ubo bindings within this descriptor set

		// One vector per binding - vector size is 
		// determined by ubo subrange size. ubo data bindings may be sparse. 
		// Data is uploaded to GPU upon draw.
		// TODO: change this to use vkBufferObjects
		// If the buffer object is dynamic, use dynamic uniform, 
		// and update dynamic binding offset, if it is static, use offset and range.
		std::map<uint32_t, std::vector<uint8_t>> dynamicUboData;

	};

private:
	
	// a draw command has everything needed to draw an object
	const DrawCommandInfo mDrawCommandInfo;
	// map from binding number to ubo data state

	DrawCommand() = delete;
	
	// lookup table from uniform name to storage info for dynamic ubos
	// TODO: maybe mUniformMembers should move to shader.
	std::map<std::string, of::vk::Shader::UboMemberSubrange> mUniformMembers;

private:      /* transient data */

	uint64_t mPipelineHash = 0;

	// Bindings data, (vector index == set number) -- indices must not be sparse!
	std::vector<DescriptorSetData_t> mDescriptorSetData;

	// vector of buffers holding vertex attribute data
	std::vector<::vk::Buffer> mVertexBuffers;
	
	// offsets into buffer for vertex attribute data
	std::vector<::vk::DeviceSize> mVertexOffsets;
	
	// 1-or-0 element buffer of indices for this draw command
	std::vector<::vk::Buffer> mIndexBuffer;

	// offsets into buffer for index data - this is optional
	std::vector<::vk::DeviceSize> mIndexOffsets;

	uint32_t mNumIndices  = 0;
	uint32_t mNumVertices = 0;

	std::shared_ptr<ofMesh> mMsh; /* optional */

public:

	const DrawCommandInfo& getInfo() const {
		return mDrawCommandInfo;
	}

	const DescriptorSetData_t& getDescriptorSetData(size_t setId_) const {
		return mDescriptorSetData[setId_];
	}

	// setup all non-transient state for this draw object
	DrawCommand( const DrawCommandInfo& dcs );

	const std::vector<::vk::DeviceSize>& getVertexOffsets(){
		return mVertexOffsets;
	}
	const std::vector<::vk::DeviceSize>& getIndexOffsets(){
		return mIndexOffsets;
	}
	const std::vector<::vk::Buffer>& getVertexBuffers(){
		return mVertexBuffers;
	}
	const std::vector<::vk::Buffer>& getIndexBuffer(){
		return mIndexBuffer;
	}
	const uint32_t getNumIndices(){
		return mNumIndices;
	}
	const uint32_t getNumVertices(){
		return mNumVertices;
	}

	// set data for upload to ubo - data is stored locally 
	// until draw command is submitted
	void commitUniforms( const std::unique_ptr<of::vk::Allocator>& alloc_ );
	
	void commitMeshAttributes( const std::unique_ptr<of::vk::Allocator>& alloc_ );

	void setMesh( const shared_ptr<ofMesh>& msh_ );

	void setAttribute( std::string name_, ::vk::Buffer buffer, ::vk::DeviceSize offset );
	void setIndices(::vk::Buffer buffer, ::vk::DeviceSize offset );

	// upload uniform data to gpu memory
	template <class T> 
	void setUniform( std::string uniformName, const T& uniformValue_ ){

		const auto foundMemberIt = mUniformMembers.find( uniformName );

		if ( foundMemberIt != mUniformMembers.end() ){
			const auto & memberSubrange = foundMemberIt->second;
			if ( memberSubrange.range < sizeof( T ) ){
				ofLogWarning() << "Could not set uniform '" << uniformName << "': Uniform data size does not match: "
					<< " Expected: " << memberSubrange.range << ", received: " << sizeof( T ) << ".";
				return;
			}
			// --------| invariant: size match, we can copy data into our vector.

			auto & dataVec = mDescriptorSetData[memberSubrange.setNumber].dynamicUboData[memberSubrange.bindingNumber];

			if ( memberSubrange.offset + memberSubrange.range <= dataVec.size() ){
				memcpy( dataVec.data() + memberSubrange.offset, &uniformValue_, memberSubrange.range );
			} else{
				ofLogError() << "Not enough space in local uniform storage. Has this drawcommand been properly initialised?";
			}
		}

	}

};

} // namespace of
