#include "Context.h"
#include "ofVkRenderer.h"


// buffers have descriptors (these tell us 

// ----------------------------------------------------------------------

void of::vk::Context::setup(ofVkRenderer* renderer_){

	mRenderer = renderer_;

	// The most important shader uniforms are the matrices
	// model, view, and projection matrix
	
	// we allocate enough memory in host-visible space to 
	// be able to update mMaxElementCount number of state objects 
	// as uniforms on each draw call.

	// TODO: effectively, this buffer needs the ability to grow - but there is a penalty for allocating - so ideally,
	// you would be able to tell it how many matrices to expect.
	
	if ( mMatrixUniformData.memory ){
		ofLogWarning() << "calling setup on already set up Context";
		reset();
	}

	auto & device = mRenderer->mDevice;

	// we need to find out the min buffer uniform alignment from the 
	// physical device.
	auto alignment = mRenderer->mPhysicalDeviceProperties.limits.minUniformBufferOffsetAlignment;

	mHostMemory.alignedMatrixStateSize = alignment * (( alignment + sizeof( MatrixState ) - 1 ) / alignment);

 	// Prepare and initialize uniform buffer containing shader uniforms
	VkResult err;

	// Vertex shader uniform buffer block
	VkBufferCreateInfo bufferInfo {
		VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,                   // VkStructureType        sType;
		nullptr,                                                // const void*            pNext;
		0,                                                      // VkBufferCreateFlags    flags;
		mHostMemory.alignedMatrixStateSize * mMaxElementCount,  // VkDeviceSize           size;
		VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,                     // VkBufferUsageFlags     usage;
		VK_SHARING_MODE_EXCLUSIVE,                              // VkSharingMode          sharingMode;
	    0,                                                      // uint32_t               queueFamilyIndexCount;
		nullptr,                                                // const uint32_t*        pQueueFamilyIndices;
	};

	// Create a new buffer
	vkCreateBuffer( device, &bufferInfo, nullptr, &mMatrixUniformData.buffer );
	
	// Get memory requirements including size, alignment and memory type 
	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements( device, mMatrixUniformData.buffer, &memReqs );

	assert( mHostMemory.alignedMatrixStateSize * mMaxElementCount == memReqs.size );

	// Gets the appropriate memory type for this type of buffer allocation
	// Only memory types that are visible to the host and coherent (coherent means they
	// appear to the GPU without the need of explicit range flushes)
	// Vulkan 1.0 guarantees the presence of at least one host-visible+coherent memory heap.
	VkMemoryAllocateInfo allocInfo {};
	mRenderer->getMemoryAllocationInfo( 
		memReqs, 
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 
		allocInfo 
	);

	allocInfo.allocationSize = 1UL << 27 - 1; // 2^26 = 67108864 bytes

	// Allocate memory for the uniform buffer
	// todo: check for and recover from allocation errors
	vkAllocateMemory( device, &allocInfo, nullptr, &( mMatrixUniformData.memory ) );
	
	// Bind memory to buffer
	vkBindBufferMemory( device, mMatrixUniformData.buffer, mMatrixUniformData.memory, 0 );

	// Store information in the uniform's descriptor
	mMatrixUniformData.descriptorBufferInfo.buffer = mMatrixUniformData.buffer;
	mMatrixUniformData.descriptorBufferInfo.offset = 0;
	mMatrixUniformData.descriptorBufferInfo.range = sizeof(MatrixState);

	vkMapMemory(
		device,
		mMatrixUniformData.memory,
		0,
		VK_WHOLE_SIZE, 0, (void**)&mHostMemory.pData
	);
}

// ----------------------------------------------------------------------

void of::vk::Context::begin(){
	mSavedMatricesLastElement = 0;
	mCurrentMatrixState = {}; // reset matrix state
}

// ----------------------------------------------------------------------

void of::vk::Context::end(){
}

// ----------------------------------------------------------------------

void of::vk::Context::reset(){
	auto & device = mRenderer->mDevice;
	
	vkUnmapMemory( device, mMatrixUniformData.memory );
	mHostMemory.pData = nullptr;

	vkFreeMemory( device, mMatrixUniformData.memory, nullptr );
	vkDestroyBuffer( device, mMatrixUniformData.buffer, nullptr );
}

// ----------------------------------------------------------------------

VkDescriptorBufferInfo & of::vk::Context::getDescriptorBufferInfo(){
	return mMatrixUniformData.descriptorBufferInfo;
}

// ----------------------------------------------------------------------

void of::vk::Context::push(){
	mMatrixStack.push( mCurrentMatrixState );
	mMatrixIdStack.push( mCurrentMatrixId );
	mCurrentMatrixId = -1;
}

// ----------------------------------------------------------------------

void of::vk::Context::pop(){
	if ( !mMatrixStack.empty() ){
		ofLog() << "pop, before: " << mCurrentMatrixId << " now: " << mMatrixIdStack.top() ;

		mCurrentMatrixState = mMatrixStack.top(); mMatrixStack.pop();
		mCurrentMatrixId = mMatrixIdStack.top(); mMatrixIdStack.pop();
	}
	else{
		ofLogError() << "Context:: Cannot push Matrix state further back than 0";
	}
}

// you only have to submit a matrix state to GPU memory if something has 
// been drawn with it. 
// if you do so, increase the matrixStateId 
//
// TODO: return an offset in bytes rather than an index.
//
// ----------------------------------------------------------------------
size_t of::vk::Context::getCurrentMatrixStateIdx(){

	// only when a matrix state id is requested,
	// is matrix data saved to 

	if ( mCurrentMatrixId == -1 ){

		if (  mSavedMatricesLastElement == mMaxElementCount ){
			ofLogError() << "out of matrix space.";
			return ( mMaxElementCount - 1 );
		}

		// save matrix to buffer - offset by id
		memcpy( mHostMemory.pData 
			+ (mHostMemory.alignedMatrixStateSize * mSavedMatricesLastElement),
			&mCurrentMatrixState,
			sizeof( MatrixState ));

		mCurrentMatrixId = mSavedMatricesLastElement;

		++ mSavedMatricesLastElement;
		
	}

	// return current matrix state index, if such index exists.
	// if index does not exist, add the current matrix to the list of saved 
	// matrixes, and generate a new index.
	return mCurrentMatrixId;
}

// ----------------------------------------------------------------------

size_t of::vk::Context::getCurrentMatrixStateOffset(){
	return mHostMemory.alignedMatrixStateSize * getCurrentMatrixStateIdx();
}

void of::vk::Context::setViewMatrix( const ofMatrix4x4 & mat_ ){
	mCurrentMatrixId = -1;
	mCurrentMatrixState.viewMatrix = mat_;
}

void of::vk::Context::setProjectionMatrix( const ofMatrix4x4 & mat_ ){
	mCurrentMatrixId = -1;
	mCurrentMatrixState.projectionMatrix = mat_;

}

// ----------------------------------------------------------------------
void of::vk::Context::translate( const ofVec3f& v_ ){
	mCurrentMatrixId = -1;
	mCurrentMatrixState.modelMatrix.glTranslate( v_ );
}

// ----------------------------------------------------------------------
void of::vk::Context::rotate( const float& degrees_, const ofVec3f& axis_ ){
	mCurrentMatrixId = -1;
	mCurrentMatrixState.modelMatrix.glRotate( degrees_, axis_.x, axis_.y, axis_.z );
}