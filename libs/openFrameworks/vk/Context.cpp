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

void of::vk::Context::addShader( std::shared_ptr<of::vk::Shader> shader_ ){
	mShaders.push_back( shader_ );

	// TODO: since this method may be called dynamically, we 
	// should process shaders here as well.

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

	mDynamicUniformBuffferOffsets.resize( mSettings.numSwapchainImages );


	// get pipeline layout for each shader based on its descriptorsetlayout
	// and then store this back into the shader, as it is unique to the shader.
	// CONSIDER: should this rather happen inside of the shader?

	for ( auto &s : mShaders ){
		// Derive pipelinelayout from 
		// vector of descriptorsetlayouts
		// we don't care too much about push constants yet.
		std::vector<VkDescriptorSetLayout> layouts;
		const auto & keys = s->getSetLayoutKeys();
		layouts.reserve( keys.size() );
		for ( const auto &k : keys ){
			layouts.push_back( mDescriptorSetLayouts[k]->vkDescriptorSetLayout );
	   }
		VkPipelineLayoutCreateInfo pipelineInfo{
			VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,    // VkStructureType                 sType;
			nullptr,                                          // const void*                     pNext;
			0,                                                // VkPipelineLayoutCreateFlags     flags;
			uint32_t(layouts.size()),                         // uint32_t                        setLayoutCount;
			layouts.data(),                                   // const VkDescriptorSetLayout*    pSetLayouts;
			0,                                                // uint32_t                        pushConstantRangeCount;
			nullptr                                           // const VkPushConstantRange*      pPushConstantRanges;
		};
		auto pipelineLayout = std::shared_ptr<VkPipelineLayout>( new VkPipelineLayout, [device=mSettings.device](VkPipelineLayout* lhs){
			vkDestroyPipelineLayout( device, *lhs, nullptr );
			lhs = nullptr;
		} );
		vkCreatePipelineLayout(mSettings.device,&pipelineInfo,nullptr,pipelineLayout.get());
		s->setPipelineLayout( pipelineLayout );
	}

	setupDescriptorPool();
	setupFrameState();

	// CONSIDER: as the pipeline cache is one of the few elements which is actually mutexed 
	// by vulkan, we could share a cache over mulitple contexts and the cache could therefore
	// be owned by the renderer wich in turn owns the contexts.  
	mPipelineCache = of::vk::createPipelineCache( mSettings.device, "ofAppPipelineCache.bin" );

	
}
// ----------------------------------------------------------------------

