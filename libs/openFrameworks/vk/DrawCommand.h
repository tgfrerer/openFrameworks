#pragma once
#include "vulkan/vulkan.hpp"
#include "vk/Shader.h"
#include "vk/Pipeline.h"
#include "vk/HelperTypes.h"
#include "vk/Texture.h"
#include "ofMesh.h"


namespace of{
namespace vk{

class DrawCommand;	   // ffdecl.
class RenderBatch;	   // ffdecl.
class BufferAllocator;	   // ffdecl.

// ----------------------------------------------------------------------

class DrawCommand
{
	friend class RenderBatch;

public :
	
	// draw mode to be used when submitting DrawCommand
	enum class DrawMethod : uint32_t
	{
		eDraw        = 0, // Default method
		eIndexed        , // Indexed draw
		eIndirect       , // todo: implement
		eIndexedIndirect, // todo: implement
	};

private:

	// A draw command has everything needed to draw an object
	//
	// TODO: really, there's no need to keep this state here as data - better 
	// store it in a common repository of all pipeline states, and index it by
	// an ID, which can be compared against, instead of a hash.
	GraphicsPipelineState mPipelineState;

private:      /* transient data */

	uint64_t mPipelineHash = 0;

	// Bindings data for descriptorSets, (vector index == set number) - retrieved from shader on setup
	std::vector<DescriptorSetData_t>             mDescriptorSetData;
	
	// Pointer to lookup table for uniform name-> Uniform Key- retrieved from shader on setup
	const std::map<std::string, UniformId_t>*    mUniformDictionary;

	// Pointer to lookup table for set,binding -> Uniform Key - retrieved from shader on setup
	const std::vector<std::vector<UniformId_t>>* mUniformBindings;

	// Vector of buffers holding vertex attribute data
	std::vector<::vk::Buffer> mVertexBuffers;

	// Offsets into buffer for vertex attribute data
	std::vector<::vk::DeviceSize> mVertexOffsets;

	// 1-or-0 element buffer of indices for this draw command
	::vk::Buffer mIndexBuffer = nullptr;

	// Offsets into buffer for index data - this is optional
	::vk::DeviceSize mIndexOffsets = 0;

	// Draw method to be used when submitting DrawCommand
	DrawMethod mDrawMethod = DrawMethod::eDraw;

	// draw parameters used when submitting DrawCommand
	uint32_t mNumIndices    = 0;
	uint32_t mNumVertices   = 0;
	uint32_t mInstanceCount = 1; 
	uint32_t mFirstVertex   = 0; 
	uint32_t mFirstIndex    = 0; 
	uint32_t mVertexOffset  = 0; 
	uint32_t mFirstInstance = 0; 

	std::shared_ptr<ofMesh> mMsh; /* optional */

	// Set data for upload to ubo - data is stored locally 
	// until draw command is submitted
	void commitUniforms( BufferAllocator& alloc_ );

	void commitMeshAttributes( BufferAllocator& alloc_ );

public:

	void setup(const GraphicsPipelineState& pipelineState);

	const GraphicsPipelineState&         getPipelineState() const;
	
	const DescriptorSetData_t&           getDescriptorSetData( size_t setId_ ) const;

	const std::vector<::vk::DeviceSize>& getVertexOffsets();
	const std::vector<::vk::Buffer>&     getVertexBuffers();
	
	const ::vk::DeviceSize&              getIndexOffsets();
	const ::vk::Buffer&                  getIndexBuffer();
	
	// Getters and setters for (instanced) draw parameters
	DrawCommand&  setDrawMethod   ( DrawMethod method );
	DrawCommand&  setNumIndices   ( uint32_t numVertices );
	DrawCommand&  setNumVertices  ( uint32_t numVertices );
	DrawCommand&  setInstanceCount( uint32_t instanceCount );
	DrawCommand&  setFirstVertex  ( uint32_t firstVertex   );
	DrawCommand&  setFirstIndex   ( uint32_t firstIndex    );
	DrawCommand&  setVertexOffset ( uint32_t vertexOffset  );
	DrawCommand&  setFirstInstance( uint32_t firstInstance );
				   
