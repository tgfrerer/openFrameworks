#include "Context.h"
#include "ofVkRenderer.h"
#include "vk/vkAllocator.h"
#include "vk/Shader.h"
#include "vk/Pipeline.h"
#include "spooky/SpookyV2.h"

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
	settings.frames = uint32_t(mSettings.numVirtualFrames);
	settings.size = ( 2UL << 24 ) * settings.frames;  // (16 MB * number of swapchain images)

	mAlloc = std::make_shared<of::vk::Allocator>(settings);
	mAlloc->setup();


	// CONSIDER: as the pipeline cache is one of the few elements which is actually mutexed 
	// by vulkan, we could share a cache over mulitple contexts and the cache could therefore
	// be owned by the renderer wich in turn owns the contexts.  
	mPipelineCache = of::vk::createPipelineCache( mSettings.device, "ofAppPipelineCache.bin" );

}

// ----------------------------------------------------------------------

void of::vk::Context::reset(){
	

	if ( nullptr != mPipelineCache ){
		vkDestroyPipelineCache( mSettings.device, mPipelineCache, nullptr );
		mPipelineCache = nullptr;
	}

	// Destroy all descriptors by destroying the pools they were
	// allocated from.
	for ( auto & p : mDescriptorPool ){
		vkDestroyDescriptorPool( mSettings.device, p, 0 );
		p = nullptr;
	}
	mDescriptorPool.clear();

	// ! TODO: create pipeline layout manager - 
	// so that you don't have to destroy shaders 
	mShaders.clear();

	mAlloc->reset();

	for ( auto &p : mVkPipelines ){
		if ( nullptr != p.second ){
			vkDestroyPipeline( mSettings.device, p.second, nullptr );
			p.second = nullptr;
		}
	}
	mVkPipelines.clear();

}

// ----------------------------------------------------------------------

void of::vk::Context::addShader( std::shared_ptr<of::vk::Shader> shader_ ){
	if ( mCurrentFrameState.initialised ){
		ofLogError() << "Cannot add shader after Context has been initialised. Add shader before you begin context for the first time.";
	} else{
		mShaders.push_back( shader_ );
	}
}

// ----------------------------------------------------------------------

