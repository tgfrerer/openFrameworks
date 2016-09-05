#include "Context.h"
#include "ofVkRenderer.h"
#include "vk/vkAllocator.h"
#include "vk/Shader.h"
#include "vk/Texture.h"
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
of::vk::Context::Context( const of::vk::Context::Settings& settings_ )
	: mSettings( settings_ ){}

// ----------------------------------------------------------------------
of::vk::Context::~Context(){
	reset();
	mShaders.clear();
}

// ----------------------------------------------------------------------

void of::vk::Context::setup( ofVkRenderer* renderer_ ){

	// NOTE: some of setup is deferred to the first call to Context::begin()
	// as this is where we can be sure that all shaders have been
	// added to this context.

	of::vk::Allocator::Settings settings{};
	settings.device = mSettings.device;
	settings.renderer = renderer_;
	settings.frames = uint32_t( mSettings.numVirtualFrames );
	settings.size = ( 2UL << 24 ) * settings.frames;  // (16 MB * number of swapchain images)

	mAlloc = std::make_unique<of::vk::Allocator>( settings );
	mAlloc->setup();

	if ( mSettings.numVirtualFrames > 64 ){
		ofLogError() << "Context: Number of virtual frames must not be greater than 64. \n";
		// More than 3 virtual frames rarely make sense - 
		// And more than 64 will lead to unpredictable behaviour of mDescriptorPoolsDirty bitfield, 
		// which is used to control whether descriptorPools need to be re-created. 
		// mDescriptorPoolsDirty can only hold 64 bits.
	}

	// CONSIDER: as the pipeline cache is one of the few elements which is actually mutexed 
	// by vulkan, we could share a cache over mulitple contexts and the cache could therefore
	// be owned by the renderer wich in turn owns the contexts.  
	mPipelineCache = of::vk::createPipelineCache( mSettings.device, "ofAppPipelineCache.bin" );

}

// ----------------------------------------------------------------------

void of::vk::Context::reset(){

	// Destroy all descriptors by destroying the pools they were
	// allocated from.

	for ( auto &pools : mDescriptorPoolOverspillPools ){
		for ( auto &p : pools ){
			if ( p ){
				mSettings.device.destroyDescriptorPool( p );
				p = nullptr;
			}
		}
	}
	mDescriptorPoolOverspillPools.clear();

	for ( auto & p : mDescriptorPool ){
		if ( p ){
			mSettings.device.destroyDescriptorPool( p );
			p = nullptr;
		}
	}
	mDescriptorPool.clear();

	mDescriptorPoolsDirty = -1;
	mDescriptorPoolMaxSets = 0;

	mCurrentFrameState.initialised = false;

	mAlloc->reset();

	for ( auto &p : mVkPipelines ){
		if ( p.second ){
			mSettings.device.destroyPipeline( p.second );
			p.second = nullptr;
		}
	}

	mVkPipelines.clear();

	if ( mPipelineCache){
		mSettings.device.destroyPipelineCache( mPipelineCache );
		mPipelineCache = nullptr;
	}
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

	Frame frame;

	// iterate over all uniform bindings
	for ( const auto & b : mShaderManager->getDescriptorInfos() ){

		const auto uniformKey = b.first;
		const auto & descriptorInfo = *b.second;

		if ( descriptorInfo.type == ::vk::DescriptorType::eUniformBufferDynamic ){
			// we want the member name to be the full name, 
			// i.e. : "DefaultMatrices.ProjectionMatrix", to avoid clashes.
			UboStack uboState;
			uboState.name = descriptorInfo.name;
			uboState.struct_size = descriptorInfo.storageSize;
			uboState.state.data.resize( descriptorInfo.storageSize );

			frame.uboState[uniformKey] = std::move( uboState );
			frame.uboNames[descriptorInfo.name] = &frame.uboState[uniformKey];

			for ( const auto & member : descriptorInfo.memberRanges ){
				const auto & memberName = member.first;
				const auto & range = member.second;
				UboBindingInfo m;
				m.offset = uint32_t( range.offset );
				m.range = uint32_t( range.range );
				m.buffer = &frame.uboState[uniformKey];
				frame.mUboMembers[m.buffer->name + "." + memberName] = m;
				// Also add this ubo member to the global namespace - report if there is a namespace clash.
				auto insertionResult = frame.mUboMembers.insert( { memberName, std::move( m ) } );
				if ( insertionResult.second == false ){
					ofLogWarning() << "Shader analysis: UBO Member name is ambiguous: " << ( *insertionResult.first ).first << std::endl
						<< "More than one UBO Block reference a variable with name: " << ( *insertionResult.first ).first;
				}
			}

		} else if ( descriptorInfo.type == ::vk::DescriptorType::eCombinedImageSampler ){
			// add a dummy texture 
			frame.mUniformTextures[descriptorInfo.name] = std::make_shared<of::vk::Texture>();
			
		}// end if type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC

	}  // end for all uniform bindings

	mCurrentFrameState = std::move( frame );

}

