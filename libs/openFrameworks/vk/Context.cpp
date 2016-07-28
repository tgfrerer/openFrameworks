#include "Context.h"
#include "ofVkRenderer.h"
#include "vk/vkAllocator.h"
#include "vk/Shader.h"


/*

We want context to track current Pipeline state. 
Any draw state change that affects Pipeline state dirties affected PSO state.

If PSO state is dirty - this means we have to change pipeline before next draw.

On pipeline state change request, first look up if a pipeline with the requested 
state already exists in cache -> the lookup could be through a hash. 

	If Yes, bind the cached pipeline.
	If No, compile, bind, and cache pipeline.

The same thing needs to hold true for descriptorSets - if there is a change in 
texture state requested, we need to check if we already have a descriptorset that
covers this texture with the inputs requested. 

If not, allocate and cache a new descriptorset - The trouble here is that we cannot
store this effectively, i.e. we cannot know how many descriptors to reserve in the 
descriptorpool.

*/


// ----------------------------------------------------------------------
of::vk::Context::Context(const of::vk::Context::Settings& settings_)
: mSettings(settings_) {
}
// ----------------------------------------------------------------------

void of::vk::Context::setup(ofVkRenderer* renderer_){

	of::vk::Allocator::Settings settings{};
	settings.device = mSettings.device;
	settings.renderer = renderer_;
	settings.frames = uint32_t(mSettings.numSwapchainImages);
	settings.size = ( 2UL << 24 ) * settings.frames;  // (16 MB * number of swapchain images)

	mAlloc = std::make_shared<of::vk::Allocator>(settings);
	mAlloc->setup();

	mCurrentShader = mSettings.shaders.front();

	mDynamicUniformBuffferOffsets.resize( mSettings.numSwapchainImages );

	setupDescriptorSetsFromShaders();
	setupFrameStateFromShaders();
	
}
// ----------------------------------------------------------------------

