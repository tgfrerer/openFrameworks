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


void DrawCommand::setup( const GraphicsPipelineState& pipelineState ){

	if ( false == pipelineState.getShader().get() ){
		ofLogError() << "Cannot setup draw command without valid shader inside pipeline.";
		return;
	}

	// --------| invariant: pipeline has shader

	mPipelineState = pipelineState;

	mDescriptorSetData = mPipelineState.getShader()->getDescriptorSetData();
	mUniformDictionary = &mPipelineState.getShader()->getUniformDictionary();

	// parse shader info to find out how many buffers to reserve for vertex attributes.

	const auto & vertexInfo = mPipelineState.getShader()->getVertexInfo();

	size_t numVertexBindings = vertexInfo.bindingDescription.size();

	mVertexBuffers.resize( numVertexBindings, nullptr );
	mVertexOffsets.resize( numVertexBindings, 0 );

}

// ------------------------------------------------------------

void DrawCommand::commitUniforms( const std::unique_ptr<BufferAllocator>& alloc ){

	for ( auto & descriptorSetData : mDescriptorSetData ){

		auto imgInfoIt = descriptorSetData.imageAttachment.begin();
		auto bufferInfoIt = descriptorSetData.bufferAttachment.begin();
		auto dynamicOffsetsIt = descriptorSetData.dynamicBindingOffsets.begin();
		auto dataIt = descriptorSetData.dynamicUboData.begin();

		for ( auto & descriptor : descriptorSetData.descriptors ){

			switch ( descriptor.type ){
			case ::vk::DescriptorType::eSampler:
				break;
			case ::vk::DescriptorType::eCombinedImageSampler:
			{
				descriptor.imageView = imgInfoIt->imageView;
				descriptor.sampler = imgInfoIt->sampler;
				descriptor.imageLayout = imgInfoIt->imageLayout;
				imgInfoIt++;
			}
			break;
			case ::vk::DescriptorType::eSampledImage:
				break;
			case ::vk::DescriptorType::eStorageImage:
				break;
			case ::vk::DescriptorType::eUniformTexelBuffer:
				break;
			case ::vk::DescriptorType::eStorageTexelBuffer:
				break;
			case ::vk::DescriptorType::eUniformBuffer:
				break;
			case ::vk::DescriptorType::eUniformBufferDynamic:
			{
				descriptor.buffer = alloc->getBuffer();
				::vk::DeviceSize offset;
				void * dataP = nullptr;

				const auto & dataVec = *dataIt;
				const auto & dataRange = dataVec.size();

				// allocate memory on gpu
				if ( alloc->allocate( dataRange, offset ) && alloc->map( dataP ) ){

					// copy data from draw command temp storage to gpu
					memcpy( dataP, dataVec.data(), dataRange );

					// update dynamic binding offsets for this binding
					*dynamicOffsetsIt = offset;

				} else{
					ofLogError() << "commitUniforms: could not allocate transient memory.";
				}
				dataIt++;
				dynamicOffsetsIt++;
				descriptor.range = dataRange;
			}
			break;
			case ::vk::DescriptorType::eStorageBuffer:
				break;
			case ::vk::DescriptorType::eStorageBufferDynamic:
			{
				descriptor.buffer = bufferInfoIt->buffer;
				descriptor.range = bufferInfoIt->range;
				*dynamicOffsetsIt = bufferInfoIt->offset;

				bufferInfoIt++;
				dynamicOffsetsIt++;
			}
			break;
			case ::vk::DescriptorType::eInputAttachment:
				break;
			default:
				break;
			} // end switch

		} // end for each descriptor
	} // end foreach mDescriptorSetData
}

// ------------------------------------------------------------

