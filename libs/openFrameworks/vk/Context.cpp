#include "Context.h"
#include "ofVkRenderer.h"
#include "vk/vkAllocator.h"
#include "vk/Shader.h"

// ----------------------------------------------------------------------
of::vk::Context::Context(const of::vk::Context::Settings& settings_)
: mSettings(settings_) {

}
// ----------------------------------------------------------------------

void of::vk::Context::setup(ofVkRenderer* renderer_){

	// The most important shader uniforms are the matrices
	// model, view, and projection matrix
	
	of::vk::Allocator::Settings settings{};
	settings.device = mSettings.device;
	settings.renderer = renderer_;
	settings.frames = mSettings.numSwapchainImages;
	settings.size = ( 2UL << 24 ) * settings.frames;  // (16 MB * number of swapchain images)

	mAlloc = std::make_shared<of::vk::Allocator>(settings);
	mAlloc->setup();

	mMatrixStateBufferInfo = {
		mAlloc->getBuffer(),   // VkBuffer        buffer;
		0,                     // VkDeviceSize    offset;
		sizeof(MatrixState),   // VkDeviceSize    range;
	};

	mStyleStateBufferInfo = {
		mAlloc->getBuffer(),
		0,
		sizeof( StyleState ),
	};

	if (!mFrames.empty())
		mFrames.clear();
	
	mFrames.resize( mSettings.numSwapchainImages, ContextState() );
	mDynamicOffsets.resize( mSettings.numSwapchainImages );

	mCurrentShader = mSettings.shaders.front();
	setupDescriptorSetsFromShaders();
	
}

// ----------------------------------------------------------------------

bool of::vk::Context::setupDescriptorSetsFromShaders(){

	// 1.   Get shader uniforms from all shaders
	// 1.1. Check uniforms with same name are identical in what they describe
	// 2.   Count all unique uniforms * array size each
	// 2.2  Make sure descriptorpool is large enough to hold at least twice that size
	// 2.   Create descriptorSetLayouts based on uniforms (that is, descriptorSetLayoutBinding)
	// 3.   Allocate descriptorSets from pool based on descriptorSetLayouts 

	// get all unique set layouts over all shaders attached to 
	// this context.
	for ( const auto &shd : mSettings.shaders ){
		for ( const auto & layout : shd->getSetLayouts() ){
			mDescriptorSetLayouts[layout.key] = layout;
		}
	}

	// Setup the descriptor pool so that it has enough space to accomodate 
	// for the total number of descriptors of each type specified over all 
	// elements in mDescriptorSetLayouts
	setupDescriptorPool( mDescriptorSetLayouts, mDescriptorPool, mDescriptorSets );

	// Allocate a descriptorSet for each unique descriptor set layout
	allocateDescriptorSets( mDescriptorSetLayouts, mDescriptorPool, mDescriptorSets );

	// initialise freshly allocated descriptorSets - and point them all to our unified 
	// dynamic memory buffer.
	initialiseDescriptorSets(mDescriptorSetLayouts, mDescriptorSets);

	return true;
}

// ----------------------------------------------------------------------

void of::vk::Context::allocateDescriptorSets( const std::map<uint64_t, of::vk::Shader::SetLayout>& setLayouts_, const VkDescriptorPool& descriptorPool_, std::map<uint64_t, VkDescriptorSet>& descriptorSets_ ){
	for ( const auto &layouts : setLayouts_ ){
		const auto & key = layouts.first;
		const auto & layout = layouts.second.vkLayout;

		// now allocate descriptorSet to back this layout up
		auto & newDescriptorSet = descriptorSets_[key] = VkDescriptorSet();

		VkDescriptorSetAllocateInfo allocInfo{
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,	                  // VkStructureType                 sType;
			nullptr,	                                                      // const void*                     pNext;
			descriptorPool_,                                                  // VkDescriptorPool                descriptorPool;
			1,                                                                // uint32_t                        descriptorSetCount;
			&layout                                                           // const VkDescriptorSetLayout*    pSetLayouts;
		};
		vkAllocateDescriptorSets( mSettings.device, &allocInfo, &newDescriptorSet );
	}
}

// ----------------------------------------------------------------------

