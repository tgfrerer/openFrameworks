#include "vk/DrawCommand.h"
#include "vk/RenderBatch.h"
#include "vk/Shader.h"

// setup all non-transient state for this draw object

// current ubo values are stored with draw command

// think about it as immutable DATA versus STATE - we want immutable DATA
// not state. DATA is Plain Old Data - and this is how the draw command 
// must store itself.

// ----------------------------------------------------------------------

of::DrawCommand::DrawCommand( const DrawCommandInfo & dcs )
	:mDrawCommandInfo( dcs ){

	// Initialise Ubo blobs with default values, based on 
	// default values received from Shader. 
	//
	// Shader should provide us with values to initialise, because
	// these values depend on the shader, and the shader knows the
	// uniform variable types.

	const auto & descriptorSetsInfo = mDrawCommandInfo.getPipeline().getShader()->getDescriptorSetsInfo();
	const auto & shaderUniforms     = mDrawCommandInfo.getPipeline().getShader()->getUniforms();

	mDescriptorSetData.reserve( descriptorSetsInfo.size() );
	
	// we need to query the shader for uniforms - 
	// but because uniforms are independent of sets, 
	// this is slightly more complicated.


	for ( auto&di : descriptorSetsInfo ){
		DescriptorSetData_t tmpDescriptorSetData;
		
		auto & bindingsVec = tmpDescriptorSetData.descriptorBindings;

		bindingsVec.reserve(di.bindings.size());
		
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
			mUniformMembers.insert( {uniform.first + "." + uniformMemberPair.first ,uniformMemberPair.second } );
			// add only with member name - this might work, but if members share the same name, we're in trouble.
			mUniformMembers.insert( { uniformMemberPair.first ,uniformMemberPair.second } );
		}
		
	}

	// parse shader info to find out how many buffers to reserve for vertex attributes.

	const auto & vertexInfo = mDrawCommandInfo.getPipeline().getShader()->getVertexInfo();

	size_t numAttributes = vertexInfo.attribute.size();
	
	mVertexBuffers.resize( numAttributes, nullptr );
	mVertexOffsets.resize( numAttributes, 0 );

	for ( size_t i = 0; i != numAttributes; ++i ){
		ofLog() << std::setw(4) << i <<  ":" << vertexInfo.attributeNames[i];
	}
}


// ------------------------------------------------------------

void of::DrawCommand::commitUniforms(const std::unique_ptr<of::vk::Allocator>& alloc ){
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
				descriptorSetData.descriptorBindings[bindingNumber].range = VK_WHOLE_SIZE;

			} else{
				ofLogError() << "commitUniforms: could not allocate transient memory.";
			}
			
		}
	}
}

// ------------------------------------------------------------

void of::DrawCommand::commitMeshAttributes( const std::unique_ptr<of::vk::Allocator>& alloc ){
	// check if current draw command has a mesh - if yes, upload mesh data to buffer memory.
	if ( mMsh ){
		auto &mesh = *mMsh;

		::vk::DeviceSize offset = 0;
		void * dataP            = nullptr;

		if ( !mesh.hasVertices() ){
			ofLogError() << "Mesh has no vertices.";
			return;
		} else {
			const auto & vertices = mesh.getVertices(); 
			// allocate data on gpu
			if ( alloc->allocate( vertices.size(), offset ) && alloc->map( dataP )){
				alloc->map( dataP );
				memcpy( dataP, vertices.data(), sizeof(vertices[0]) * vertices.size() );
				setAttribute( "inPos", alloc->getBuffer(), offset );
				mNumVertices = vertices.size();
			}
		}

		if ( mesh.hasColors() && mesh.usingColors()){
			const auto & colors = mesh.getColors();
			// allocate data on gpu
			if ( alloc->allocate( colors.size(), offset ) && alloc->map( dataP ) ){
				memcpy( dataP, colors.data(), sizeof( colors[0] ) * colors.size() );
				setAttribute( "inColor", alloc->getBuffer(), offset );
			}
		}
		if ( mesh.hasNormals() && mesh.usingNormals() ){
			const auto & normals = mesh.getColors();
			// allocate data on gpu
			if ( alloc->allocate( normals.size(), offset ) && alloc->map( dataP ) ){
				memcpy( dataP, normals.data(), sizeof( normals[0] ) * normals.size() );
				setAttribute( "inNormal", alloc->getBuffer(), offset );
			}
		}
		if ( mesh.hasTexCoords() && mesh.usingTextures() ){
			const auto & texCoords = mesh.getTexCoords();
			// allocate data on gpu
			if ( alloc->allocate( texCoords.size(), offset ) && alloc->map( dataP ) ){
				memcpy( dataP, texCoords.data(), sizeof( texCoords[0] ) * texCoords.size() );
				setAttribute( "inTexCoord", alloc->getBuffer(), offset );
			}
		}

		if ( mesh.hasIndices() && mesh.usingIndices() ){
			const auto & indices = mesh.getIndices();
			// allocate data on gpu
			if ( alloc->allocate( indices.size(), offset ) && alloc->map( dataP ) ){
				
				memcpy( dataP, indices.data(), sizeof( indices[0] ) * indices.size() );
				setIndices( alloc->getBuffer(), offset );
				mNumIndices = indices.size();
			}
		}

	}
}

// ------------------------------------------------------------


void of::DrawCommand::setAttribute( std::string name_, ::vk::Buffer buffer_, ::vk::DeviceSize offset_ ){
	
	const auto & vertexInfo = mDrawCommandInfo.getPipeline().getShader()->getVertexInfo();
	
	const auto & attributeNames = vertexInfo.attributeNames;
	auto it = std::find(attributeNames.begin(),attributeNames.end(),name_);
	
	if ( it != attributeNames.end() ){
		size_t index = (it - attributeNames.begin());
		mVertexBuffers[index] = buffer_;
		mVertexOffsets[index] = offset_;
	}
}

// ------------------------------------------------------------

void of::DrawCommand::setIndices( ::vk::Buffer buffer_, ::vk::DeviceSize offset_ ){
	mIndexBuffer.resize( 1, nullptr );
	mIndexOffsets.resize( 1, 0 );

	mIndexBuffer[0] = buffer_;
	mIndexOffsets[0] = offset_;
}

// ------------------------------------------------------------

void of::DrawCommand::setMesh(const shared_ptr<ofMesh> & msh_ ){
	mMsh = msh_;
}

// ------------------------------------------------------------