#pragma once

#include <vulkan/vulkan.h>
#include "ofMatrix4x4.h"
#include "ofMesh.h"

/// Context manages all transient state
/// + transformation matrices
/// + material 
/// + geometry bindings
/// transient state is tracked and accumulated in CPU memory
/// before frame submission, state is flushed to GPU memory

/*

Context exists to provide legacy support for immediate mode 
style rendering.

You draw inside a context and can expect it to work
in a similar way to OpenGL immediate mode. But without the OpenGL 
"under the hood" driver optimisations.

It may be possible to use context to pre-record memory
and command buffers - and to use this to playback "canned" 
framebuffers.

For this to work, you would use a static context - a context 
with one frame of backing memory - which is transferred from 
host memory to GPU memory before being used to draw.

*/


class ofVkRenderer;

namespace of {
namespace vk {

class Allocator; // ffdecl.

// A Context stores any transient data
// and keeps state, mimicking legacy "immediate mode" renderer behaviour

// The context holds a number of frames, dependent on the 
// number of swapchain images. For each swapchain image,
// there is a context. 

// The context has one allocator, which holds one buffer which is backed
// by one large chunk device memory. Device memory is segmented into 
// equal sized parts, one part for each swapchain image.

// You tell the context which frame to operate on by passing the swapchain 
// image index when calling Context::begin()

class Context
{
	
	ofVkRenderer * mRenderer;

	shared_ptr<of::vk::Allocator> mAlloc;

	// A GPU-backed buffer object to back these
	// matrices.

	VkDescriptorBufferInfo mMatrixStateBufferInfo;

	struct MatrixState
	{
		// IMPORTANT: this sequence needs to map the sequence in the UBO block 
		// within the shader!!!
		ofMatrix4x4 projectionMatrix;
		ofMatrix4x4 modelMatrix;
		ofMatrix4x4 viewMatrix;
	};

	struct ContextState
	{
		size_t mSavedMatricesLastElement = 0;

		// stack of all pushed or popped matrices.
		// -1 indicates the matrix has not been saved yet
		// positive integer indicates matrix index into savedmatrices
		stack<int>              mMatrixIdStack;
		std::stack<MatrixState> mMatrixStack;

		int                     mCurrentMatrixId = -1;         // -1 means undefined, not yet used/saved
		VkDeviceSize            mCurrentMatrixStateOffset = 0; // offset into buffer to get current matrix

		MatrixState             mCurrentMatrixState;
	};

	// one ContextState element per swapchain image
	std::vector<ContextState> mFrame;

	int mSwapIdx = 0;

	bool storeCurrentMatrixState();

public:

	// get offset in bytes for the current matrix into the matrix memory buffer
	// this must be a mutliple of  minUniformBufferOffsetAlignment
	const VkDeviceSize& getCurrentMatrixStateOffset();

	// invalidates link to saved matrix from current matrix
	// (forces saving out a separate matrix)
	/*void dirtyCurrentMatrixState(){
		mCurrentMatrixId = -1;
	};*/

	
	// the descriptor is something like a view into the 
	// memory, an alias so to say
	VkDescriptorBufferInfo& getDescriptorBufferInfo();
	const VkBuffer&         getVkBuffer() const;

	// allocates memory on the GPU for each swapchain image (call rarely)
	void setup(ofVkRenderer * renderer_, size_t numSwapchainImages_);

	// destroys memory allocations
	void reset();

	/// map uniform buffers so that they can be written to.
	/// \return an address into gpu readable memory
	/// also resets indices into internal matrix state structures
	void begin(size_t frame_);

	// unmap uniform buffers 
	void end();

	// whenever a draw command occurs, the current matrix id has to be either

	inline const ofMatrix4x4 & getViewMatrix() const {
		// TODO: add error checking: if mSwapIdx == -1
		// we are not allowed to getViewMatrix, since we are not within an open Context.
		return mFrame[mSwapIdx].mCurrentMatrixState.viewMatrix;
	};

	inline const ofMatrix4x4 & getModelMatrix() const {
		// TODO: add error checking: if mSwapIdx == -1
		return mFrame[mSwapIdx].mCurrentMatrixState.modelMatrix;
	};

	inline const ofMatrix4x4 & projectionMatrix() const {
		// TODO: add error checking: if mSwapIdx == -1
		return mFrame[mSwapIdx].mCurrentMatrixState.projectionMatrix;
	}

	void setViewMatrix( const ofMatrix4x4& mat_ );
	void setProjectionMatrix( const ofMatrix4x4& mat_ );

	void translate(const ofVec3f& v_);
	void rotate( const float & degrees_, const ofVec3f& axis_ );

	// push currentMatrix state
	void push();
	// pop current Matrix state
	void pop();

	// vertex memory operations

	// store vertex and index data inside the current dynamic memory frame
	// return memory mapping offets based on current memory buffer.
	bool storeMesh( const ofMesh& mesh_, std::vector<VkDeviceSize>& vertexOffsets, std::vector<VkDeviceSize>& indexOffsets );



};

} // namespace vk
} // namespace of