// create a descriptor pool that has enough of each descriptor type as
// referenced in our map of SetLayouts held in mDescriptorSetLayout
// this might, if a descriptorPool was previously allocated, 
// reset that descriptorPool and also delete any descriptorSets associated
// with that descriptorPool.
void of::vk::Context::setupDescriptorPool( const std::map<uint64_t, of::vk::Shader::SetLayout>& setLayouts_, VkDescriptorPool& descriptorPool_, std::map<uint64_t, VkDescriptorSet>& descriptorSets_ )
{   
	// To know how many descriptors of each type to allocate, 
	// we group descriptors over all layouts by type and count each group.

	// Group descriptors by type over all unique uniforms
	std::map<VkDescriptorType, uint32_t> poolCounts; // size of pool necessary for each descriptor type
	for ( const auto &u : setLayouts_ ){

		for ( const auto & bindingInfo : u.second.bindingInfo ){
			auto & it = poolCounts.find( bindingInfo.binding.descriptorType );
			if ( it == poolCounts.end() ){
				// descriptor of this type not yet found - insert new
				poolCounts.emplace( bindingInfo.binding.descriptorType, bindingInfo.binding.descriptorCount );
			} else{
				// descriptor of this type already found - add count 
				it->second += bindingInfo.binding.descriptorCount;
			}
		}
	}

	// ---------| invariant: poolCounts holds per-descriptorType count of descriptors

	std::vector<VkDescriptorPoolSize> poolSizes;
	poolSizes.reserve( poolCounts.size() );

	for ( auto &p : poolCounts ){
		poolSizes.push_back( { p.first, p.second } );
	}

	uint32_t setCount = setLayouts_.size();	 // number of unique descriptorSets

	// free any descriptorSets allocated if descriptorPool was already initialised.
	if ( descriptorPool_ != nullptr ){
		ofLogNotice() << "DescriptorPool re-initialised. Resetting.";
		vkResetDescriptorPool( mSettings.device, descriptorPool_, 0 );
		descriptorPool_ = nullptr;
		descriptorSets_.clear();
	}

	// Create a pool for this context
	// All descriptors used by shaders associated to this context will come from this pool
	VkDescriptorPoolCreateInfo descriptorPoolInfo = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,                       // VkStructureType                sType;
		nullptr,                                                             // const void*                    pNext;
		0,                                                                   // VkDescriptorPoolCreateFlags    flags;
		setCount,                                                            // uint32_t                       maxSets;
		poolSizes.size(),                                                    // uint32_t                       poolSizeCount;
		poolSizes.data(),                                                    // const VkDescriptorPoolSize*    pPoolSizes;
	};

	VkResult vkRes = vkCreateDescriptorPool( mSettings.device, &descriptorPoolInfo, nullptr, &descriptorPool_ );
}

// ----------------------------------------------------------------------

void of::vk::Context::initialiseDescriptorSets( const std::map<uint64_t, of::vk::Shader::SetLayout>& setLayouts_, std::map<uint64_t, VkDescriptorSet>& descriptorSets_ ){
	// At this point the descriptors within the set are untyped 
	// so we have to write type information into it, 
	// as well as binding information so the set knows how to ingest data from memory

	std::vector<VkWriteDescriptorSet> writeDescriptorSets;
	writeDescriptorSets.reserve( mDescriptorSets.size() );
	// we need to store buffer info temporarily as the VkWriteDescriptorSet needs 
	// to point to a resource outside of the scope of the for loop it is created
	// within.
	std::map < uint64_t, std::vector<VkDescriptorBufferInfo>> bufferInfoStore; 

	// iterate over all setLayouts (since each element corresponds to a descriptor set)
	for (auto & layout : setLayouts_ )
	{
		const auto& key = layout.first;
		const auto& layoutInfo = layout.second.bindingInfo;
		// !TODO: deal with bindings which are not uniform buffers.

		// since within context all our uniform bindings
		// are dynamic, we should be able to bind them all to the same buffer
		// and the same base address. when drawing, the dynamic offset should point to 
		// the correct memory location for each ubo element.
		
		// this is a crass simplification, but if we can get away with it, the better =)

		// note that here, you point the writeDescriptorSet to dstBinding and dstSet, 
		// if descriptorCount is greater than the number of bindings in the set, 
		// the next bindings will be overwritten.

		uint32_t descriptor_array_count = 0;

		// we need to get the number of descriptors by accumulating the descriptorCount
		// over each layoutBinding

		std::vector<VkDescriptorBufferInfo> descriptorBufferInfo;
		descriptorBufferInfo.reserve( layoutInfo.size() );

		VkDeviceSize runningOffset = 0;
		// go over each binding in descriptorSetLayout
		for ( const auto &bindingInfo : layoutInfo ){
			// how many array elements in this binding?
			descriptor_array_count = bindingInfo.binding.descriptorCount;
			
			// It appears that writeDescriptorSet does not immediately consume VkDescriptorBufferInfo*
			// so we must make sure that this is around for when we need it:

			size_t firstBindingArrayIdx = 0;
			// repeat for every element in the binding array 
			for ( size_t i = 0; i != descriptor_array_count; ++i ){
				bufferInfoStore[key].push_back( {
					mAlloc->getBuffer(),                // VkBuffer        buffer;
					runningOffset,                      // VkDeviceSize    offset;
					bindingInfo.size                    // VkDeviceSize    range;
				} );
				
				runningOffset += bindingInfo.size;
				if ( i == 0 ){
					// remember index for array element 0 for this binding array
					firstBindingArrayIdx = bufferInfoStore[key].size() - 1;
				}
			}

			// TODO: Q: Is it possible that elements of a descriptorSet are of different types?
			//          If so, this will complicate this assignment, as this method only allows
			//          us to write elements of the same type.
			//       A: we can very strongly assume it is so, as any descriptors without named 
			//          set are placed into set 0
			// 
			// for now, assume all elements within a descriptorSet are of the same type as the first element
			auto descriptorType = bindingInfo.binding.descriptorType;
			auto dstBinding     = bindingInfo.binding.binding;

			

			// we create a writeDescriptorSet per binding.

			VkWriteDescriptorSet tmpDescriptorSet{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,                    // VkStructureType                  sType;
				nullptr,                                                   // const void*                      pNext;
				mDescriptorSets[key],                                      // VkDescriptorSet                  dstSet;
				dstBinding,                                                // uint32_t                         dstBinding;
				0,                                                         // uint32_t                         dstArrayElement; // starting element in array
				descriptor_array_count,                                    // uint32_t                         descriptorCount;
				descriptorType,                                            // VkDescriptorType                 descriptorType;
				nullptr,                                                   // const VkDescriptorImageInfo*     pImageInfo;
				&bufferInfoStore[key][firstBindingArrayIdx],               // const VkDescriptorBufferInfo*    pBufferInfo;
				nullptr,                                                   // const VkBufferView*              pTexelBufferView;
			};

			writeDescriptorSets.push_back( std::move( tmpDescriptorSet ) );
		}

		
	}

	vkUpdateDescriptorSets( mSettings.device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL );
}