// ----------------------------------------------------------------------

// Create a descriptor pools with the minumum number of descriptors needed 
// for each kind of descriptors.
void of::vk::Context::setupDescriptorPools(){

	mDescriptorPoolSizes   = mShaderManager->getVkDescriptorPoolSizes();
	mDescriptorPoolMaxSets = mShaderManager->getNumDescriptorSets();

	if ( !mDescriptorPool.empty() ){
		// reset any currently set descriptorpools if necessary.
		for ( auto & p : mDescriptorPool ){
			ofLogNotice() << "DescriptorPool re-initialised. Resetting.";
			vkResetDescriptorPool( mSettings.device, p, 0 );
		}
		for ( auto &poolVec : mDescriptorPoolOverspillPools ){
			for ( auto pool : poolVec ){
				vkResetDescriptorPool( mSettings.device, pool, 0 );
				vkDestroyDescriptorPool( mSettings.device, pool, nullptr );
			}
			poolVec.clear();
		}
	} else{

		// create as many slots for descriptorpools as there are swapchain images.
		mDescriptorPool.resize( mSettings.numVirtualFrames, nullptr );

		// create empty overspill pools - just in case.
		mDescriptorPoolOverspillPools.resize( mSettings.numVirtualFrames );
	}
}

// ----------------------------------------------------------------------

void of::vk::Context::resetDescriptorPool( size_t frame_ ){

	// mDescriptorPoolsDirty is a 64 bit bitflag, where the lowest bit 
	// represents virtual frame 0, and the highest bit virtual frame 64.
	// The bitflag values tell us whether a descriptorPool for the virtual
	// frame is dirty. If so, we need to destroy and re-create the 
	// decriptorPool attached to this virtual frame.

	if ( 0 == ( ( 1ULL << frame_ ) & mDescriptorPoolsDirty ) ){
		mSettings.device.resetDescriptorPool( mDescriptorPool[frame_] );
		return;
	}

	// ---------| invariant: descriptor pool for this virtual frame is dirty 

	// Delete any overspill pools

	for ( auto &pool : mDescriptorPoolOverspillPools[frame_] ){
	   // we need to delete all old pools, and consolidate them into 
	   // the main pool.
		mSettings.device.resetDescriptorPool( pool );
		mSettings.device.destroyDescriptorPool( pool );
	}

	mDescriptorPoolOverspillPools[frame_].clear();

	// Reset and destroy the main descriptor pool for this virtual frame
	// and build the main pool based on 

	if ( mDescriptorPool[frame_] ){
		mSettings.device.resetDescriptorPool( mDescriptorPool[frame_] );
		mSettings.device.destroyDescriptorPool( mDescriptorPool[frame_] );
		mDescriptorPool[frame_] = nullptr;
	}
 
	// Create pool for this context - each virtual frame has its own version of the pool.
	// All descriptors used by shaders associated to this context will come from this pool. 
	//
	// Note that this pool does not set VkDescriptorPoolCreateFlags - this means that all 
	// descriptors allocated from this pool must be freed in bulk, by resetting the 
	// descriptorPool, and cannot be individually freed.

	auto descriptorPoolInfo = ::vk::DescriptorPoolCreateInfo();

	descriptorPoolInfo
		.setMaxSets       ( mDescriptorPoolMaxSets)
		.setPoolSizeCount ( mDescriptorPoolSizes.size() )
		.setPPoolSizes    ( mDescriptorPoolSizes.data() )
		;

	mDescriptorPool[frame_] = mSettings.device.createDescriptorPool( descriptorPoolInfo );

	// mark this particular descriptorPool as not dirty
	mDescriptorPoolsDirty ^= ( 1ULL << frame_ );

}