void DrawCommand::commitMeshAttributes( const std::unique_ptr<BufferAllocator>& alloc ){
	// check if current draw command has a mesh - if yes, upload mesh data to buffer memory.
	if ( mMsh ){
		auto &mesh = *mMsh;

		if ( !mesh.hasVertices() ){
			ofLogError() << "Mesh has no vertices.";
			return;
		} else{
			allocAndSetAttribute( "inPos", mesh.getVertices(), alloc );
			mNumVertices = uint32_t( mesh.getVertices().size() );
		}

		if ( mesh.hasColors() && mesh.usingColors() ){
			allocAndSetAttribute( "inColor", mesh.getColors(), alloc );
		}
		if ( mesh.hasNormals() && mesh.usingNormals() ){
			allocAndSetAttribute( "inNormal", mesh.getNormals(), alloc );
		}
		if ( mesh.hasTexCoords() && mesh.usingTextures() ){
			allocAndSetAttribute( "inTexCoord", mesh.getTexCoords(), alloc );
		}

		if ( mesh.hasIndices() && mesh.usingIndices() ){
			const auto & indices = mesh.getIndices();
			const auto byteSize = sizeof( indices[0] ) * indices.size();

			void * dataP = nullptr;
			::vk::DeviceSize offset = 0;

			if ( alloc->allocate( byteSize, offset ) && alloc->map( dataP ) ){
				memcpy( dataP, indices.data(), byteSize );
				setIndices( alloc->getBuffer(), offset );
				mNumIndices = uint32_t( indices.size() );
			}

		} else{
			mIndexBuffer = nullptr;
			mIndexOffsets = 0;
		}

	}
}

// ------------------------------------------------------------

DrawCommand & DrawCommand::setMesh( const shared_ptr<ofMesh> & msh_ ){
	mMsh = msh_;
	return *this;
}

// ------------------------------------------------------------

template<typename T>
DrawCommand & DrawCommand::allocAndSetAttribute( const std::string & attrName_, const std::vector<T>& vec, const std::unique_ptr<BufferAllocator>& alloc ){
	
	size_t index = 0;
	
	if ( mPipelineState.getShader()->getAttributeBinding( attrName_, index ) ){
		return allocAndSetAttribute( index, vec, alloc );
	}

	// --------| invariant: name was not resolved successfully.

	ofLogWarning()
		<< "Attribute '" << attrName_ << "' could not be found in shader: "
		<< mPipelineState.getShader()->mSettings.sources.at( ::vk::ShaderStageFlagBits::eVertex ).getName();

	return *this;
}

// ------------------------------------------------------------

template<typename T>
DrawCommand & DrawCommand::allocAndSetAttribute( const std::string & attrName_, const T * data, size_t numBytes, const std::unique_ptr<BufferAllocator>& alloc ){

	size_t index = 0;

	if ( mPipelineState.getShader()->getAttributeBinding( attrName_, index ) ){
		return allocAndSetAttribute( index, data, numBytes, alloc );
	}

	// --------| invariant: name was not resolved successfully.

	ofLogWarning()
		<< "Attribute '" << attrName_ << "' could not be found in shader: "
		<< mPipelineState.getShader()->mSettings.sources.at( ::vk::ShaderStageFlagBits::eVertex ).getName();

	return *this;
}

// ------------------------------------------------------------
// upload vertex data to gpu memory
template<typename T>
DrawCommand & DrawCommand::allocAndSetAttribute( const size_t& attribLocation_, const std::vector<T>& vec, const std::unique_ptr<BufferAllocator>& alloc ){
	const auto numBytes = sizeof( vec[0] ) * vec.size();
	return allocAndSetAttribute(attribLocation_, vec.data(), numBytes, alloc);
}

// ------------------------------------------------------------
// upload vertex data to gpu memory
DrawCommand & DrawCommand::allocAndSetAttribute( const size_t& attribLocation_, const void * data, size_t numBytes, const std::unique_ptr<BufferAllocator>& alloc ){
	
	void * dataP = nullptr;
	::vk::DeviceSize offset = 0;
	// allocate data on gpu
	if ( alloc->allocate( numBytes, offset ) && alloc->map( dataP ) ){
		alloc->map( dataP );
		memcpy( dataP, data, numBytes );
		return setAttribute( attribLocation_, alloc->getBuffer(), offset );
	}

	ofLogWarning() << "Could not allocate memory for attribLocation: " << attribLocation_;
	
	return *this;
}
// ------------------------------------------------------------

DrawCommand & DrawCommand::allocAndSetIndices( const ofIndexType * data, size_t numBytes, const std::unique_ptr<BufferAllocator>& alloc ){

	void * dataP = nullptr;
	::vk::DeviceSize offset = 0;
	
	// allocate data on gpu

	if ( alloc->allocate( numBytes, offset ) && alloc->map( dataP ) ){
		alloc->map( dataP );
		memcpy( dataP, data, numBytes );
		return setIndices( alloc->getBuffer(), offset );
	}

	ofLogWarning() << "Could not allocate memory for indices. ";

	return *this;
}
