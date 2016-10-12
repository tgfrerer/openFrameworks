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

				if ( binding.descriptorType == ::vk::DescriptorType::eCombinedImageSampler ){
					// store image attachment 
					tmpDescriptorSetData.imageAttachment[bindingsVec.size()] = {};
				}

				bindingsVec.emplace_back( std::move( bindingData ) );
			}
		}

		mDescriptorSetData.emplace_back( std::move( tmpDescriptorSetData ) );
	}

	// ------| invariant: descriptor set data has been transferred from shader for all descriptor sets
	
	const auto & shaderUniforms = mPipelineState.getShader()->getUniforms();

	for ( const auto & uniformPair : shaderUniforms ){
		const auto & uniformName = uniformPair.first;
		const auto & uniform     = uniformPair.second;
		if ( uniform.setLayoutBinding.descriptorType == ::vk::DescriptorType::eUniformBufferDynamic ){
			mDescriptorSetData[uniform.setNumber].dynamicUboData[uniform.setLayoutBinding.binding].resize( uniform.uboRange.storageSize, 0 );
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

void DrawCommand::commitUniforms(const std::unique_ptr<BufferAllocator>& alloc ){
	for ( auto & descriptorSetData : mDescriptorSetData ){

		for ( const auto & dataPair : descriptorSetData.imageAttachment ){
			const auto & bindingNumber = dataPair.first;
			const auto & imageInfo     = dataPair.second;

			descriptorSetData.descriptorBindings[bindingNumber].sampler     = imageInfo.sampler;
			descriptorSetData.descriptorBindings[bindingNumber].imageView   = imageInfo.imageView;
			descriptorSetData.descriptorBindings[bindingNumber].imageLayout = imageInfo.imageLayout;
		}

		for ( const auto & dataPair : descriptorSetData.dynamicUboData ){

			const auto & bindingNumber = dataPair.first;
			const auto & dataVec       = dataPair.second;

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
		} // end foreach descriptorSetData.dynamicUboData
	} // end foreach mDescriptorSetData
}

// ------------------------------------------------------------

void DrawCommand::commitMeshAttributes( const std::unique_ptr<BufferAllocator>& alloc ){
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