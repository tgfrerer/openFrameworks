#include "Context.h"
#include "ofVkRenderer.h"
#include "vk/vkAllocator.h"
#include "vk/Shader.h"
#include "vk/Pipeline.h"

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

	mCurrentShader = mShaders.front();

	// get pipeline layout for each shader based on its descriptorsetlayout
	// and then store this back into the shader, as it is unique to the shader.
	// CONSIDER: should this rather happen inside of the shader?

	for ( auto &s : mShaders ){
		// Derive pipelinelayout from 
		// vector of descriptorsetlayouts
		// we don't care too much about push constants yet.
		std::vector<VkDescriptorSetLayout> layouts;
		const auto & keys = s->getSetLayoutKeys();
		layouts.reserve( keys.size );
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

	mCurrentGraphicsPipelineState.mShader = mCurrentShader;
	mCurrentGraphicsPipelineState.mRenderPass = mSettings.renderPass;
	
}
// ----------------------------------------------------------------------

//void of::vk::Context::setupPipelines(){
//
//	// pipelines should, like shaders, be part of the context
//	// so that the context can be encapsulated fully within its
//	// own thread if it wanted so.
//
//	// GraphicsPipelineState comes with sensible defaults
//	// and is able to produce pipelines based on its current state.
//	// the idea will be to have a dynamic version of this object to
//	// keep track of current context state and create new pipelines
//	// on the fly if needed, or, alternatively, create all pipeline
//	// combinatinons upfront based on a .json file which lists each
//	// state combination for required pipelines.
//	of::vk::GraphicsPipelineState defaultPSO;
//
//	// TODO: let us choose which shader we want to use with our pipeline.
//	defaultPSO.mShader           = mShaders.front();
//	defaultPSO.mRenderPass       = mSettings.renderPass;
//
//	// create pipeline layout based on vector of descriptorSetLayouts queried from mContext
//	// this is way crude, and pipeline should be inside of context, context
//	// should return the layout based on shader paramter (derive layout from shader bindings) 
//	defaultPSO.mLayout = of::vk::createPipelineLayout( 
//		mSettings.device,
//		getDescriptorSetLayoutForShader(defaultPSO.mShader)
//	);
//
//	// !TODO: we want one pipeline layout per shader
//	// and then derived pipelines depending on PSO state. 
//
//	mPipelineLayouts.emplace_back( defaultPSO.mLayout );
//
//	mPipelines.solid = defaultPSO.createPipeline( mSettings.device, mPipelineCache );
//
//	defaultPSO.mRasterizationState.polygonMode = VK_POLYGON_MODE_LINE;
//
//	mPipelines.wireframe = defaultPSO.createPipeline( mSettings.device, mPipelineCache );
//}
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
void of::vk::Context::setupDescriptorPool()
{   
	// To know how many descriptors of each type to allocate, 
	// we group descriptors over all layouts by type and count each group.

	// Group descriptors by type over all unique DescriptorSetLayouts
	std::map<VkDescriptorType, uint32_t> poolCounts; // size of pool necessary for each descriptor type
	for ( const auto &u : mDescriptorSetLayouts ){

		for ( const auto & bindingInfo : u.second->setLayout.bindings){
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

	uint32_t setCount = uint32_t(mDescriptorSetLayouts.size());	 // number of unique descriptorSets

	// free any descriptorSets allocated if descriptorPool was already initialised.
	if ( mDescriptorPool != nullptr ){
		ofLogNotice() << "DescriptorPool re-initialised. Resetting.";
		vkResetDescriptorPool( mSettings.device, mDescriptorPool, 0 );
		mDescriptorPool = nullptr;
		// TODO: if you had allocated descriptorsets from this pool, 
		// they just have all been deleted through this reset.
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

	auto err = vkCreateDescriptorPool( mSettings.device, &descriptorPoolInfo, nullptr, &mDescriptorPool );
	assert( !err );
}

//// ----------------------------------------------------------------------
//
//void of::vk::Context::allocateDescriptorSets(){
//
//	// TODO: this needs to happen dynamically - we only allocate descriptor sets when 
//	// we don't already have one for the current layout and pipeline state
//	// pipeline state includes texture bindings.
//
//	// descriptorset id depends on descriptorSetLayout and texture binding state.
//
//
//
//	for ( const auto &layouts : mDescriptorSetLayouts ){
//		const auto & key = layouts.first;
//		const auto & layout = layouts.second->vkDescriptorSetLayout;
//
//		// now allocate descriptorSet to back this layout up
//		auto & newDescriptorSet = descriptorSets_[key] = VkDescriptorSet();
//
//		VkDescriptorSetAllocateInfo allocInfo{
//			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,	                  // VkStructureType                 sType;
//			nullptr,	                                                      // const void*                     pNext;
//			mDescriptorPool,                                                  // VkDescriptorPool                descriptorPool;
//			1,                                                                // uint32_t                        descriptorSetCount;
//			&layout                                                           // const VkDescriptorSetLayout*    pSetLayouts;
//		};
//		auto err = vkAllocateDescriptorSets( mSettings.device, &allocInfo, &newDescriptorSet );
//		assert( !err );
//	}
//}
//
//
//// ----------------------------------------------------------------------
//
//void of::vk::Context::initialiseDescriptorSets( ){
//	
//	// At this point the descriptors within the set are untyped 
//	// so we have to write type information into it, 
//	// as well as binding information so the set knows how to ingest data from memory
//
//	std::vector<VkWriteDescriptorSet> writeDescriptorSets;
//	writeDescriptorSets.reserve( mDescriptorSets.size() );
//	
//	// We need to store buffer info temporarily as the VkWriteDescriptorSet needs 
//	// to point to a resource outside of the scope of the for loop it is created
//	// within.
//	std::map < uint64_t, std::vector<VkDescriptorBufferInfo>> bufferInfoStore; 
//
//	// iterate over all setLayouts (since each element corresponds to a descriptor set)
//	for (auto & layout : setLayouts_ )
//	{
//		const auto& key = layout.first;
//		const auto& layoutInfo = layout.second.bindings;
//		// TODO: deal with bindings which are not uniform buffers.
//
//		// Since within context all our uniform bindings 
//		// are dynamic, we should be able to bind them all to the same buffer
//		// and the same base address. When drawing, the dynamic offset should point to 
//		// the correct memory location for each ubo element.
//		
//		// Note that here, you point the writeDescriptorSet to dstBinding and dstSet; 
//		// if descriptorCount was greater than the number of bindings in the set, 
//		// the next bindings will be overwritten.
//
//		uint32_t descriptor_array_count = 0;
//
//		// We need to get the number of descriptors by accumulating the descriptorCount
//		// over each layoutBinding
//
//		// Reserve vector size because otherwise reallocation when pushing will invalidate pointers
//		bufferInfoStore[key].reserve( layoutInfo.size() ); 
//		
//		// Go over each binding in descriptorSetLayout
//		for ( const auto &bindingInfo : layoutInfo ){
//			// how many array elements in this binding?
//			descriptor_array_count = bindingInfo.binding.descriptorCount;
//			
//			// It appears that writeDescriptorSet does not immediately consume VkDescriptorBufferInfo*
//			// so we must make sure that this is around for when we need it:
//
//			bufferInfoStore[key].push_back( {
//				mAlloc->getBuffer(),                // VkBuffer        buffer;
//				0,                                  // VkDeviceSize    offset;		// we start any new binding at offset 0, as data for each descriptor will always be separately allocated and uploaded.
//				bindingInfo.size                    // VkDeviceSize    range;
//			} );
//			
//			const auto & bufElement = bufferInfoStore[key].back();
//
//			// Q: Is it possible that elements of a descriptorSet are of different VkDescriptorType?
//			//
//			// A: Yes. This is why this method should write only one binding (== Descriptor) 
//			//    at a time - as all members of a binding must share the same VkDescriptorType.
//			
//			auto descriptorType = bindingInfo.binding.descriptorType;
//			auto dstBinding     = bindingInfo.binding.binding;
//
//			// Create one writeDescriptorSet per binding.
//
//			VkWriteDescriptorSet tmpDescriptorSet{
//				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,                    // VkStructureType                  sType;
//				nullptr,                                                   // const void*                      pNext;
//				mDescriptorSets[key],                                      // VkDescriptorSet                  dstSet;
//				dstBinding,                                                // uint32_t                         dstBinding;
//				0,                                                         // uint32_t                         dstArrayElement; // starting element in array
//				descriptor_array_count,                                    // uint32_t                         descriptorCount;
//				descriptorType,                                            // VkDescriptorType                 descriptorType;
//				nullptr,                                                   // const VkDescriptorImageInfo*     pImageInfo;
//				&bufElement,                                               // const VkDescriptorBufferInfo*    pBufferInfo;
//				nullptr,                                                   // const VkBufferView*              pTexelBufferView;
//			};
//
//			writeDescriptorSets.push_back( std::move( tmpDescriptorSet ) );
//			
//		}
//		
//	}
//
//	vkUpdateDescriptorSets( mSettings.device, uint32_t(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, NULL );
//}


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
		mDescriptorSetLayouts.clear();
	}
	

	vkDestroyPipelineCache( mSettings.device, mPipelineCache, nullptr );

	//vkDestroyPipeline( mSettings.device, mPipelines.solid, nullptr );
	//vkDestroyPipeline( mSettings.device, mPipelines.wireframe, nullptr );

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
	// as context knows which shader/pipeline is currently bound the context knows which
	// descriptorsets are currently required.
	// 
	// !TODO: getBoundDescriptorSets needs implementing 
	vector<VkDescriptorSet> currentlyBoundDescriptorSets = getBoundDescriptorSets();

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
		uint32_t( currentlyBoundDescriptorSets.size() ),  // setCount: how many sets to bind
		currentlyBoundDescriptorSets.data(),              // the descriptor sets to match up with our mPipelineLayout (need to be compatible)
		uint32_t( dynamicOffsets.size() ),                // dynamic offsets count how many dynamic offsets
		dynamicOffsets.data()                             // dynamic offsets for each
	);
}

