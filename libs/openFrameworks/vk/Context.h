#pragma once

#include <vulkan/vulkan.h>
#include "ofMatrix4x4.h"

/// Context manages all transient state
/// + transformation matrices
/// + material 
/// + geometry bindings
/// transient state is tracked and accumulated in CPU memory
/// before frame submission, state is flushed to GPU memory


class ofVkRenderer;

namespace of {
namespace vk {


// todo: a dynamic buffer should act similar to 
// a vector, pre-allocating some memory so that 
// it may accomodata for growth.
// 
// we need to make sure that buffers do so in a
// clever way, and that they keep track on the 
// actual memory that is in use.
//
// it is also evident from nv-examples that it 
// could make sense to stage memory which is 
// host-visible and to transfer it to device-
// only visible memory before rendering.
//
// when using dynamic buffers it is important
// to know that the range for these is not of 
// very fine granularity, but controlled by 
// minUniformBufferOffsetAlignment
// so you might need some padding. having one 
// matrix (64 bytes) within a 256 minimal buffer
// offset seems like a waste of space. better 
// have some more. There is effectively space for
// 4 matrices, if you had a 256B minimal offset.
//
// this limit may be smaller depending on the 
// vulkan implementation, though!


// a context stores any transient data
// and keeps these ready to flush to the GPU

class Context
{
	
	ofVkRenderer * mRenderer;

	// A GPU-backed buffer object to back these
	// matrices.
	struct
	{
		VkBuffer buffer = nullptr;
		VkDeviceMemory memory = nullptr;
		VkDescriptorBufferInfo descriptorBufferInfo;
	}  mMatrixUniformData;

	// host-visible address to memory for buffer
	// nullptr if not mapped.
	struct HostMemory
	{
		uint8_t* pData = nullptr;
		//size_t allocatedSize = 0;	// mapped size in bytes
		size_t alignedMatrixStateSize = 0;  // matrixState size in bytes aligned to minUniformBufferOffsetAlignment
	} mHostMemory;


	// FIXME: eventually, the plan would be to have one buffer
	// in which we store the full nodegraph of all matrices
	// and offsets to point to the element which is used 
	// during rendering/drawing.
	
	struct MatrixState
	{
		// IMPORTANT: this sequence needs to map the sequence in the UBO block 
		// within the shader!!!
		ofMatrix4x4 projectionMatrix;
		ofMatrix4x4 modelMatrix;
		ofMatrix4x4 viewMatrix;
		
	} mMatrixState;

	std::vector<MatrixState>	   mSavedMatrices;
	size_t mSavedMatricesLastElement = 0;
	size_t mMaxElementCount = 262144;	 // todo: find max element count based on 

	// stack of all pushed or popped matrices.
	// -1 indicates the matrix has not been saved yet
	// positive integer indicates matrix index into savedmatrices
	stack<int> mMatrixIdStack;
	std::stack<MatrixState> mMatrixStack;

	MatrixState mCurrentMatrixState;
	int         mCurrentMatrixId = -1;

	/// returns index of current matrix for generating 
	/// the binding offset into host memory for the descriptor
	/// \note as a side-effect will upload (stage) matrix 
	// state data if current matrix state has not yet been 
	// uploaded.
	size_t getCurrentMatrixStateIdx();

	// get offset in bytes for the current matrix into the matrix memory buffer
	// this must be a mutliple of  minUniformBufferOffsetAlignment
	size_t getCurrentMatrixStateOffset();

	// invalidates link to saved matrix from current matrix
	// (forces saving out a separate matrix)
	void dirtyCurrentMatrixState(){
		mCurrentMatrixId = -1;
	};

	// allocates memory on the GPU (call rarely)
	void setup();

	// destroys memory allocations
	void reset();

	/// map uniform buffers so that they can be written to.
	/// \return an address into gpu readable memory
	/// also resets indices into internal matrix state structures
	void begin();

	// unmap uniform buffers 
	void end();

	vector<VkMappedMemoryRange> mMappedRanges;

	// the descriptor is something like a view into the 
	// memory, an alias so to say
	VkDescriptorBufferInfo& getDescriptorBufferInfo();

	friend class ofVkRenderer;

public:
	

	// whenever a draw command occurs, the current matrix id has to be either

	// push currentMatrix state
	void push();
	// pop current Matrix state
	void pop();

};



} // namespace vk
} // namespace of