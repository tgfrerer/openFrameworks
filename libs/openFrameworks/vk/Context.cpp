#include "Context.h"
#include "ofVkRenderer.h"


// buffers have descriptors (these tell us 

// ----------------------------------------------------------------------

void of::vk::Context::setup(){

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
	
	// reserve size for saved matrices in our temporary buffer
	mSavedMatrices.resize( mMaxElementCount );

	auto & device = mRenderer->mDevice;

	// we need to find out the min buffer uniform alignment from the 
	// physical device.
	auto alignment = mRenderer->mPhysicalDeviceProperties.limits.minUniformBufferOffsetAlignment;

	mHostMemory.alignedMatrixStateSize = alignment * (( alignment + sizeof( mMatrixState ) - 1 ) / alignment);

 	// Prepare and initialize uniform buffer containing shader uniforms

	// Vertex shader uniform buffer block
	VkBufferCreateInfo bufferInfo = {};
	VkResult err;

	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = mHostMemory.alignedMatrixStateSize * mMaxElementCount;
	bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

	// Create a new buffer
	err = vkCreateBuffer( device, &bufferInfo, nullptr, &mMatrixUniformData.buffer );
	assert( !err );
	// Get memory requirements including size, alignment and memory type 
	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements( device, mMatrixUniformData.buffer, &memReqs );

	assert( mHostMemory.alignedMatrixStateSize * mMaxElementCount == memReqs.size );

	// Gets the appropriate memory type for this type of buffer allocation
	// Only memory types that are visible to the host
	VkMemoryAllocateInfo allocInfo = {};
	mRenderer->getMemoryAllocationInfo( memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, allocInfo );
	// Allocate memory for the uniform buffer
	err = vkAllocateMemory( device, &allocInfo, nullptr, &( mMatrixUniformData.memory ) );
	assert( !err );
	// Bind memory to buffer
	err = vkBindBufferMemory( device, mMatrixUniformData.buffer, mMatrixUniformData.memory, 0 );
	assert( !err );

	// Store information in the uniform's descriptor
	mMatrixUniformData.descriptorBufferInfo.buffer = mMatrixUniformData.buffer;
	mMatrixUniformData.descriptorBufferInfo.offset = 0;
	mMatrixUniformData.descriptorBufferInfo.range = sizeof(mMatrixState);
}

// ----------------------------------------------------------------------


void of::vk::Context::begin(){

	if ( mHostMemory.pData){
		ofLogError() << "mapped uniform buffer whilst already mapped. re-mapping...";
		end();
	}

	auto & device = mRenderer->mDevice;
	size_t element_id = 0;
	// Map uniform buffer data and update it
	
	VkResult err = vkMapMemory( 
		device, 
		mMatrixUniformData.memory, 
		0,
		VK_WHOLE_SIZE, 0, (void**)&mHostMemory.pData
	);
	assert( !err );

	mSavedMatricesLastElement = 0;
	mMatrixState = {}; // reset matrix state
}

// ----------------------------------------------------------------------

void of::vk::Context::end(){
	auto & device = mRenderer->mDevice;
	/*VkMappedMemoryRange range{};
	range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
	range.pNext = nullptr;
	range.memory = mMatrixUniformDataBuffer.memory;
	range.offset = element_id * mAlignedMatrixStateSize;
	range.size = mAlignedMatrixStateSize;

	vkFlushMappedMemoryRanges( device, 1, &range );*/

	vkUnmapMemory( device, mMatrixUniformData.memory );
	mHostMemory.pData = nullptr;
}

// ----------------------------------------------------------------------

void of::vk::Context::reset(){
	vkFreeMemory( mRenderer->mDevice, mMatrixUniformData.memory, nullptr );
	vkDestroyBuffer( mRenderer->mDevice, mMatrixUniformData.buffer, nullptr );
}

// ----------------------------------------------------------------------

VkDescriptorBufferInfo & of::vk::Context::getDescriptorBufferInfo(){
	return mMatrixUniformData.descriptorBufferInfo;
}

// ----------------------------------------------------------------------

// you only have to submit a matrix state to GPU memory if something has 
// been drawn with it. 

// if you do so, increase the matrixStateId 
// 


// ----------------------------------------------------------------------

void of::vk::Context::push(){
	mMatrixStack.push( mCurrentMatrixState );
	mMatrixIdStack.push( mCurrentMatrixId );
}

// ----------------------------------------------------------------------

void of::vk::Context::pop(){
	if ( !mMatrixStack.empty() ){
		mCurrentMatrixState = mMatrixStack.top(); mMatrixStack.pop();
		mCurrentMatrixId = mMatrixIdStack.top(); mMatrixIdStack.pop();
	}
	else{
		ofLogError() << "Context:: Cannot push Matrix state further back than 0";
	}
		
}

// ----------------------------------------------------------------------
size_t of::vk::Context::getCurrentMatrixStateIdx(){

	if ( mCurrentMatrixId == -1 ){

		if ( mSavedMatricesLastElement == mSavedMatrices.size() ){
			ofLogError() << "out of matrix space.";
			// TODO: realloc
			return mSavedMatricesLastElement-1;
		}

		// save matrix to buffer - offset by id
		memcpy( mHostMemory.pData + (mHostMemory.alignedMatrixStateSize * mSavedMatricesLastElement),
			&mMatrixState,
			sizeof(mMatrixState));

		//mSavedMatrices[mSavedMatricesLastElement] = ( mCurrentMatrixState );
		mCurrentMatrixId = mSavedMatricesLastElement;
		mSavedMatricesLastElement++;
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