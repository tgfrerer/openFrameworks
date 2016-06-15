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

	if (!mFrame.empty())
		mFrame.clear();
	mFrame.resize( numSwapchainImages_, ContextState() );

}

// ----------------------------------------------------------------------

void of::vk::Context::begin(size_t frame_){
	mSwapIdx = frame_;
	mAlloc->free(frame_);
	mFrame[mSwapIdx].mCurrentMatrixState = {}; // reset matrix state
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

// ----------------------------------------------------------------------

void of::vk::Context::push(){
	auto & f = mFrame[mSwapIdx];
	f.mMatrixStack.push( f.mCurrentMatrixState );
	f.mMatrixIdStack.push( f.mCurrentMatrixId );
	f.mCurrentMatrixId = -1;
}

// ----------------------------------------------------------------------

void of::vk::Context::pop(){
	auto & f = mFrame[mSwapIdx];

	if ( !f.mMatrixStack.empty() ){
		f.mCurrentMatrixState = f.mMatrixStack.top(); f.mMatrixStack.pop();
		f.mCurrentMatrixId = f.mMatrixIdStack.top(); f.mMatrixIdStack.pop();
	}
	else{
		ofLogError() << "Context:: Cannot push Matrix state further back than 0";
	}
}

// ----------------------------------------------------------------------

size_t of::vk::Context::getCurrentMatrixStateIdx(){
	// return current matrix state index, if such index exists.
	// if index does not exist, add the current matrix to the list of saved 
	// matrixes, and generate a new index.

	// only when a matrix state id is requested,
	// is matrix data saved to gpu accessible memory
	
	auto & f = mFrame[mSwapIdx];

	if ( f.mCurrentMatrixId == -1 ){

		void * pData = nullptr;
		if ( ! mAlloc->allocate( sizeof( MatrixState ), pData, f.mCurrentMatrixStateOffset, mSwapIdx )){
			ofLogError() << "out of matrix space.";
			return ( 0 );
		}

		// ----------| invariant: allocation successful

		// save matrix state into buffer
		memcpy( pData, &f.mCurrentMatrixState, sizeof( MatrixState ));

		f.mCurrentMatrixId = f.mSavedMatricesLastElement;

		++ f.mSavedMatricesLastElement;
		
	}
	return f.mCurrentMatrixId;
}

// ----------------------------------------------------------------------

const uint32_t& of::vk::Context::getCurrentMatrixStateOffset(){
	getCurrentMatrixStateIdx();
	return mFrame[mSwapIdx].mCurrentMatrixStateOffset;
}

// ----------------------------------------------------------------------

void of::vk::Context::setViewMatrix( const ofMatrix4x4 & mat_ ){
	auto & f = mFrame[mSwapIdx]; 
	f.mCurrentMatrixId = -1;
	f.mCurrentMatrixState.viewMatrix = mat_;
}

// ----------------------------------------------------------------------

void of::vk::Context::setProjectionMatrix( const ofMatrix4x4 & mat_ ){
	auto & f = mFrame[mSwapIdx]; 
	f.mCurrentMatrixId = -1;
	f.mCurrentMatrixState.projectionMatrix = mat_;
}

// ----------------------------------------------------------------------

void of::vk::Context::translate( const ofVec3f& v_ ){
	auto & f = mFrame[mSwapIdx];
	f.mCurrentMatrixId = -1;
	f.mCurrentMatrixState.modelMatrix.glTranslate( v_ );
}

// ----------------------------------------------------------------------

void of::vk::Context::rotate( const float& degrees_, const ofVec3f& axis_ ){
	auto & f = mFrame[mSwapIdx];
	f.mCurrentMatrixId = -1;
	f.mCurrentMatrixState.modelMatrix.glRotate( degrees_, axis_.x, axis_.y, axis_.z );
}