void of::vk::Context::initialiseFrameState(){

	// Frame holds stacks of memory, used to track
	// current state for each uniform member 
	// currently bound. 
	Frame frame;

	// set space aside to back all descriptorsets 
	for ( const auto & l : mShaderManager->getDescriptorSetLayouts() ){
		const auto & key = l.first;
		const auto & layout = l.second->setLayout;

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

// Create a descriptor pool that has enough of each descriptor type as
// referenced in our map of SetLayouts held in mDescriptorSetLayout
// this might, if a descriptorPool was previously allocated, 
// reset that descriptorPool and also delete any descriptorSets associated
// with that descriptorPool.
void of::vk::Context::setupDescriptorPool(){
	// To know how many descriptors of each type to allocate, 
	// we group descriptors over all layouts by type and count each group.

	// TODO: better ask the shader manager for an estimate based on all shaders
	const auto & descriptorSetLayouts = mShaderManager->getDescriptorSetLayouts();

	// Group descriptors by type over all unique DescriptorSetLayouts
	std::map<VkDescriptorType, uint32_t> poolCounts; // size of pool necessary for each descriptor type
	for ( const auto &u : descriptorSetLayouts ){

		for ( const auto & bindingInfo : u.second->setLayout.bindings ){
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

	// TODO: we know that we can re-use dynamic buffer uniforms, 
	// but for each image sampler that we encounter, let's allocate 
	// about 16 image descriptors...

	// ---------| invariant: poolCounts holds per-descriptorType count of descriptors

	std::vector<VkDescriptorPoolSize> poolSizes;
	poolSizes.reserve( poolCounts.size() );

	for ( auto &p : poolCounts ){
		poolSizes.push_back( { p.first, p.second } );
	}

	uint32_t setCount = uint32_t( descriptorSetLayouts.size() );	 // number of unique descriptorSets


	if ( !mDescriptorPool.empty() ){
		// reset any currently set descriptorpools if necessary.
		for ( auto & p : mDescriptorPool ){
			ofLogNotice() << "DescriptorPool re-initialised. Resetting.";
			vkResetDescriptorPool( mSettings.device, p, 0 );
		}
	} else {
		// Create a pool for this context - each swapchain image has its own version of the pool
		// All descriptors used by shaders associated to this context 
		// will come from this pool
		VkDescriptorPoolCreateInfo descriptorPoolInfo = {
			VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,  // VkStructureType                sType;
			nullptr,                                        // const void*                    pNext;
			0,                                              // VkDescriptorPoolCreateFlags    flags;
			setCount,                                       // uint32_t                       maxSets;
			uint32_t( poolSizes.size() ),                   // uint32_t                       poolSizeCount;
			poolSizes.data(),                               // const VkDescriptorPoolSize*    pPoolSizes;
		};

		// create as many descriptorpools as there are swapchain images.
		mDescriptorPool.resize( mSettings.numVirtualFrames );
		for ( size_t i = 0; i != mSettings.numVirtualFrames; ++i ){
			auto err = vkCreateDescriptorPool( mSettings.device, &descriptorPoolInfo, nullptr, &mDescriptorPool[i] );
			assert( !err );
		}
	}
}

// ----------------------------------------------------------------------

void of::vk::Context::begin(size_t frame_){
	mFrameIndex = int(frame_);
	mAlloc->free(frame_);

	// DescriptorPool and frameState are set up based on 
	// the current library of DescriptorSetLayouts inside
	// the ShaderManager.

	if ( mCurrentFrameState.initialised == false ){
		// We defer setting up descriptor pool 
		// and framestate to when its first used here.
		setupDescriptorPool();
		initialiseFrameState();
		mCurrentFrameState.initialised = true;
	}

	// make sure all shader uniforms are marked dirty when context is started fresh.
	for ( auto & uniformBuffer : mCurrentFrameState.mUniformMembers ){
		auto & buffer = *uniformBuffer.second.buffer;
		buffer.lastSavedStackId = -1;
		buffer.stateStack.clear();
		buffer.state.memoryOffset = 0;
		buffer.state.stackId = -1;
		buffer.state.data.resize( buffer.struct_size, 0 );
	}

	// Reset current DescriptorPool

	vkResetDescriptorPool( mSettings.device, mDescriptorPool[frame_], 0 );
	
	// reset pipeline state
	mCurrentGraphicsPipelineState.reset();
	{
		mCurrentGraphicsPipelineState.setShader( mShaders.front() );
		mCurrentGraphicsPipelineState.setRenderPass(mSettings.defaultRenderPass);	  /* !TODO: we should porbably expose this - and bind a default renderpass here */
	}

	mDSS_layoutKey.clear();
	mDSS_dirty.clear();
	mDSS_set.clear();

}

// ----------------------------------------------------------------------

void of::vk::Context::end(){
}

// ----------------------------------------------------------------------

of::vk::Context & of::vk::Context::setShader( const std::shared_ptr<of::vk::Shader>& shader_ ){
	mCurrentGraphicsPipelineState.setShader( shader_ );
	return *this;
}

// ----------------------------------------------------------------------

of::vk::Context & of::vk::Context::setRenderPass( const VkRenderPass & renderpass_ ){
	mCurrentGraphicsPipelineState.setRenderPass( renderpass_ );
	return *this;
}

// ----------------------------------------------------------------------

const VkBuffer & of::vk::Context::getVkBuffer() const {
	return mAlloc->getBuffer();
}

// ----------------------------------------------------------------------

of::vk::Context& of::vk::Context::pushBuffer( const std::string & ubo_ ){
	auto uboMemberWithParentWithName = std::find_if( mCurrentFrameState.mUniformMembers.begin(), mCurrentFrameState.mUniformMembers.end(),
		[&ubo_]( const std::pair<std::string, UniformMember> & lhs ) -> bool{
		return ( lhs.second.buffer->name == ubo_ );
	} );

	if ( uboMemberWithParentWithName != mCurrentFrameState.mUniformMembers.end() ){
		( uboMemberWithParentWithName->second.buffer->push() );
	}
	return *this;
}

// ----------------------------------------------------------------------

of::vk::Context& of::vk::Context::popBuffer( const std::string & ubo_ ){
	auto uboMemberWithParentWithName = std::find_if( mCurrentFrameState.mUniformMembers.begin(), mCurrentFrameState.mUniformMembers.end(),
		[&ubo_]( const std::pair<std::string, UniformMember> & lhs ) -> bool{
		return ( lhs.second.buffer->name == ubo_ );
	} );

	if ( uboMemberWithParentWithName != mCurrentFrameState.mUniformMembers.end() ){
		( uboMemberWithParentWithName->second.buffer->pop() );
	}
	return *this;
}

// ----------------------------------------------------------------------

of::vk::Context& of::vk::Context::draw(const VkCommandBuffer& cmd, const ofMesh & mesh_){

	// store uniforms if needed
	flushUniformBufferState();
	bindPipeline( cmd );
	bindDescriptorSets( cmd );

	std::vector<VkDeviceSize> vertexOffsets;
	std::vector<VkDeviceSize> indexOffsets;

	// Store vertex data using Context.
	// - this uses Allocator to store mesh data in the current frame' s dynamic memory
	// Context will return memory offsets into vertices, indices, based on current context memory buffer
	// 
	// TODO: check if it made sense to cache already stored meshes, 
	//       so that meshes which have already been stored this frame 
	//       may be re-used.
	storeMesh( mesh_, vertexOffsets, indexOffsets );

	// TODO: cull vertexOffsets which refer to empty vertex attribute data
	//       make sure that a pipeline with the correct bindings is bound to match the 
	//       presence or non-presence of mesh data.

	// Bind vertex data buffers to current pipeline. 
	// The vector indices into bufferRefs, vertexOffsets correspond to [binding numbers] of the currently bound pipeline.
	// See Shader.h for an explanation of how this is mapped to shader attribute locations
	vector<VkBuffer> bufferRefs( vertexOffsets.size(), getVkBuffer() );
	vkCmdBindVertexBuffers( cmd, 0, uint32_t( bufferRefs.size() ), bufferRefs.data(), vertexOffsets.data() );

	if ( indexOffsets.empty() ){
		// non-indexed draw
		vkCmdDraw( cmd, uint32_t( mesh_.getNumVertices() ), 1, 0, 1 );
	} else{
		// indexed draw
		vkCmdBindIndexBuffer( cmd, bufferRefs[0], indexOffsets[0], VK_INDEX_TYPE_UINT32 );
		vkCmdDrawIndexed( cmd, uint32_t( mesh_.getNumIndices() ), 1, 0, 0, 1 );
	}
	return *this;
}

// ----------------------------------------------------------------------

bool of::vk::Context::storeMesh( const ofMesh & mesh_, std::vector<VkDeviceSize>& vertexOffsets, std::vector<VkDeviceSize>& indexOffsets ){
	// CONSIDER: add option to interleave 
	
	uint32_t numVertices   = uint32_t(mesh_.getVertices().size() );
	uint32_t numColors     = uint32_t(mesh_.getColors().size()   );
	uint32_t numNormals    = uint32_t(mesh_.getNormals().size()  );
	uint32_t numTexCooords = uint32_t(mesh_.getTexCoords().size());
	uint32_t numIndices    = uint32_t( mesh_.getIndices().size() );

	// CONSIDER: add error checking - make sure 
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
	if ( mAlloc->allocate( numBytes, pData, vertexOffsets[0], mFrameIndex ) ){
		memcpy( pData, mesh_.getVerticesPointer(), numBytes );
	};

	// binding number 1
	numBytes = numColors * sizeof( ofFloatColor );
	if ( mAlloc->allocate( numBytes, pData, vertexOffsets[1], mFrameIndex ) ){
		memcpy( pData, mesh_.getColorsPointer(), numBytes );
	};

	// binding number 2
	numBytes = numNormals * sizeof( ofVec3f );
	if ( mAlloc->allocate( numBytes, pData, vertexOffsets[2], mFrameIndex ) ){
		memcpy( pData, mesh_.getNormalsPointer(), numBytes );
	};

	numBytes = numTexCooords * sizeof( ofVec2f );
	if ( mAlloc->allocate( numBytes, pData, vertexOffsets[3], mFrameIndex ) ){
		memcpy( pData, mesh_.getTexCoordsPointer(), numBytes );
	};


	VkDeviceSize indexOffset = 0;
	numBytes = numIndices * sizeof( ofIndexType );
	if ( mAlloc->allocate( numBytes, pData, indexOffset, mFrameIndex ) ){
		indexOffsets.push_back( indexOffset );
		memcpy( pData, mesh_.getIndexPointer(), numBytes );
	};

	return false;
}

// ----------------------------------------------------------------------

void of::vk::Context::flushUniformBufferState( ){

	// iterate over all currently bound descriptorsets

	for ( const auto& key : mCurrentGraphicsPipelineState.getShader()->getSetLayoutKeys() ){
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
				auto success = mAlloc->allocate( numBytes, pDst, newOffset, mFrameIndex );
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

			} else{
				// otherwise, just re-use old memory offset, and therefore old memory
				*offsetIt = uniformBuffer.state.memoryOffset;
			}

			++offsetIt;
		}
	}
}

// ----------------------------------------------------------------------

void of::vk::Context::bindDescriptorSets( const VkCommandBuffer & cmd ){
	
	// Update mDSS_set
	updateDescriptorSetState();
	const auto & boundVkDescriptorSets = mDSS_set;
	const auto & currentShader = mCurrentGraphicsPipelineState.getShader();

	// get dynamic offsets for currently bound descriptorsets
	// now append descriptorOffsets for this set to vector of descriptorOffsets for this layout
	std::vector<uint32_t> dynamicBindingOffsets;

	for ( const auto& key : currentShader->getSetLayoutKeys() ){
		DescriptorSetState & descriptorSetState = mCurrentFrameState.mUniformBufferState[key];
		dynamicBindingOffsets.insert(
			dynamicBindingOffsets.end(),
			descriptorSetState.bindingOffsets.begin(), descriptorSetState.bindingOffsets.end()
		);
	}

	// we build dynamic offsets by going over each of the currently bound descriptorSets in 
	// currentlyBoundDescriptorsets, and for each dynamic binding within these sets, we add an offset to the list.
	// we must guarantee that dynamicOffsets has the same number of elements as currentlBoundDescriptorSets has descriptors
	// the number of descriptors is calculated by summing up all descriptorCounts per binding per descriptorSet

	// Bind uniforms (the first set contains the matrices)
	vkCmdBindDescriptorSets(
		cmd,
		VK_PIPELINE_BIND_POINT_GRAPHICS,                  // use graphics, not compute pipeline
		*currentShader->getPipelineLayout(),              // VkPipelineLayout object used to program the bindings.
		0, 						                          // firstset: first set index (of the above) to bind to - mDescriptorSet[0] will be bound to pipeline layout [firstset]
		uint32_t( boundVkDescriptorSets.size() ),         // setCount: how many sets to bind
		boundVkDescriptorSets.data(),                     // the descriptor sets to match up with our mPipelineLayout (need to be compatible)
		uint32_t( dynamicBindingOffsets.size() ),         // dynamic offsets count how many dynamic offsets
		dynamicBindingOffsets.data()                      // dynamic offsets for each descriptor
	);
}

// ----------------------------------------------------------------------

const std::vector<VkDescriptorSet>& of::vk::Context::updateDescriptorSetState(){

	// Allocate & update any descriptorSets - from the first dirty
	// descriptorSet to the end of the current descriptorSet sequence.
		
		for ( int i = 0; i != mDSS_dirty.size(); ++i ){
			if ( mDSS_dirty[i] ){

				std::vector<VkDescriptorSetLayout> layouts;
				layouts.reserve( mDSS_dirty.size() - i );
				
				for ( size_t j = i; j != mDSS_dirty.size(); ++j ){
					layouts.push_back(mShaderManager->getDescriptorSetLayout(mDSS_layoutKey[j]) );
				}

				VkDescriptorSetAllocateInfo allocInfo{
					VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,	 // VkStructureType                 sType;
					nullptr,	                                     // const void*                     pNext;
					mDescriptorPool[mFrameIndex],                       // VkDescriptorPool                descriptorPool;
					uint32_t(layouts.size()),                        // uint32_t                        descriptorSetCount;
					layouts.data()                                   // const VkDescriptorSetLayout*    pSetLayouts;
				};

				vkAllocateDescriptorSets( mSettings.device, &allocInfo, &mDSS_set[i] );
				updateDescriptorSets( layouts, i );

				break;
			}
		}

	return mDSS_set;
}

// ----------------------------------------------------------------------

void of::vk::Context::updateDescriptorSets( std::vector<VkDescriptorSetLayout> &layouts, int firstSetIdx ){

	// We assume mDSS_set[firstSetIdx .. mDSS_set.size()-1] are untyped, 
	// and have not yet been used for drawing. We therefore write type
	// information, and binding information into these descriptorSets.
		
	std::vector<VkWriteDescriptorSet> writeDescriptorSets;
	writeDescriptorSets.reserve( layouts.size() );

	// Temporary storage for VkDescriptorBufferInfo
	// so this can be pointed to from within the loop.
	std::map < uint64_t, std::vector<VkDescriptorBufferInfo>> descriptorBufferInfoStorage;

	// iterate over all setLayouts (since each element corresponds to a DescriptorSet)
	for ( size_t j = firstSetIdx; j != mDSS_dirty.size(); ++j ){
		const auto& key = mDSS_layoutKey[j];
		const auto& bindings = mShaderManager->getBindings( mDSS_layoutKey[j] );
		// TODO: deal with bindings which are not uniform buffers.

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
		descriptorBufferInfoStorage[key].reserve( bindings.size() );

		// Go over each binding in descriptorSetLayout
		for ( const auto &bindingInfo : bindings ){
			// how many array elements in this binding?
			descriptor_array_count = bindingInfo.binding.descriptorCount;

			// It appears that writeDescriptorSet does not immediately consume VkDescriptorBufferInfo*
			// so we must make sure that this is around for when we need it:

			descriptorBufferInfoStorage[key].push_back( {
				mAlloc->getBuffer(),                // VkBuffer        buffer;
				0,                                  // VkDeviceSize    offset;		// we start any new binding at offset 0, as data for each descriptor will always be separately allocated and uploaded.
				bindingInfo.size                    // VkDeviceSize    range;
			} );

			const auto & bufElement = descriptorBufferInfoStorage[key].back();

			// Q: Is it possible that elements of a descriptorSet are of different VkDescriptorType?
			//
			// A: Yes. This is why this method should write only one binding (== Descriptor) 
			//    at a time - as all members of a binding must share the same VkDescriptorType.

			auto descriptorType = bindingInfo.binding.descriptorType;
			auto dstBinding = bindingInfo.binding.binding;

			// Create one writeDescriptorSet per binding.

			VkWriteDescriptorSet tmpDescriptorSet{
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,                    // VkStructureType                  sType;
				nullptr,                                                   // const void*                      pNext;
				mDSS_set[j],                                               // VkDescriptorSet                  dstSet;
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

		mDSS_dirty[j] = false;
	}

	vkUpdateDescriptorSets( mSettings.device, uint32_t( writeDescriptorSets.size() ), writeDescriptorSets.data(), 0, NULL );

}

// ----------------------------------------------------------------------
void of::vk::Context::bindPipeline( const VkCommandBuffer & cmd ){

	// If the current pipeline state is not dirty, no need to bind something 
	// that is already bound. Return immediately.
	//
	// Otherwise try to bind a cached pipeline for current pipeline state.
	//
	// If cache lookup fails, new pipeline needs to be compiled at this point. 
	// This can be very costly.
	
	if ( !mCurrentGraphicsPipelineState.mDirty ){
		return;
	} else {
		
		const std::vector<uint64_t>& layouts = mCurrentGraphicsPipelineState.getShader()->getSetLayoutKeys();

		uint64_t pipelineHash = mCurrentGraphicsPipelineState.calculateHash(); 

		auto p = mVkPipelines.find( pipelineHash );
		if ( p == mVkPipelines.end() ){
			ofLog() << "Creating pipeline " << std::hex << pipelineHash;
			mVkPipelines[pipelineHash] = mCurrentGraphicsPipelineState.createPipeline( mSettings.device, mPipelineCache );
		} 
		mCurrentVkPipeline = mVkPipelines[pipelineHash];

		mDSS_layoutKey.resize( layouts.size(), 0);
		mDSS_dirty.resize(     layouts.size(), true );
		mDSS_set.resize(       layouts.size(), nullptr );

		bool foundIncompatible = false; // invalidate all set bindings after and including first incompatible set
		for ( size_t i = 0; i != layouts.size(); ++i ){
			if ( mDSS_layoutKey[i] != layouts[i] 
				|| foundIncompatible ){
				mDSS_layoutKey[i] = layouts[i];
				mDSS_set[i] = nullptr;
				mDSS_dirty[i] = true;
				foundIncompatible = true;
			}
		}

		// Bind the rendering pipeline (including the shaders)
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mCurrentVkPipeline );
		mCurrentGraphicsPipelineState.mDirty = false;
	}

}

// ----------------------------------------------------------------------

of::vk::Context& of::vk::Context::setViewMatrix( const glm::mat4x4 & mat_ ){
	setUniform( "viewMatrix", mat_ );
	return *this;
}

// ----------------------------------------------------------------------

of::vk::Context& of::vk::Context::setProjectionMatrix( const glm::mat4x4 & mat_ ){
	setUniform( "projectionMatrix", mat_ );
	return *this;
}

// ----------------------------------------------------------------------

of::vk::Context& of::vk::Context::translate( const glm::vec3& v_ ){
	getUniform<glm::mat4x4>( "modelMatrix" ) = glm::translate( getUniform<glm::mat4x4>( "modelMatrix" ), v_ );
	return *this;
}

// ----------------------------------------------------------------------

of::vk::Context& of::vk::Context::rotateRad( const float& radians_, const glm::vec3& axis_ ){
	getUniform<glm::mat4x4>( "modelMatrix" ) = glm::rotate( getUniform<glm::mat4x4>( "modelMatrix" ), radians_, axis_ );
	return *this;
}