	DrawMethod     getDrawMethod   ();
	uint32_t       getNumIndices   ();
	uint32_t       getNumVertices  ();
	uint32_t       getInstanceCount();
	uint32_t       getFirstVertex  ();
	uint32_t       getFirstIndex   ();
	uint32_t       getVertexOffset ();
	uint32_t       getFirstInstance();

	// Use ofMesh to draw - this method is here to aid prototyping, and to render dynamic
	// meshes. The mesh will get uploaded to temporary GPU memory when the DrawCommand
	// is queued up into a RenderBatch. Use setAttribute and setIndices to render static
	// meshes, and for more control over how drawing behaves.
	of::vk::DrawCommand & setMesh( const std::shared_ptr<ofMesh>& msh_ );

	// Allocate, and store attribute data in gpu memory
	template <typename T>
	of::vk::DrawCommand & allocAndSetAttribute( const std::string& attrName_, const std::vector<T> & vec, BufferAllocator& alloc );

	// Allocate, and store attribute data in gpu memory
	template <typename T>
	of::vk::DrawCommand & allocAndSetAttribute( const std::string& attrName_, const T* data, size_t numBytes, BufferAllocator& alloc );

	// Allocate, and store attribute data in gpu memory
	template <typename T>
	of::vk::DrawCommand & allocAndSetAttribute( const size_t& attribLocation_, const std::vector<T> & vec, BufferAllocator& alloc );

	// Allocate, and store attribute data in gpu memory
	of::vk::DrawCommand & allocAndSetAttribute( const size_t& attribLocation_, const void* data, size_t numBytes, BufferAllocator& alloc );

	// Allocate, and store index data in gpu memory
	of::vk::DrawCommand & allocAndSetIndices( const ofIndexType* data, size_t numBytes, BufferAllocator& alloc );


	of::vk::DrawCommand & setAttribute( const std::string& name_, ::vk::Buffer buffer_, ::vk::DeviceSize offset_ );
	of::vk::DrawCommand & setAttribute( const size_t attribLocation_, ::vk::Buffer buffer, ::vk::DeviceSize offset );
	of::vk::DrawCommand & setAttribute( const size_t attribLocation_, const of::vk::BufferRegion & bufferRegion_ );
	
	of::vk::DrawCommand & setIndices( ::vk::Buffer buffer, ::vk::DeviceSize offset );
	of::vk::DrawCommand & setIndices( const of::vk::BufferRegion& bufferRegion_ );

	// Store uniform values to staging cpu memory
	template <typename T>
	of::vk::DrawCommand & setUniform( const std::string& uniformName, const T& uniformValue_ );

	// Store ubo to staging cpu memory
	template <typename T>
	of::vk::DrawCommand & setUbo( uint32_t setId, uint32_t bindingId, const T& struct_ );

