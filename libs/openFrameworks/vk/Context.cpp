#include "Context.h"
#include "ofVkRenderer.h"
#include "vk/vkAllocator.h"

// ----------------------------------------------------------------------

void of::vk::Context::setup(ofVkRenderer* renderer_, size_t numSwapchainImages_ ){

	mRenderer = renderer_;

	// The most important shader uniforms are the matrices
	// model, view, and projection matrix
	
	of::vk::Allocator::Settings settings{};
	settings.device = mRenderer->getVkDevice();
	settings.renderer = mRenderer;
	settings.frames = numSwapchainImages_;
	settings.size =  ( (2UL << 24)); // (32 MB * number of swapchain images)
	
	settings.size = settings.size * numSwapchainImages_ ;

	mAlloc = std::make_shared<of::vk::Allocator>(settings);
	mAlloc->setup();

	mMatrixStateBufferInfo = {
		mAlloc->getBuffer(),   // VkBuffer        buffer;
		0,                     // VkDeviceSize    offset;
		sizeof(MatrixState),   // VkDeviceSize    range;
	};

	if (!mFrames.empty())
		mFrames.clear();
	mFrames.resize( numSwapchainImages_, ContextState() );

}

// ----------------------------------------------------------------------

void of::vk::Context::begin(size_t frame_){
	mSwapIdx = frame_;
	mAlloc->free(frame_);
	mFrames[mSwapIdx].mCurrentMatrixState = {}; // reset matrix state
}

// ----------------------------------------------------------------------

void of::vk::Context::end(){
	mSwapIdx = -1;
}

// ----------------------------------------------------------------------

void of::vk::Context::reset(){
	mAlloc->reset();
}

// ----------------------------------------------------------------------

VkDescriptorBufferInfo & of::vk::Context::getDescriptorBufferInfo(){
	return mMatrixStateBufferInfo;
}

const VkBuffer & of::vk::Context::getVkBuffer() const {
	return mAlloc->getBuffer();
}

// ----------------------------------------------------------------------

void of::vk::Context::push(){
	auto & f = mFrames[mSwapIdx];
	f.mMatrixStack.push( f.mCurrentMatrixState );
	f.mMatrixIdStack.push( f.mCurrentMatrixId );
	f.mCurrentMatrixId = -1;
}

// ----------------------------------------------------------------------

void of::vk::Context::pop(){
	auto & f = mFrames[mSwapIdx];

	if ( !f.mMatrixStack.empty() ){
		f.mCurrentMatrixState = f.mMatrixStack.top(); f.mMatrixStack.pop();
		f.mCurrentMatrixId = f.mMatrixIdStack.top(); f.mMatrixIdStack.pop();
	}
	else{
		ofLogError() << "Context:: Cannot push Matrix state further back than 0";
	}
}

// ----------------------------------------------------------------------