// ----------------------------------------------------------------------

// ----------------------------------------------------------------------

void of::vk::Context::begin(size_t frame_){
	mSwapIdx = frame_;
	mAlloc->free(frame_);
	mFrames[mSwapIdx].mCurrentMatrixState = {}; // reset matrix state
	// we want as many dynamic offsets as descriptorSets, 
	// and we want them all to start at 0, when we begin the context.
	mDynamicOffsets[mSwapIdx] = std::vector<uint32_t>(mCurrentShader->getSetLayoutKeys().size(), 0);
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

std::vector<VkDescriptorBufferInfo> of::vk::Context::getDescriptorBufferInfo(std::string uboName_){
	// !TODO: IMPLEMENT!!! return correct buffer for ubo name
	// this means, we might have more than one UBO for the context. 
	// next to the matrices, we might want to set global colors and other dynamic 
	// uniform parameters for the default shaders via ubo
	return{ mMatrixStateBufferInfo, mStyleStateBufferInfo };
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
		// we store the dynamic offset into the first frame
		
		VkDeviceSize newOffset = 0;	// conversion here is annoying, but can't be helped.
		auto success = mAlloc->allocate( sizeof( MatrixState ), pData, newOffset, mSwapIdx );
		mDynamicOffsets[mSwapIdx][0] = (uint32_t)newOffset;

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
bool of::vk::Context::setUniform4f(ofFloatColor* pSource){
	
	// TODO: let shader perform uniform lookup, so that we know where to write to
	// The shader should be able to tell the offset per member for the buffer in question
	// if the shader kept track of member names, and per-member binding information. 

	void * pDst = nullptr;
	size_t setId = 1;
	VkDeviceSize numBytes = 4 * sizeof( float );
	VkDeviceSize newOffset = 0;	// conversion here is annoying, but can't be helped.
	auto success = mAlloc->allocate( numBytes, pDst, newOffset, mSwapIdx );
	mDynamicOffsets[mSwapIdx][setId] = (uint32_t)newOffset;

	if ( !success ){
		ofLogError() << "out of buffer space.";
		return false;
	}

	// ----------| invariant: allocation successful

	// Save data into GPU buffer
	memcpy( pDst, pSource, numBytes );

	return true;
}

// ----------------------------------------------------------------------

const std::vector<uint32_t>& of::vk::Context::getDynamicOffsetsForDescriptorSets() const{
	return  mDynamicOffsets[mSwapIdx];
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