	of::vk::DrawCommand & setTexture( const std::string& name, const of::vk::Texture& tex_ );
	of::vk::DrawCommand & setStorageBuffer( const std::string& name, const of::vk::BufferRegion& buf_ );

};

// ------------------------------------------------------------

template<typename T>
inline DrawCommand& DrawCommand::setUbo( uint32_t setId, uint32_t bindingId, const T& struct_ ) {

	if ( mUniformBindings->size() <= setId || (*mUniformBindings)[setId].size() <= bindingId ) {
		ofLogWarning() << "Could not find Ubo in set: '" << setId << "' at binding number: " << "'" << bindingId << "' index not found in shader.";
		return *this;
	}

	// --------| invariant: uniform found

	const auto& uniformInfo = (*mUniformBindings)[setId][bindingId];

	if ( uniformInfo.dataRange < sizeof( T ) ) {
		ofLogWarning() << "Could not set ubo : Data size does not match: "
			<< " Expected: " << uniformInfo.dataRange << ", received: " << sizeof( T ) << ".";
		return *this;
	}

	// --------| invariant: size match, we can copy data into our vector.

	auto & dataVec = mDescriptorSetData[uniformInfo.setIndex].dynamicUboData[uniformInfo.auxDataIndex];

	if ( uniformInfo.dataOffset + uniformInfo.dataRange <= dataVec.size() ) {
		memcpy( dataVec.data() + uniformInfo.dataOffset, &struct_, uniformInfo.dataRange );
	} else {
		ofLogError() << "Not enough space in local uniform storage. Has this drawCommand been properly initialised?";
	}

	return *this;
}

// ------------------------------------------------------------


template<typename T>
inline DrawCommand& DrawCommand::setUniform( const std::string & uniformName, const T & uniformValue_ ){

	auto uniformInfoIt = mUniformDictionary->find( uniformName );
	
	if ( uniformInfoIt == mUniformDictionary->end() ){
		ofLogWarning() << "Could not set Uniform '" << uniformName << "': Uniform name not found in shader";
		return *this;
	}

	// --------| invariant: uniform found

	const auto & uniformInfo = uniformInfoIt->second;
	
	if ( uniformInfo.dataRange < sizeof( T ) ){
		ofLogWarning() << "Could not set uniform '" << uniformName << "': Uniform data size does not match: "
			<< " Expected: " << uniformInfo.dataRange << ", received: " << sizeof( T ) << ".";
		return *this;
	}

	// --------| invariant: size match, we can copy data into our vector.

	auto & dataVec = mDescriptorSetData[uniformInfo.setIndex].dynamicUboData[uniformInfo.auxDataIndex];

	if ( uniformInfo.dataOffset + uniformInfo.dataRange <= dataVec.size() ){
		memcpy( dataVec.data() + uniformInfo.dataOffset, &uniformValue_, uniformInfo.dataRange );
	} else{
		ofLogError() << "Not enough space in local uniform storage. Has this drawCommand been properly initialised?";
	}

	return *this;
}

// ------------------------------------------------------------

inline of::vk::DrawCommand & of::vk::DrawCommand::setTexture( const std::string & uniformName, const of::vk::Texture& tex_ ){
	
	auto uniformInfoIt = mUniformDictionary->find( uniformName );

	if ( uniformInfoIt == mUniformDictionary->end() ){
		ofLogWarning() << "Could not set Texture '" << uniformName << "': Uniform name not found in shader";
		return *this;
	}

	// --------| invariant: uniform found
	
	const auto & uniformInfo = uniformInfoIt->second;

	auto & imageAttachment = mDescriptorSetData[uniformInfo.setIndex].imageAttachment[uniformInfo.auxDataIndex];

	imageAttachment.sampler     = tex_.getSampler();
	imageAttachment.imageView   = tex_.getImageView();
	imageAttachment.imageLayout = tex_.getImageLayout();
	
	return *this;
}

// ------------------------------------------------------------

inline of::vk::DrawCommand & of::vk::DrawCommand::setStorageBuffer( const std::string & uniformName, const of::vk::BufferRegion& buf_ ){

	auto uniformInfoIt = mUniformDictionary->find( uniformName );

	if ( uniformInfoIt == mUniformDictionary->end() ){
		ofLogWarning() << "Could not set Storage Buffer '" << uniformName << "': Uniform name not found in shader";
		return *this;
	}

	// --------| invariant: uniform found

	const auto & uniformInfo = uniformInfoIt->second;

	auto & bufferAttachment = mDescriptorSetData[uniformInfo.setIndex].bufferAttachment[uniformInfo.auxDataIndex];

	bufferAttachment = buf_;

	return *this;
}

} // namespace 
} // end namespace of

// ------------------------------------------------------------
// Inline getters and setters

inline const of::vk::GraphicsPipelineState & of::vk::DrawCommand::getPipelineState() const{
	return mPipelineState;
}

