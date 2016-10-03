#pragma once
#include "vulkan/vulkan.hpp"
#include "vk/Shader.h"
#include "vk/Pipeline.h"
#include "ofMesh.h"

namespace of{
namespace vk{

class DrawCommand;	   // ffdecl.
class RenderBatch;	   // ffdecl.
class Allocator;	   // ffdecl.


// ----------------------------------------------------------------------

class DrawCommand
{
	friend class RenderBatch;
	
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
	GraphicsPipelineState mPipelineState;
	// map from binding number to ubo data state

	// lookup table from uniform name to storage info for dynamic ubos
	// TODO: maybe mUniformMembers should move to shader.
	std::map<std::string, Shader::UboMemberSubrange> mUniformMembers;

private:      /* transient data */

	uint64_t mPipelineHash = 0;

	// Bindings data for descriptorSets, (vector index == set number) -- indices must not be sparse!
	std::vector<DescriptorSetData_t> mDescriptorSetData;

	// vector of buffers holding vertex attribute data
	std::vector<::vk::Buffer> mVertexBuffers;

	// offsets into buffer for vertex attribute data
	std::vector<::vk::DeviceSize> mVertexOffsets;

	// 1-or-0 element buffer of indices for this draw command
	::vk::Buffer mIndexBuffer = nullptr;

	// offsets into buffer for index data - this is optional
	::vk::DeviceSize mIndexOffsets = 0;

	uint32_t mNumIndices = 0;
	uint32_t mNumVertices = 0;

	std::shared_ptr<ofMesh> mMsh; /* optional */

public:

	void setup(const GraphicsPipelineState& pipelineState);

	const GraphicsPipelineState&         getPipelineState() const;
	
	const DescriptorSetData_t&           getDescriptorSetData( size_t setId_ ) const;

	const std::vector<::vk::DeviceSize>& getVertexOffsets();
	const std::vector<::vk::Buffer>&     getVertexBuffers();
	
	const ::vk::DeviceSize&              getIndexOffsets();
	const ::vk::Buffer&                  getIndexBuffer();
	
	const uint32_t                       getNumIndices();
	void                                 setNumVertices( uint32_t numVertices );
	
	const uint32_t                       getNumVertices();
	void                                 setNumIndices( uint32_t numIndices );

	// set data for upload to ubo - data is stored locally 
	// until draw command is submitted
	void commitUniforms( const std::unique_ptr<Allocator>& alloc_ );

	void commitMeshAttributes( const std::unique_ptr<Allocator>& alloc_ );

	void setMesh( const shared_ptr<ofMesh>& msh_ );

	void setAttribute( const std::string& name_, ::vk::Buffer buffer, ::vk::DeviceSize offset );

	void setIndices( ::vk::Buffer buffer, ::vk::DeviceSize offset );

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

} // namespace 
} // end namespace of

// ------------------------------------------------------------
// Inline getters and setters

inline const of::vk::GraphicsPipelineState & of::vk::DrawCommand::getPipelineState() const{
	return mPipelineState;
}

inline const of::vk::DrawCommand::DescriptorSetData_t & of::vk::DrawCommand::getDescriptorSetData( size_t setId_ ) const{
	return mDescriptorSetData[setId_];
}

inline const std::vector<::vk::DeviceSize>& of::vk::DrawCommand::getVertexOffsets(){
	return mVertexOffsets;
}

inline const::vk::DeviceSize & of::vk::DrawCommand::getIndexOffsets(){
	return mIndexOffsets;
}

inline const std::vector<::vk::Buffer>& of::vk::DrawCommand::getVertexBuffers(){
	return mVertexBuffers;
}

inline const::vk::Buffer & of::vk::DrawCommand::getIndexBuffer(){
	return mIndexBuffer;
}

inline const uint32_t of::vk::DrawCommand::getNumIndices(){
	return mNumIndices;
}

inline const uint32_t of::vk::DrawCommand::getNumVertices(){
	return mNumVertices;
}

inline void of::vk::DrawCommand::setNumVertices( uint32_t numVertices ){
	mNumVertices = numVertices;
}

inline void of::vk::DrawCommand::setNumIndices( uint32_t numIndices ){
	mNumIndices = numIndices;
}