// ----------------------------------------------------------------------

std::vector<VkDescriptorSet> of::vk::Context::getBoundDescriptorSets(){

	// !TODO: this method needs to either return descriptorsets for the 
	// currently bound pipeline layout from cache, 
	// or, if the descriptorsets are dirty, allocate new descriptor sets,
	// write to them, and return the new ones.


	// THIS METHOD NEEDS TO DYNAMICALLY ALLOCATE DESCRIPTORS THROUGH 
	// DESCRITPORSETS --
	// IF DESCRIPTORS ARE ALREADY PRESENT, NO NEED TO ALLOCATE
	// THEM, SO FIRST CHECK, AND STORE DESCRIPTORSETS BY HASH

	/*
	
	When are descriptorsets dirty? If one image descriptor/binding has 
	changed, for example. But also, when the current descriptorsets
	dont match the current pipelinelayout. 
	
	You can store descriptorsets in a cache, indexed by hash.

	hash is calculated based on descriptorset binding state.
	
	*/

	return std::vector<VkDescriptorSet>();
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
		// TODO: look whether it would make sense to store the pipeline with the 
		// shader so that derivatives can be made based on shader state foremost, meaning
		// derivatives are derived from other pipelines with the same shader.
		mCurrentPipeline = mCurrentGraphicsPipelineState.createPipeline(mSettings.device, mPipelineCache);
		// Bind the rendering pipeline (including the shaders)
		vkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, mCurrentPipeline );
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