inline const of::vk::DescriptorSetData_t & of::vk::DrawCommand::getDescriptorSetData( size_t setId_ ) const{
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

inline uint32_t of::vk::DrawCommand::getNumIndices(){
	return mNumIndices;
}

inline uint32_t of::vk::DrawCommand::getNumVertices(){
	return mNumVertices;
}

inline uint32_t of::vk::DrawCommand::getInstanceCount(){
	return mInstanceCount;
}

inline uint32_t of::vk::DrawCommand::getFirstVertex(){
	return mFirstVertex;
}

inline uint32_t of::vk::DrawCommand::getFirstIndex(){
	return mFirstIndex;
}

inline uint32_t of::vk::DrawCommand::getVertexOffset(){
	return mVertexOffset;
}

inline uint32_t of::vk::DrawCommand::getFirstInstance(){
	return mFirstInstance;
}

inline of::vk::DrawCommand::DrawMethod of::vk::DrawCommand::getDrawMethod(){
	return mDrawMethod;
}

inline of::vk::DrawCommand & of::vk::DrawCommand::setNumIndices( uint32_t numIndices ){
	mNumIndices = numIndices;
	return *this;
}

inline of::vk::DrawCommand & of::vk::DrawCommand::setNumVertices( uint32_t numVertices ){
	mNumVertices = numVertices;
	return *this;
}

inline of::vk::DrawCommand & of::vk::DrawCommand::setInstanceCount( uint32_t instanceCount ){
	mInstanceCount = instanceCount;
	return *this;
}

inline of::vk::DrawCommand & of::vk::DrawCommand::setFirstVertex( uint32_t firstVertex ){
	mFirstVertex = firstVertex;
	return *this;
}

inline of::vk::DrawCommand & of::vk::DrawCommand::setFirstIndex( uint32_t firstIndex ){
	mFirstIndex = firstIndex;
	return *this;
}

inline of::vk::DrawCommand & of::vk::DrawCommand::setVertexOffset( uint32_t vertexOffset ){
	mVertexOffset = vertexOffset;
	return *this;
}

inline of::vk::DrawCommand & of::vk::DrawCommand::setFirstInstance( uint32_t firstInstance ){
	mFirstInstance = firstInstance;
	return *this;
}

inline of::vk::DrawCommand & of::vk::DrawCommand::setDrawMethod( DrawMethod method_ ){
	mDrawMethod = method_;
	return *this;
}

// ------------------------------------------------------------

inline of::vk::DrawCommand & of::vk::DrawCommand::setAttribute( const size_t attribLocation_, const of::vk::BufferRegion & bufferRegion_ ){
	return setAttribute( attribLocation_, bufferRegion_.buffer, bufferRegion_.offset );
}

// ------------------------------------------------------------

inline of::vk::DrawCommand & of::vk::DrawCommand::setAttribute( const std::string& name_, ::vk::Buffer buffer_, ::vk::DeviceSize offset_ ){
	size_t index = 0;
	if ( mPipelineState.getShader()->getAttributeBinding( name_, index ) ){
		setAttribute( index, buffer_, offset_ );
		return *this;
	}

	// --------| invariant: name was not resolved successfully.

	ofLogWarning() 
		<< "Attribute '" << name_ << "' could not be found in shader: " 
		<< mPipelineState.getShader()->getName();
	return *this;
}

// ------------------------------------------------------------

inline of::vk::DrawCommand & of::vk::DrawCommand::setAttribute( const size_t attribLocation_, ::vk::Buffer buffer_, ::vk::DeviceSize offset_ ){

	if ( attribLocation_ >= mVertexBuffers.size() ){
		ofLogError() << "Attribute location not available: " << attribLocation_;
		return *this;
	}

	// ---------| invariant: attribLocation is valid

	mVertexBuffers[attribLocation_] = buffer_;
	mVertexOffsets[attribLocation_] = offset_;
	
	return *this;
}

// ------------------------------------------------------------

inline of::vk::DrawCommand & of::vk::DrawCommand::setIndices( const of::vk::BufferRegion& bufferRegion_ ){
	return setIndices( bufferRegion_.buffer, bufferRegion_.offset );
}

// ------------------------------------------------------------

inline of::vk::DrawCommand & of::vk::DrawCommand::setIndices( ::vk::Buffer buffer_, ::vk::DeviceSize offset_ ){
	mIndexBuffer = buffer_;
	mIndexOffsets = offset_;
	return *this;
}

// ------------------------------------------------------------