void of::vk::Context::setupFrameStateFromShaders(){

	// Frame holds stacks of memory, used to track
	// current state for each uniform member 
	// currently bound. 
	Frame frame;

	// set space aside to back all descriptorsets 
	for ( const auto & l : mDescriptorSetLayouts ){
		const auto & key = l.first;
		const auto & layout = l.second;

		auto & setState = frame.mUniformBufferState[key] = DescriptorSetState();
		
		setState.bindingOffsets.resize( layout.bindings.size(), 0 );

		for ( const auto &binding : layout.bindings ){
			UniformBufferState uboState;
			uboState.name = binding.name;
			uboState.struct_size = binding.size;
			uboState.bindingId = binding.binding.binding;
			uboState.state.data.resize( binding.size, 0 );

			setState.bindings.emplace_back( std::move( uboState ));
			UniformBufferState * lastUniformBufferStateAddr = &(setState.bindings.back());

			for ( const auto & member : binding.memberRanges ){
				const auto & range       = member.second;
				const auto & uniformName = member.first;
				UniformMember m;
				m.offset = uint32_t(range.offset);
				m.range  = uint32_t(range.range);
				m.buffer = lastUniformBufferStateAddr;
				frame.mUniformMembers[uniformName] = std::move( m );
			}
		}
		
	}

	mCurrentFrameState = std::move( frame );
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

// Create a descriptor pool that has enough of each descriptor type as
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

		for ( const auto & bindingInfo : u.second.bindings ){
			const auto & it = poolCounts.find( bindingInfo.binding.descriptorType );
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

	uint32_t setCount = uint32_t(setLayouts_.size());	 // number of unique descriptorSets

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
	    uint32_t(poolSizes.size()),                                          // uint32_t                       poolSizeCount;
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
	
	// We need to store buffer info temporarily as the VkWriteDescriptorSet needs 
	// to point to a resource outside of the scope of the for loop it is created
	// within.
	std::map < uint64_t, std::vector<VkDescriptorBufferInfo>> bufferInfoStore; 

	// iterate over all setLayouts (since each element corresponds to a descriptor set)
	for (auto & layout : setLayouts_ )
	{
		const auto& key = layout.first;
		const auto& layoutInfo = layout.second.bindings;
		// !TODO: deal with bindings which are not uniform buffers.

		// Since within context all our uniform bindings 
		// are dynamic, we should be able to bind them all to the same buffer
		// and the same base address. When drawing, the dynamic offset should point to 
		// the correct memory location for each ubo element.
		
		// Note that here, you point the writeDescriptorSet to dstBinding and dstSet; 
		// if descriptorCount was greater than the number of bindings in the set, 
		// the next bindings will be overwritten.

		uint32_t descriptor_array_count = 0;

		// We need to get the number of descriptors by accumulating the descriptorCount
		// over each layoutBinding

		// Reserve vector size because otherwise reallocation when pushing will invalidate pointers
		bufferInfoStore[key].reserve( layoutInfo.size() ); 
		
		// Go over each binding in descriptorSetLayout
		for ( const auto &bindingInfo : layoutInfo ){
			// how many array elements in this binding?
			descriptor_array_count = bindingInfo.binding.descriptorCount;
			
			// It appears that writeDescriptorSet does not immediately consume VkDescriptorBufferInfo*
			// so we must make sure that this is around for when we need it:

			bufferInfoStore[key].push_back( {
				mAlloc->getBuffer(),                // VkBuffer        buffer;
				0,                                  // VkDeviceSize    offset;		// we start any new binding at offset 0, as data for each descriptor will always be separately allocated and uploaded.
				bindingInfo.size                    // VkDeviceSize    range;
			} );
			
			const auto & bufElement = bufferInfoStore[key].back();

			// Q: Is it possible that elements of a descriptorSet are of different VkDescriptorType?
			//
			// A: Yes. This is why this method should write only one binding (== Descriptor) 
			//    at a time - as all members of a binding must share the same VkDescriptorType.
			
			auto descriptorType = bindingInfo.binding.descriptorType;
			auto dstBinding     = bindingInfo.binding.binding;

			// Create one writeDescriptorSet per binding.

			VkWriteDescriptorSet tmpDescriptorSet{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,                    // VkStructureType                  sType;
				nullptr,                                                   // const void*                      pNext;
				mDescriptorSets[key],                                      // VkDescriptorSet                  dstSet;
				dstBinding,                                                // uint32_t                         dstBinding;
				0,                                                         // uint32_t                         dstArrayElement; // starting element in array
				descriptor_array_count,                                    // uint32_t                         descriptorCount;
				descriptorType,                                            // VkDescriptorType                 descriptorType;
				nullptr,                                                   // const VkDescriptorImageInfo*     pImageInfo;
				&bufElement,                                               // const VkDescriptorBufferInfo*    pBufferInfo;
				nullptr,                                                   // const VkBufferView*              pTexelBufferView;
			};

			writeDescriptorSets.push_back( std::move( tmpDescriptorSet ) );
			
		}
		
	}

	vkUpdateDescriptorSets( mSettings.device, uint32_t(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL );
}

// ----------------------------------------------------------------------

void of::vk::Context::begin(size_t frame_){
	mSwapIdx = int(frame_);
	mAlloc->free(frame_);

	// TODO: bind shader here
	
	// make sure all shader uniforms are marked dirty when context is started fresh.
	for ( auto & uniformBuffer : mCurrentFrameState.mUniformMembers ){
		auto & buffer = *uniformBuffer.second.buffer;
		buffer.lastSavedStackId = -1;
		buffer.stateStack.clear();
		buffer.state.memoryOffset = 0;
		buffer.state.stackId = -1;
		buffer.state.data.resize( buffer.struct_size, 0 );
	}

}

// ----------------------------------------------------------------------

void of::vk::Context::end(){
	mSwapIdx = -1;
}

// ----------------------------------------------------------------------

void of::vk::Context::reset(){
	mAlloc->reset();
	// free any descriptorSets allocated if descriptorPool was already initialised.
	if ( mDescriptorPool != nullptr ){
		vkDestroyDescriptorPool( mSettings.device, mDescriptorPool, 0 );
		mDescriptorPool = nullptr;
		mDescriptorSets.clear();
	}
}

// ----------------------------------------------------------------------

const VkBuffer & of::vk::Context::getVkBuffer() const {
	return mAlloc->getBuffer();
}

// ----------------------------------------------------------------------

void of::vk::Context::pushBuffer( const std::string & ubo_ ){
	auto uboMemberWithParentWithName = std::find_if( mCurrentFrameState.mUniformMembers.begin(), mCurrentFrameState.mUniformMembers.end(),
		[&ubo_]( const std::pair<std::string, UniformMember> & lhs ) -> bool{
		return ( lhs.second.buffer->name == ubo_ );
	} );

	if ( uboMemberWithParentWithName != mCurrentFrameState.mUniformMembers.end() ){
		( uboMemberWithParentWithName->second.buffer->push() );
	}
}

// ----------------------------------------------------------------------

void of::vk::Context::popBuffer( const std::string & ubo_ ){
	auto uboMemberWithParentWithName = std::find_if( mCurrentFrameState.mUniformMembers.begin(), mCurrentFrameState.mUniformMembers.end(),
		[&ubo_]( const std::pair<std::string, UniformMember> & lhs ) -> bool{
		return ( lhs.second.buffer->name == ubo_ );
	} );

	if ( uboMemberWithParentWithName != mCurrentFrameState.mUniformMembers.end() ){
		( uboMemberWithParentWithName->second.buffer->pop() );
	}
}

// ----------------------------------------------------------------------

bool of::vk::Context::storeMesh( const ofMesh & mesh_, std::vector<VkDeviceSize>& vertexOffsets, std::vector<VkDeviceSize>& indexOffsets ){
	// CONSIDER: add option to interleave 
	
	uint32_t numVertices   = uint32_t(mesh_.getVertices().size() );
	uint32_t numColors     = uint32_t(mesh_.getColors().size()   );
	uint32_t numNormals    = uint32_t(mesh_.getNormals().size()  );
	uint32_t numTexCooords = uint32_t(mesh_.getTexCoords().size());
	uint32_t numIndices    = uint32_t( mesh_.getIndices().size() );

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

void of::vk::Context::flushUniformBufferState( ){

	mDynamicUniformBuffferOffsets[mSwapIdx].clear();

	// iterate over all currently bound descriptorsets

	for ( const auto& key : mCurrentShader->getSetLayoutKeys() ){
		DescriptorSetState & descriptorSetState = mCurrentFrameState.mUniformBufferState[key];

		std::vector<uint32_t>::iterator offsetIt = descriptorSetState.bindingOffsets.begin();
		
		// iterate over all currently bound descriptors 
		for ( auto &uniformBuffer : descriptorSetState.bindings ){

			// this is just for security.
			if ( offsetIt == descriptorSetState.bindingOffsets.end() ){
				ofLogError() << "Device offsets list is not of same size as uniformbuffer list.";
				break;
			}

			// only write to GPU if descriptor is dirty
			if ( uniformBuffer.state.stackId == -1 ){

				void * pDst = nullptr;
				
				VkDeviceSize numBytes = uniformBuffer.struct_size;
				VkDeviceSize newOffset = 0;	// device GPU memory offset for this buffer 
				auto success = mAlloc->allocate( numBytes, pDst, newOffset, mSwapIdx );
				*offsetIt = (uint32_t)newOffset; // store offset into offsets list.
				if ( !success ){
					ofLogError() << "out of buffer space.";
				}
				// ----------| invariant: allocation successful

				// Save data into GPU buffer
				memcpy( pDst, uniformBuffer.state.data.data(), numBytes );
				// store GPU memory offset with data
				uniformBuffer.state.memoryOffset = newOffset;

				++uniformBuffer.lastSavedStackId; 
				uniformBuffer.state.stackId = uniformBuffer.lastSavedStackId;
				
			} else { 
				// otherwise, just re-use old memory offset, and therefore old memory
				*offsetIt = uniformBuffer.state.memoryOffset;
			}

			++offsetIt;
		}

		// now append descriptorOffsets for this set to vector of descriptorOffsets for this layout
		mDynamicUniformBuffferOffsets[mSwapIdx].insert(
			mDynamicUniformBuffferOffsets[mSwapIdx].end(),
			descriptorSetState.bindingOffsets.begin(), descriptorSetState.bindingOffsets.end() 
		);

	}	// end for ( const auto& key : mCurrentShader->getSetLayoutKeys() )

}

// ----------------------------------------------------------------------

const std::vector<uint32_t>& of::vk::Context::getDynamicUniformBufferOffsets() const{
	return  mDynamicUniformBuffferOffsets[mSwapIdx];
}

// ----------------------------------------------------------------------

void of::vk::Context::setViewMatrix( const glm::mat4x4 & mat_ ){
	setUniform( "viewMatrix", mat_ );
}

// ----------------------------------------------------------------------

void of::vk::Context::setProjectionMatrix( const glm::mat4x4 & mat_ ){
	setUniform( "projectionMatrix", mat_ );
}

// ----------------------------------------------------------------------

void of::vk::Context::translate( const glm::vec3& v_ ){
	getUniform<glm::mat4x4>( "modelMatrix" ) = glm::translate( getUniform<glm::mat4x4>( "modelMatrix" ), v_ );
		
}

// ----------------------------------------------------------------------

void of::vk::Context::rotateRad( const float& radians_, const glm::vec3& axis_ ){
	getUniform<glm::mat4x4>( "modelMatrix" ) = glm::rotate( getUniform<glm::mat4x4>( "modelMatrix" ), radians_, axis_ );
}