bool of::vk::Context::storeMesh( const ofMesh & mesh_, std::vector<VkDeviceSize>& vertexOffsets, std::vector<VkDeviceSize>& indexOffsets ){
	// TODO: add option to interleave 
	
	auto & f = mFrames[mSwapIdx];
	
	uint32_t numVertices   = mesh_.getVertices().size();
	uint32_t numColors     = mesh_.getColors().size();
	uint32_t numNormals    = mesh_.getNormals().size();
	uint32_t numTexCooords = mesh_.getTexCoords().size();

	uint32_t numIndices    = mesh_.getIndices().size();

	// TODO: add error checking - make sure 
	// numVertices == numColors == numNormals == numTexCooords

	// For now, only store vertices, normals
	// and indices.

	// Q: how do we deal with meshes that don't have data for all possible attributes?
	// 
	// A: we could go straight ahead here, but the method actually 
	//    generating the command buffer would cull "empty" slots 
	//    by interrogating the mesh for missing data in vectors.
	//    We know that a mesh does not have normals, for example, if the count of 
	//    normals is 0.
	//

	// Q: should we cache meshes to save memory and potentially time?

	void*    pData    = nullptr;
	uint32_t numBytes = 0;

	vertexOffsets.resize( 4, 0 ); 

	// binding number 0
	numBytes = numVertices * sizeof( ofVec3f );
	if ( mAlloc->allocate( numBytes, pData, vertexOffsets[0], mSwapIdx ) ){
		memcpy( pData, mesh_.getVerticesPointer(), numBytes );
	};

	// binding number 1
	numBytes = numColors * sizeof( ofFloatColor );
	if ( mAlloc->allocate( numBytes, pData, vertexOffsets[1], mSwapIdx ) ){
		memcpy( pData, mesh_.getColorsPointer(), numBytes );
	};

	// binding number 2
	numBytes = numNormals * sizeof( ofVec3f );
	if ( mAlloc->allocate( numBytes, pData, vertexOffsets[2], mSwapIdx ) ){
		memcpy( pData, mesh_.getNormalsPointer(), numBytes );
	};

	numBytes = numTexCooords * sizeof( ofVec2f );
	if ( mAlloc->allocate( numBytes, pData, vertexOffsets[3], mSwapIdx ) ){
		memcpy( pData, mesh_.getTexCoordsPointer(), numBytes );
	};


	VkDeviceSize indexOffset = 0;
	numBytes = numIndices * sizeof( ofIndexType );
	if ( mAlloc->allocate( numBytes, pData, indexOffset, mSwapIdx ) ){
		indexOffsets.push_back( indexOffset );
		memcpy( pData, mesh_.getIndexPointer(), numBytes );
	};

	return false;
}

// ----------------------------------------------------------------------

bool of::vk::Context::storeCurrentMatrixState(){
	
	// Matrix data is only uploaded if current matrix id is -1, 
	// meaning there was no current matrix or the current matrix
	// was invalidated
	
	auto & f = mFrames[mSwapIdx];

	if ( f.mCurrentMatrixId == -1 ){

		void * pData = nullptr;
		auto success = mAlloc->allocate( sizeof( MatrixState ), pData, f.mCurrentMatrixStateOffset, mSwapIdx );
		
		if ( !success ){
			ofLogError() << "out of matrix space.";
			return false;
		}

		// ----------| invariant: allocation successful

		// Save current matrix state into GPU buffer
		memcpy( pData, &f.mCurrentMatrixState, sizeof( MatrixState ));

		f.mCurrentMatrixId = f.mSavedMatricesLastElement;
		++ f.mSavedMatricesLastElement;
		
	}
	return true;
}

// ----------------------------------------------------------------------

const VkDeviceSize& of::vk::Context::getCurrentMatrixStateOffset(){
	storeCurrentMatrixState();
	return mFrames[mSwapIdx].mCurrentMatrixStateOffset;
}

// ----------------------------------------------------------------------

void of::vk::Context::setViewMatrix( const ofMatrix4x4 & mat_ ){
	auto & f = mFrames[mSwapIdx]; 
	f.mCurrentMatrixId = -1;
	f.mCurrentMatrixState.viewMatrix = mat_;
}

// ----------------------------------------------------------------------

void of::vk::Context::setProjectionMatrix( const ofMatrix4x4 & mat_ ){
	auto & f = mFrames[mSwapIdx]; 
	f.mCurrentMatrixId = -1;
	f.mCurrentMatrixState.projectionMatrix = mat_;
}

// ----------------------------------------------------------------------

void of::vk::Context::translate( const ofVec3f& v_ ){
	auto & f = mFrames[mSwapIdx];
	f.mCurrentMatrixId = -1;
	f.mCurrentMatrixState.modelMatrix.glTranslate( v_ );
}

// ----------------------------------------------------------------------

void of::vk::Context::rotate( const float& degrees_, const ofVec3f& axis_ ){
	auto & f = mFrames[mSwapIdx];
	f.mCurrentMatrixId = -1;
	f.mCurrentMatrixState.modelMatrix.glRotate( degrees_, axis_.x, axis_.y, axis_.z );
}