// ----------------------------------------------------------------------

void of::vk::Context::begin( size_t frame_ ){
	mFrameIndex = int( frame_ );
	mAlloc->free( frame_ );

	// DescriptorPool and FrameState are set up based on 
	// the current collection of all DescriptorSetLayouts inside
	// the ShaderManager.

	if ( mCurrentFrameState.initialised == false ){
		// We defer setting up descriptor related operations 
		// and framestate to when its first used here,
		// because only then can we be certain that all shaders
		// used by this context have been processed.
		mShaderManager->createVkDescriptorSetLayouts();
		setupDescriptorPools();

		initialiseFrameState();
		mCurrentFrameState.initialised = true;
	}

	// make sure all shader uniforms are marked dirty when context is started fresh.
	for ( auto & uboStatePair : mCurrentFrameState.uboState ){
		auto & buffer = uboStatePair.second;
		buffer.reset();
	}

	// Reset frees all descriptors allocated from current DescriptorPool
	// if descriptorPool was not large enough, creates a new, larger DescriptorPool.
	resetDescriptorPool( frame_ );

	// Reset pipeline state
	mCurrentGraphicsPipelineState.reset();
	{
		mCurrentGraphicsPipelineState.setShader( mShaders.front() );
		mCurrentGraphicsPipelineState.setRenderPass( mSettings.defaultRenderPass );	  /* TODO: we should porbably expose this - and bind a default renderpass here */
	}

	mPipelineLayoutState = PipelineLayoutState();

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

of::vk::Context & of::vk::Context::setRenderPass( const ::vk::RenderPass & renderpass_ ){
	mCurrentGraphicsPipelineState.setRenderPass( renderpass_ );
	return *this;
}

// ----------------------------------------------------------------------

const ::vk::Buffer & of::vk::Context::getVkBuffer() const{
	return mAlloc->getBuffer();
}

// ----------------------------------------------------------------------

of::vk::Context& of::vk::Context::pushBuffer( const std::string & ubo_ ){
	auto uboWithName = mCurrentFrameState.uboNames.find( ubo_ );

	if ( uboWithName != mCurrentFrameState.uboNames.end() ){
		( uboWithName->second->push() );
	}
	return *this;
}

// ----------------------------------------------------------------------

of::vk::Context& of::vk::Context::popBuffer( const std::string & ubo_ ){
	auto uboWithName = mCurrentFrameState.uboNames.find( ubo_ );

	if ( uboWithName != mCurrentFrameState.uboNames.end() ){
		( uboWithName->second->pop() );
	}
	return *this;
}

// ----------------------------------------------------------------------

of::vk::Context& of::vk::Context::draw( const ::vk::CommandBuffer& cmd, const ofMesh & mesh_ ){

	bindPipeline( cmd );

	// allocate descriptors if pipeline set indices dirty
	// write into any newly allocated descriptors
	updateDescriptorSetState();

	// store uniforms if needed
	flushUniformBufferState();

	bindDescriptorSets( cmd );

	std::vector<::vk::DeviceSize> vertexOffsets;
	std::vector<::vk::DeviceSize> indexOffsets;

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
	auto buf = getVkBuffer();
	vector<::vk::Buffer> bufferRefs( vertexOffsets.size(), buf);

	cmd.bindVertexBuffers( 0, bufferRefs, vertexOffsets );

	if ( indexOffsets.empty() ){
		// non-indexed draw
		cmd.draw( uint32_t( mesh_.getNumVertices() ), 1, 0, 0 ); //last param was 1
	} else{
		// indexed draw
		cmd.bindIndexBuffer( bufferRefs[0], indexOffsets[0], ::vk::IndexType::eUint32 );
		cmd.drawIndexed( mesh_.getNumIndices(), 1, 0, 0, 0 ); // last param was 1
	}
	return *this;
}

// ----------------------------------------------------------------------

bool of::vk::Context::storeMesh( const ofMesh & mesh_, std::vector<VkDeviceSize>& vertexOffsets, std::vector<VkDeviceSize>& indexOffsets ){
	// CONSIDER: add option to interleave 

	uint32_t numVertices   = uint32_t( mesh_.getVertices().size()  );
	uint32_t numColors     = uint32_t( mesh_.getColors().size()    );
	uint32_t numNormals    = uint32_t( mesh_.getNormals().size()   );
	uint32_t numTexCooords = uint32_t( mesh_.getTexCoords().size() );
	uint32_t numIndices    = uint32_t( mesh_.getIndices().size()   );

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

of::vk::Context & of::vk::Context::bindTexture( std::shared_ptr<of::vk::Texture> tex, const std::string& name ){
	// look up descriptorSetLayouts which reference this texture binding,
	// and mark them as tainted.
	mCurrentFrameState.mUniformTextures[name] = tex;
	
	const auto & dirtySetKeys = mShaderManager->getTextureUsage( name );
	for ( const auto &k : dirtySetKeys ){
		// remove dirty sets from cache so they have to be re-created.
		mPipelineLayoutState.descriptorSetCache.erase( k );
	}
	
	mCurrentGraphicsPipelineState.mDirty = true;

	return *this;
}

// ----------------------------------------------------------------------

void of::vk::Context::flushUniformBufferState(){

	std::vector<uint32_t> currentOffsets;

	// Lazily store data to GPU memory for dynamic ubo bindings,
	// and update current Frame State bindingOffsets -
	// If data has not changed, return previous bindingOffsets.

	for ( const auto& bindingTable : mPipelineLayoutState.bindingState ){
		// bindingState is a vector of binding tables: each binding table
		// describes the bindings of a set. Each binding within a table 
		// is a pair of <bindingNumber, unifromHash>

		for ( const auto & binding : bindingTable ){
			const auto & uniformHash = binding.second;

			auto it = mCurrentFrameState.uboState.find( uniformHash );
			if ( it == mCurrentFrameState.uboState.end() ){
				// current binding not in uboState - could be an image sampler binding.
				continue;
			}

			// ----------| invariant: uniformHash was found in uboState

			auto & uniformBuffer = it->second;

			// only write to GPU if descriptor is dirty
			if ( uniformBuffer.state.stackId == -1 ){

				void * pDst = nullptr;

				::vk::DeviceSize numBytes = uniformBuffer.struct_size;
				::vk::DeviceSize newOffset = 0;	// device GPU memory offset for this buffer 
				auto success = mAlloc->allocate( numBytes, pDst, newOffset, mFrameIndex );
				currentOffsets.push_back( (uint32_t)newOffset ); // store offset into offsets list.
				if ( !success ){
					ofLogError() << "out of buffer memory space.";
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
				currentOffsets.push_back( uniformBuffer.state.memoryOffset );
			}
		}
	}

	mCurrentFrameState.bindingOffsets = std::move( currentOffsets );
}

// ----------------------------------------------------------------------

void of::vk::Context::bindDescriptorSets( const ::vk::CommandBuffer & cmd ){

	// Update Pipeline Layout State, i.e. which set layouts are currently bound
	const auto & boundVkDescriptorSets = mPipelineLayoutState.vkDescriptorSets;
	const auto & currentShader = mCurrentGraphicsPipelineState.getShader();

	// get dynamic offsets for currently bound descriptorsets
	// now append descriptorOffsets for this set to vector of descriptorOffsets for this layout
	const std::vector<uint32_t> &dynamicBindingOffsets = mCurrentFrameState.bindingOffsets;

	// Bind uniforms (the first set contains the matrices)

	cmd.bindDescriptorSets(
		::vk::PipelineBindPoint::eGraphics,	  // use graphics, not compute pipeline
		*currentShader->getPipelineLayout(),  // VkPipelineLayout object used to program the bindings.
		0,                                    // firstset: first set index (of the above) to bind to - mDescriptorSet[0] will be bound to pipeline layout [firstset]
		boundVkDescriptorSets.size(),         // setCount: how many sets to bind
		boundVkDescriptorSets.data(),         // the descriptor sets to match up with our mPipelineLayout (need to be compatible)
		dynamicBindingOffsets.size(),         // dynamic offsets count how many dynamic offsets
		dynamicBindingOffsets.data()          // dynamic offsets for each descriptor
	);

}

// ----------------------------------------------------------------------

void of::vk::Context::updateDescriptorSetState(){

	// Allocate & update any descriptorSets - from the first dirty
	// descriptorSet to the end of the current descriptorSet sequence.

	// Q: what to we do if a descriptor set that we already allocated
	//    is requested again?
	// A: We should re-use it - unless it contains image samplers - 
	//    as the samplers need to be allocated with the descriptor
	//    this means a descriptorSet with image sampler cannot be 
	//    re-used unless it is used with the same image sampler.

	if ( mPipelineLayoutState.dirtySetIndices.empty() ){
		// descriptorset can be re-used.
		return;
	}

	// indices for VkDescriptorSets that have been freshly allocated
	std::vector<size_t> allocatedSetIndices;
	allocatedSetIndices.reserve( mPipelineLayoutState.dirtySetIndices.size() );

	for ( const auto i : mPipelineLayoutState.dirtySetIndices ){

		const auto & descriptorSetLayoutHash = mPipelineLayoutState.setLayoutKeys[i];
		      auto & descriptorSetCache      = mPipelineLayoutState.descriptorSetCache;

		const auto it = descriptorSetCache.find( descriptorSetLayoutHash );

		if ( it != descriptorSetCache.end() ){
			// descriptor has been found in cache
			const auto   descriptorSetLayoutHash          = it->first;
			const auto & previouslyAllocatedDescriptorSet = it->second;

			mPipelineLayoutState.vkDescriptorSets[i] = previouslyAllocatedDescriptorSet;
			mPipelineLayoutState.bindingState[i]     = mPipelineLayoutState.bindingStateCache[descriptorSetLayoutHash];

		} else{

			::vk::DescriptorSetLayout layout = mShaderManager->getVkDescriptorSetLayout( descriptorSetLayoutHash );

			auto allocInfo = ::vk::DescriptorSetAllocateInfo();
			allocInfo
				.setDescriptorPool( mDescriptorPool[mFrameIndex] )
				.setDescriptorSetCount( 1 )
				.setPSetLayouts( &layout )
				;

			std::vector<::vk::DescriptorSet> descriptorSetVec;
			
			bool success = false;
			try {
				descriptorSetVec = mSettings.device.allocateDescriptorSets( allocInfo );
				success = true;
			}
			catch ( std::exception e ){
				success = false;
			};

			if ( success ){
				mPipelineLayoutState.vkDescriptorSets[i] = descriptorSetVec.front();
			} else {

				// Allocation failed. 

				ofLogNotice() << "Failed to allocate descriptors - Creating & allocating from overspill pool.";

				// To still be able to allocate, we need to create a new pool, 
				// and allocate descriptors from the new pool:

				auto &poolSizes = mShaderManager->getDescriptorPoolSizesForSetLayout( descriptorSetLayoutHash );

				auto descriptorPoolInfo = ::vk::DescriptorPoolCreateInfo();
				descriptorPoolInfo
					.setMaxSets( 1 )
					.setPoolSizeCount( poolSizes.size() )
					.setPPoolSizes( poolSizes.data())
					;
				
				::vk::DescriptorPool overspillPool = mSettings.device.createDescriptorPool(descriptorPoolInfo);

				// Store overspill pool in per-virtual-frame vector of overspill pools.
				//
				// Once this frame has finished rendered and before it is re-used, 
				// all overspill pools for the frame will get destroyed and consolidated 
				// into a main frame pool, to mimimize re-allocations.
				mDescriptorPoolOverspillPools[mFrameIndex].emplace_back( std::move( overspillPool ) );

				mDescriptorPoolsDirty = -1; // mark flags for all frames as dirty

				// mDescriptorPoolSizes needs to grow - so that future frames won't need to overspill 
				mDescriptorPoolSizes.insert( mDescriptorPoolSizes.end(), poolSizes.cbegin(), poolSizes.cend() );

				// Number of sets needs to increase, too.
				mDescriptorPoolMaxSets++;   // increase max sets 

				// Update allocation info to now point to newly created overspill pool to allocate from.
				allocInfo.descriptorPool = mDescriptorPoolOverspillPools[mFrameIndex].back();

				// Allocate the descriptorset from the newly created overspill pool
				mPipelineLayoutState.vkDescriptorSets[i] = mSettings.device.allocateDescriptorSets( allocInfo ).front();

			} 

			// store VkDescriptorSet in state cache
			descriptorSetCache[descriptorSetLayoutHash] = mPipelineLayoutState.vkDescriptorSets[i];

			// mark descriptorSet at index for write update
			allocatedSetIndices.push_back( i );
		}

	}

	// Initialise newly allocated descriptors by writing to them

	if ( false == allocatedSetIndices.empty() ){
		updateDescriptorSets( allocatedSetIndices );

		// now that the binding table for these new descriptorsetlayouts has
		// been updated in mPipelineLayoutstate.bindingState, we want
		// to copy the newly created tables into our bingingStateCache so 
		// we don't have to re-create them, if a descriptorset is re-used.
		for ( auto i : allocatedSetIndices ){
			auto descriptorSetLayoutHash = mPipelineLayoutState.setLayoutKeys[i];
			// store bindings table for this new descriptorSetLayout into bindingState cache,
			// indexed by descriptorSetLayoutHash
			mPipelineLayoutState.bindingStateCache[descriptorSetLayoutHash] = mPipelineLayoutState.bindingState[i];
		}

	}

	mPipelineLayoutState.dirtySetIndices.clear();
}

// ----------------------------------------------------------------------

// Initialise descriptors with data after they have been allocated.
void of::vk::Context::updateDescriptorSets( const std::vector<size_t>& setIndices ){

	std::vector<::vk::WriteDescriptorSet> writeDescriptorSets;
	writeDescriptorSets.reserve( setIndices.size() );

	// Temporary storage for bufferInfo objects - we use this to aggregate the data
	// for UBO bindings and keep it alive outside the loop scope so it can be submitted
	// to the API after we accumulate inside the loop.
	std::map < uint64_t, std::vector<::vk::DescriptorBufferInfo>> descriptorBufferInfoStorage;

	std::map < uint64_t, std::vector<::vk::DescriptorImageInfo >> descriptorImageInfoStorage;

	// iterate over all setLayouts (since each element corresponds to a DescriptorSet)
	for ( const auto j : setIndices ){
		const auto& key = mPipelineLayoutState.setLayoutKeys[j];
		const auto& bindings = mShaderManager->getBindings( key );
		// TODO: deal with bindings which are not uniform buffers.

		// Since within context all our uniform bindings 
		// are dynamic, we should be able to bind them all to the same buffer
		// and the same base address. When drawing, the dynamic offset should point to 
		// the correct memory location for each ubo element.

		// Note that here, you point the writeDescriptorSet to dstBinding and dstSet; 
		// if count was greater than the number of bindings in the set, 
		// the next bindings will be overwritten.


		// Reserve vector size because otherwise reallocation when pushing will invalidate pointers
		descriptorBufferInfoStorage[key].reserve( bindings.size() );

		descriptorImageInfoStorage[key].reserve( bindings.size() );

		// now, we also store the current binding state into mPipelineLayoutState,
		// so we have the two in sync.

		// clear current binding state for this descriptor set index
		mPipelineLayoutState.bindingState[j].clear();

		// Go over each binding in descriptorSetLayout
		for ( const auto &b : bindings ){
			
			const auto & bindingNumber    = b.first;
			const auto & descriptorInfo   = b.second;

			// It appears that writeDescriptorSet does not immediately consume VkDescriptorBufferInfo*
			// so we must make sure that this is around for when we need it:

			if ( ::vk::DescriptorType::eUniformBufferDynamic == descriptorInfo->type ){

				descriptorBufferInfoStorage[key].push_back( {
					mAlloc->getBuffer(),                // VkBuffer        buffer;
					0,                                  // VkDeviceSize    offset;		// we start any new binding at offset 0, as data for each descriptor will always be separately allocated and uploaded.
					descriptorInfo->storageSize         // VkDeviceSize    range;
				} );

				const auto & bufElement = descriptorBufferInfoStorage[key].back();

				// Q: Is it possible that elements of a descriptorSet are of different VkDescriptorType?
				//
				// A: Yes. This is why this method should write only one binding (== Descriptor) 
				//    at a time - as all members of a binding must share the same VkDescriptorType.

				// Create one writeDescriptorSet per binding.

				auto tmpDescriptorSet = ::vk::WriteDescriptorSet();
				tmpDescriptorSet
					.setDstSet( mPipelineLayoutState.vkDescriptorSets[j] )
					.setDstBinding( bindingNumber )
					.setDstArrayElement( 0 )
					.setDescriptorCount( descriptorInfo->count)
					.setDescriptorType( descriptorInfo->type)
					.setPImageInfo( nullptr)
					.setPBufferInfo( &bufElement )
					.setPTexelBufferView( nullptr )
					
					;


				// store writeDescriptorSet for later, so all writes happen in bulk
				writeDescriptorSets.push_back( std::move( tmpDescriptorSet ) );
			} else if ( ::vk::DescriptorType::eCombinedImageSampler == descriptorInfo->type ){

				auto & texture = mCurrentFrameState.mUniformTextures[descriptorInfo->name];

				auto descriptorImageInfo = ::vk::DescriptorImageInfo();
				descriptorImageInfo
					.setSampler( texture->getVkSampler() )
					.setImageView( texture->getVkImageView() )
					.setImageLayout( ::vk::ImageLayout::eShaderReadOnlyOptimal )
					;

				descriptorImageInfoStorage[key].emplace_back( std::move( descriptorImageInfo ) );

				const auto & imgElement = descriptorImageInfoStorage[key].back();

				auto tmpDescriptorSet = ::vk::WriteDescriptorSet();
				tmpDescriptorSet
					.setDstSet( mPipelineLayoutState.vkDescriptorSets[j] )
					.setDstBinding( bindingNumber )
					.setDstArrayElement( 0 )
					.setDescriptorCount( descriptorInfo->count )
					.setDescriptorType( descriptorInfo->type )
					.setPImageInfo( &imgElement )
					.setPBufferInfo( nullptr )
					.setPTexelBufferView( nullptr )
					;

				writeDescriptorSets.push_back( std::move( tmpDescriptorSet ) );
			}

			// store binding into our current binding state
			mPipelineLayoutState.bindingState[j].insert( { bindingNumber,descriptorInfo->hash } );
		}
	}

	mSettings.device.updateDescriptorSets( writeDescriptorSets, {} );

}

// ----------------------------------------------------------------------
void of::vk::Context::bindPipeline( const ::vk::CommandBuffer & cmd ){

	// If the current pipeline state is not dirty, no need to bind something 
	// that is already bound. Return immediately.
	//
	// Otherwise try to bind a cached pipeline for current pipeline state.
	//
	// If cache lookup fails, new pipeline needs to be compiled at this point. 
	// This can be very costly.

	if ( !mCurrentGraphicsPipelineState.mDirty ){
		return;
	} else{

		const std::vector<uint64_t>& layouts = mCurrentGraphicsPipelineState.getShader()->getSetLayoutKeys();

		uint64_t pipelineHash = mCurrentGraphicsPipelineState.calculateHash();

		auto p = mVkPipelines.find( pipelineHash );
		if ( p == mVkPipelines.end() ){
			ofLog() << "Creating pipeline " << std::hex << pipelineHash;
			mVkPipelines[pipelineHash] = mCurrentGraphicsPipelineState.createPipeline( mSettings.device, mPipelineCache );
		}
		mCurrentVkPipeline = mVkPipelines[pipelineHash];

		mPipelineLayoutState.setLayoutKeys.resize( layouts.size(), 0 );
		mPipelineLayoutState.bindingState.resize( layouts.size() );
		mPipelineLayoutState.dirtySetIndices.reserve( layouts.size() );
		mPipelineLayoutState.vkDescriptorSets.resize( layouts.size(), nullptr );

		// Note that we invalidate all sets eagerly, so that in the next step
		// descriptorsets will either get loaded from the cache or, if 
		// cache miss, allocated. 
		
		// Binding a texture will have invalidated all cached descriptorSets 
		// which reference the texture, so we are guaranteed that new 
		// descriptors are allocated for the changed texture bindings.

		for ( size_t i = 0; i != layouts.size(); ++i ){
				mPipelineLayoutState.setLayoutKeys[i] = layouts[i];
				mPipelineLayoutState.dirtySetIndices.push_back( i );
		}

		// Bind the rendering pipeline (including the shaders)

		cmd.bindPipeline( ::vk::PipelineBindPoint::eGraphics, mCurrentVkPipeline );

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

