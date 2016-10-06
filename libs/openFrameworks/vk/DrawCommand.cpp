#include "vk/DrawCommand.h"
#include "vk/RenderBatch.h"
#include "vk/Shader.h"

using namespace of::vk;

// setup all non-transient state for this draw object

// current ubo values are stored with draw command

// think about it as immutable DATA versus STATE - we want immutable DATA
// not state. DATA is Plain Old Data - and this is how the draw command 
// must store itself.

// ----------------------------------------------------------------------


void DrawCommand::setup(const GraphicsPipelineState& pipelineState){
	
	mPipelineState = pipelineState;
	
	// Initialise Ubo blobs with default values, based on 
	// default values received from Shader. 
	//
	// Shader should provide us with values to initialise, because
	// these values depend on the shader, and the shader knows the
	// uniform variable types.

	const auto & descriptorSetsInfo = mPipelineState.getShader()->getDescriptorSetsInfo();
	const auto & shaderUniforms = mPipelineState.getShader()->getUniforms();

	mDescriptorSetData.reserve( descriptorSetsInfo.size() );

	// we need to query the shader for uniforms - 
	// but because uniforms are independent of sets, 
	// this is slightly more complicated.


	for ( auto&di : descriptorSetsInfo ){
		DescriptorSetData_t tmpDescriptorSetData;

		auto & bindingsVec = tmpDescriptorSetData.descriptorBindings;

		bindingsVec.reserve( di.bindings.size() );

		size_t numDynamicUbos = 0;

		for ( auto & binding : di.bindings ){
			if ( binding.descriptorType == ::vk::DescriptorType::eUniformBufferDynamic ){
				++numDynamicUbos;
				tmpDescriptorSetData.dynamicBindingOffsets.insert( { binding.binding, 0 } );
				tmpDescriptorSetData.dynamicUboData.insert( { binding.binding,{} } );
			}
			for ( uint32_t arrayIndex = 0; arrayIndex != binding.descriptorCount; ++arrayIndex ){
				DescriptorSetData_t::DescriptorData_t bindingData;
				bindingData.bindingNumber = binding.binding;
				bindingData.arrayIndex = arrayIndex;
				bindingData.type = binding.descriptorType;
				bindingsVec.emplace_back( std::move( bindingData ) );
			}
		}

		mDescriptorSetData.emplace_back( std::move( tmpDescriptorSetData ) );
	}

	// ------| invariant: descriptor set data has been transferred from shader for all descriptor sets

	// reserve storage for dynamic uniform data for each uniform entry
	// over all sets - then build up a list of ubos.
	for ( const auto & uniform : shaderUniforms ){
		mDescriptorSetData[uniform.second.setNumber].dynamicUboData[uniform.second.setLayoutBinding.binding].resize( uniform.second.uboRange.storageSize, 0 );
		for ( const auto & uniformMemberPair : uniform.second.uboRange.subranges ){
			// add with combined name - this should always work
			mUniformMembers.insert( { uniform.first + "." + uniformMemberPair.first ,uniformMemberPair.second } );
			// add only with member name - this might work, but if members share the same name, we're in trouble.
			mUniformMembers.insert( { uniformMemberPair.first ,uniformMemberPair.second } );
		}

	}

	// parse shader info to find out how many buffers to reserve for vertex attributes.

	const auto & vertexInfo = mPipelineState.getShader()->getVertexInfo();

	size_t numAttributes = vertexInfo.attribute.size();

	mVertexBuffers.resize( numAttributes, nullptr );
	mVertexOffsets.resize( numAttributes, 0 );

	for ( size_t i = 0; i != numAttributes; ++i ){
		ofLog() << std::setw( 4 ) << i << ":" << vertexInfo.attributeNames[i];
	}
}


// ------------------------------------------------------------

void DrawCommand::commitUniforms(const std::unique_ptr<Allocator>& alloc ){
	for ( auto & descriptorSetData : mDescriptorSetData ){
		for ( const auto & dataPair : descriptorSetData.dynamicUboData ){
			
			const auto & dataVec = dataPair.second;
			const auto & bindingNumber = dataPair.first;
			
			::vk::DeviceSize offset;
			void * dataP = nullptr;
			
			// allocate data on gpu
			if ( alloc->allocate( dataVec.size(), offset ) && alloc->map( dataP ) ){
				
				// copy data to gpu
				memcpy( dataP, dataVec.data(), dataVec.size() );

				// update dynamic binding offsets for this binding
				descriptorSetData.dynamicBindingOffsets[bindingNumber] = offset;

				// store the buffer 
				descriptorSetData.descriptorBindings[bindingNumber].buffer = alloc->getBuffer();
				descriptorSetData.descriptorBindings[bindingNumber].range = dataVec.size();

			} else{
				ofLogError() << "commitUniforms: could not allocate transient memory.";
			}
		}
	}
}

// ------------------------------------------------------------

void DrawCommand::commitMeshAttributes( const std::unique_ptr<Allocator>& alloc ){
	// check if current draw command has a mesh - if yes, upload mesh data to buffer memory.
	if ( mMsh ){
		auto &mesh = *mMsh;

		if ( !mesh.hasVertices() || !allocAndSetAttribute( "inPos", mesh.getVertices(), alloc ) ){
			ofLogError() << "Mesh has no vertices.";
			return;
		} else {
			mNumVertices = mesh.getVertices().size();
		}

		if ( mesh.hasColors() && mesh.usingColors()){
			allocAndSetAttribute( "inColor", mesh.getColors(), alloc );
		}
		if ( mesh.hasNormals() && mesh.usingNormals() ){
			allocAndSetAttribute( "inNormal", mesh.getNormals(), alloc );
		}
		if ( mesh.hasTexCoords() && mesh.usingTextures() ){
			allocAndSetAttribute( "inTexCoords", mesh.getTexCoords(), alloc );
		}

		if ( mesh.hasIndices() && mesh.usingIndices() ){
			const auto & indices = mesh.getIndices();
			const auto byteSize = sizeof( indices[0] ) * indices.size();

			void * dataP = nullptr;
			::vk::DeviceSize offset = 0;

			if ( alloc->allocate( byteSize, offset ) && alloc->map( dataP ) ){
				memcpy( dataP, indices.data(), byteSize );
				setIndices( alloc->getBuffer(), offset );
				mNumIndices = indices.size();
			}
		} else{
			mIndexBuffer = nullptr;
			mIndexOffsets = 0;
		}

	}
}

// ------------------------------------------------------------

void DrawCommand::setMesh(const shared_ptr<ofMesh> & msh_ ){
	mMsh = msh_;
}

// ------------------------------------------------------------