void of::vk::Context::setupFrameState(){

	// Frame holds stacks of memory, used to track
	// current state for each uniform member 
	// currently bound. 
	Frame frame;

	// set space aside to back all descriptorsets 
	for ( const auto & l : mDescriptorSetLayouts ){
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

void of::vk::Context::storeDescriptorSetLayout( Shader::SetLayout && setLayout_ ){

	// 1. check if layout already exists
	// 2. if yes, return shared pointer to existing layout
	// 3. if no, store layout in our map, and add then 2.

	auto & dsl = mDescriptorSetLayouts[setLayout_.key];

	if ( dsl.get() == nullptr ){
		
		// if no descriptor set was found, this means that 
		// no element with such hash exists yet in the registry.

		// create & store descriptorSetLayout based on bindings for this set
		// This means first to extract just the bindings from the data structure
		// so we can feed it to the vulkan api.
		std::vector<VkDescriptorSetLayoutBinding> bindings;

		bindings.reserve( setLayout_.bindings.size() );

		for ( const auto & b : setLayout_.bindings ){
			bindings.push_back( b.binding );
		}

		VkDescriptorSetLayoutCreateInfo createInfo{
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,    // VkStructureType                        sType;
			nullptr,                                                // const void*                            pNext;
			0,                                                      // VkDescriptorSetLayoutCreateFlags       flags;
			uint32_t( bindings.size() ),                            // uint32_t                               bindingCount;
			bindings.data()                                         // const VkDescriptorSetLayoutBinding*    pBindings;
		};

		dsl = std::shared_ptr<DescriptorSetLayoutInfo>( new DescriptorSetLayoutInfo{ std::move( setLayout_ ), nullptr }, 
			[device = mSettings.device]( DescriptorSetLayoutInfo* lhs ){
			vkDestroyDescriptorSetLayout(device, lhs->vkDescriptorSetLayout, nullptr );
		} );
		
		vkCreateDescriptorSetLayout( mSettings.device, &createInfo, nullptr, &dsl->vkDescriptorSetLayout);
	}

	// CONSIDER: store dsl back into shader, so that we can track use count for 
	// DescriptorSetLayout
	ofLog() << "DescriptorSetLayout " << std::hex << setLayout_.key << " | Use Count: " << dsl.use_count();
	
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

	// Group descriptors by type over all unique DescriptorSetLayouts
	std::map<VkDescriptorType, uint32_t> poolCounts; // size of pool necessary for each descriptor type
	for ( const auto &u : mDescriptorSetLayouts ){

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

	uint32_t setCount = uint32_t( mDescriptorSetLayouts.size() );	 // number of unique descriptorSets


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
		mDescriptorPool.resize( mSettings.numSwapchainImages );
		for ( size_t i = 0; i != mSettings.numSwapchainImages; ++i ){
			auto err = vkCreateDescriptorPool( mSettings.device, &descriptorPoolInfo, nullptr, &mDescriptorPool[i] );
			assert( !err );
		}
	}
}


// ----------------------------------------------------------------------

void of::vk::Context::begin(size_t frame_){
	mSwapIdx = int(frame_);
	mAlloc->free(frame_);

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
	{
		// !TODO: resettting the pipeline state needs to be more 
		// easy. Also, setting any render state that may affect
		// the pipeline should invalidate the pipeline. 
		mCurrentGraphicsPipelineState.reset();
		mCurrentShader = mShaders.front();

		mCurrentGraphicsPipelineState.setShader(mCurrentShader);
		mCurrentGraphicsPipelineState.setRenderPass(mSettings.renderPass);
	}

	mDSS_layoutKey.clear();
	mDSS_dirty.clear();
	mDSS_set.clear();

}

// ----------------------------------------------------------------------

void of::vk::Context::end(){
	mSwapIdx = -1;
}

// ----------------------------------------------------------------------

void of::vk::Context::reset(){
	mAlloc->reset();
	
	// Destroy all descriptors by destroying the pools they were
	// allocated from.
	for ( auto & p : mDescriptorPool ){
		vkDestroyDescriptorPool( mSettings.device, p, 0 );
		p = nullptr;
	}
	mDescriptorPool.clear();

	// Destory descriptorsetLayouts
	mDescriptorSetLayouts.clear();

	for ( auto &p : mVkPipelines ){
		if ( nullptr != p.second ){
			vkDestroyPipeline( mSettings.device, p.second, nullptr );
			p.second = nullptr;
		}
	}
	mVkPipelines.clear();

	vkDestroyPipelineCache( mSettings.device, mPipelineCache, nullptr );

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

void of::vk::Context::bindDescriptorSets( const VkCommandBuffer & cmd ){
	
	// Update mDSS_set -- DSS == "is descriptor set state"
	updateDescriptorSetState();
	const auto & boundDescriptorSets = mDSS_set;

	// we build dynamic offsets by going over each of the currently bound descriptorSets in 
	// currentlyBoundDescriptorsets, and for each dynamic binding within these sets, we add an offset to the list.
	// we must guarantee that dynamicOffsets has the same number of elements as currentlBoundDescriptorSets has descriptors
	// the number of descriptors is calculated by summing up all descriptorCounts per binding per descriptorSet

	const auto & dynamicOffsets = getDynamicUniformBufferOffsets();

	// Bind uniforms (the first set contains the matrices)
	vkCmdBindDescriptorSets(
		cmd,
		VK_PIPELINE_BIND_POINT_GRAPHICS,                  // use graphics, not compute pipeline
		*mCurrentShader->getPipelineLayout(),             // VkPipelineLayout object used to program the bindings.
		0, 						                          // firstset: first set index (of the above) to bind to - mDescriptorSet[0] will be bound to pipeline layout [firstset]
		uint32_t( boundDescriptorSets.size() ),  // setCount: how many sets to bind
		boundDescriptorSets.data(),              // the descriptor sets to match up with our mPipelineLayout (need to be compatible)
		uint32_t( dynamicOffsets.size() ),                // dynamic offsets count how many dynamic offsets
		dynamicOffsets.data()                             // dynamic offsets for each
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
					layouts.push_back( mDescriptorSetLayouts[mDSS_layoutKey[j]]->vkDescriptorSetLayout );
				}

				VkDescriptorSetAllocateInfo allocInfo{
					VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,	 // VkStructureType                 sType;
					nullptr,	                                     // const void*                     pNext;
					mDescriptorPool[mSwapIdx],                       // VkDescriptorPool                descriptorPool;
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
		const auto& bindings = mDescriptorSetLayouts[mDSS_layoutKey[j]]->setLayout.bindings;
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
	// Otherwise get a pipeline for current pipeline state.
	// If the pipeline has not been previously seen, it needs to be 
	// compiled at this point. This can be very costly.
	
	if ( !mCurrentGraphicsPipelineState.mDirty ){
		return;
	} else {
		
		const std::vector<uint64_t>& layouts = mCurrentGraphicsPipelineState.getShader()->getSetLayoutKeys();

		uint64_t pipelineHash = mCurrentGraphicsPipelineState.calculateHash(); 

		auto p = mVkPipelines.find( pipelineHash );
		if ( p == mVkPipelines.end() ){
			ofLog() << "Creating pipeline " << pipelineHash;
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

