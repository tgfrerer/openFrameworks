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

	if (!mFrames.empty())
		mFrames.clear();
	mFrames.resize( mSettings.numSwapchainImages, ContextState() );

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

	std::map<std::string, of::vk::Shader::UniformInfo> uniforms;

	for ( const auto &s : mSettings.shaders ){
		for ( const auto & sU : s->getUniforms() ){
			// check if uniform is already present in our overall map
			const auto & foundUniformIt = uniforms.find( sU.first );
			if ( foundUniformIt == uniforms.end() ){
				// not yet present - add new uniform definition
				uniforms.emplace( sU.first, sU.second );
			} else{
				
				// Uniform might be already present. 
				// We now must make sure that it is identical with 
				// what we already have.
				
				// TODO: Q: should we make an exception for stage flags when comparing uniform definitions?
				
				if ( sU.first != foundUniformIt->first 
					|| sU.second.binding.binding != foundUniformIt->second.binding.binding
					|| sU.second.binding.descriptorCount != foundUniformIt->second.binding.descriptorCount
					|| sU.second.binding.descriptorType != foundUniformIt->second.binding.descriptorType
					){
					// houston, we have a problem.
					// the uniform is not identical in detail 
					// but inhabits the same name.

					ostringstream definitions;

					definitions << "\t\t" << sU.first << "\t\t|\t\t" << foundUniformIt->first << endl <<
						sU.second.set << "|" << foundUniformIt->second.set << endl <<
						sU.second.binding.binding << "|" << foundUniformIt->second.binding.binding << endl <<
						sU.second.binding.descriptorCount << "|" << foundUniformIt->second.binding.descriptorCount << endl;

					ofLogError() << "ERR Uniform: '" << sU.first << "' has conflicting definitions:" << endl << definitions.str();
					
					// TODO: Q: Is there a possibility to recover from this? (We could prepend conflicting definitions with shader names)?
					//          A badly behaved shader should not be able to kill an application, the worst thing to happen 
					//          should be a performance penalty.
					return false;
				} else{
					// all good. uniform is re-used across shaders.
				}
			}
		}
	}

	// ----------| invariant: `uniforms` contains a map of unique uniforms

	{   // set up descriptorPool, so we have somewhere to allocate descriptors from
		
		// Group descriptors by type over all unique uniforms
		std::map<VkDescriptorType, uint32_t> poolCounts; // size of pool necessary for each descriptor type
		for ( const auto &u : uniforms ){
			auto & it = poolCounts.find( u.second.binding.descriptorType );
			if ( it == poolCounts.end() ){
				// descriptor of this type not yet found - insert new
				poolCounts.emplace( u.second.binding.descriptorType, u.second.binding.descriptorCount );
			} else{
				// descriptor of this type already found - add count 
				it->second += u.second.binding.descriptorCount;
			}
		}

		// ---------| invariant: poolCounts holds per-descriptorType count of descriptors

		std::vector<VkDescriptorPoolSize> poolSizes;
		poolSizes.reserve( poolCounts.size() );
		
		for ( auto &p : poolCounts ){
			poolSizes.push_back( { p.first, p.second } );
		}
		
		uint32_t setCount = uniforms.size(); // number of descriptorSets, assuming each uniform represents one descriptorSet
		setupDescriptorPool( setCount, poolSizes );

	}

	// Create descriptorSetLayouts
	// Then:
	// Derive descriptorSets from descriptorSetLayouts

	// first group uniforms by set ids
	// then create set layouts, one per uniform group that belongs to the same set 

	mDescriptorSetBindings.clear();

	for ( const auto & u : uniforms ){
		// group bidings by matching set
		mDescriptorSetBindings[u.second.set].push_back( u.second.binding );
	}

	// sort descriptorset bindings vectors by binding number ascending
	for ( auto & b : mDescriptorSetBindings ){
		std::sort( b.second.begin(), b.second.end(), []( const VkDescriptorSetLayoutBinding&lhs, const VkDescriptorSetLayoutBinding&rhs )->bool{return lhs.binding < rhs.binding; } );
	}

	for ( const auto &s : mDescriptorSetBindings ){

		// create descriptorSetLayouts
		VkDescriptorSetLayoutCreateInfo ci{
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,              // VkStructureType                        sType;
			nullptr,                                                          // const void*                            pNext;
			0,                                                                // VkDescriptorSetLayoutCreateFlags       flags;
			static_cast<uint32_t>( s.second.size() ),                         // uint32_t                               bindingCount;
			s.second.data()                                                   // const VkDescriptorSetLayoutBinding*    pBindings;
		};

		// add a new empty descriptorSetLayout to map, and retrieve address to this dummy element
		auto & newDescriptorSetLayout = mDescriptorSetLayouts[s.first] = VkDescriptorSetLayout();
		// create descriptorSetLayout and overwrite dummy element
		vkCreateDescriptorSetLayout( mSettings.device, &ci, nullptr, &newDescriptorSetLayout );

		// now allocate descriptorSet to back this layout up
		auto & newDescriptorSet = mDescriptorSets[s.first] = VkDescriptorSet();
		VkDescriptorSetAllocateInfo allocInfo{
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,	                  // VkStructureType                 sType;
			nullptr,	                                                      // const void*                     pNext;
			mDescriptorPool,                                                  // VkDescriptorPool                descriptorPool;
			1,                                                                // uint32_t                        descriptorSetCount;
			&newDescriptorSetLayout                                                    // const VkDescriptorSetLayout*    pSetLayouts;
		};
		vkAllocateDescriptorSets( mSettings.device, &allocInfo, &newDescriptorSet );
	}

	// initialise descriptorsets using updateDescriptorSet with writeDescriptorSets
	writeDescriptorSets();

	return true;
}

// ----------------------------------------------------------------------

void of::vk::Context::writeDescriptorSets(){
	// At this point the descriptors within the set are untyped 
	// so we have to write type information into it, 
	// as well as binding information so the set knows how to ingest data from memory

	std::vector<VkWriteDescriptorSet> writeDescriptorSets;
	writeDescriptorSets.reserve( mDescriptorSets.size() );
	// we need to store buffer info temporarily as the VkWriteDescriptorSet needs 
	// to point to a resource outside of the scope of the for loop it is created
	// within.
	std::map < uint32_t, std::vector<VkDescriptorBufferInfo>> bufferInfoStore; 

	for (auto & dsb : mDescriptorSetBindings )
	{
		// !TODO: deal with bindings which are not uniform buffers.

		// since within context all our uniform bindings
		// are dynamic, we should be able to bind them all to the same buffer
		// and the same base address. when drawing, the dynamic offset should point to 
		// the correct memory location for each ubo element.
		
		// this is a crass simplification, but if we can get away with it, the better =)

		// note that here, you point the writeDescriptorSet to dstBinding and dstSet, 
		// if descriptorCount is greater than the number of bindings in the set, 
		// the next bindings will be overwritten.

		uint32_t descriptor_count = 0;

		// we need to get the number of descriptors by accumulating the descriptorCount
		// over each layoutBinding

		for ( const auto &lb : dsb.second ){
			descriptor_count += lb.descriptorCount;
		}

		// TODO: Q: Is it possible that elements of a descriptorSet are of different types?
		//          If so, this will complicate this assignment, as this method only allows
		//          us to write elements of the same type. 
		// 
		// for now, assume all elements within a descriptorSet are of the same type as the first element
		auto descriptorType = dsb.second.front().descriptorType;

		bufferInfoStore[dsb.first] = getDescriptorBufferInfo( "" );

		// it appears that writeDescriptorSet does not immediately consume VkDescriptorBufferInfo*
		// so we must make sure that this is around for when we need it.

		VkWriteDescriptorSet tmpDescriptorSet{
			VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,                    // VkStructureType                  sType;
			nullptr,                                                   // const void*                      pNext;
			mDescriptorSets[dsb.first],                                // VkDescriptorSet                  dstSet;
			0,                                                         // uint32_t                         dstBinding;
			0,                                                         // uint32_t                         dstArrayElement; // starting element in array
			descriptor_count,                                          // uint32_t                         descriptorCount;
			descriptorType,                                            // VkDescriptorType                 descriptorType;
			nullptr,                                                   // const VkDescriptorImageInfo*     pImageInfo;
			bufferInfoStore[dsb.first].data(),                         // const VkDescriptorBufferInfo*    pBufferInfo;
			nullptr,                                                   // const VkBufferView*              pTexelBufferView;
		};
		writeDescriptorSets.push_back( std::move(tmpDescriptorSet));
	}

	vkUpdateDescriptorSets( mSettings.device, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL );
}

// ----------------------------------------------------------------------

void of::vk::Context::setupDescriptorPool( uint32_t setCount_, const std::vector<VkDescriptorPoolSize> & poolSizes_ ){
	// Create the global descriptor pool
	// All descriptors used in this example are allocated from this pool
	VkDescriptorPoolCreateInfo descriptorPoolInfo = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,                       // VkStructureType                sType;
		nullptr,                                                             // const void*                    pNext;
		0,                                                                   // VkDescriptorPoolCreateFlags    flags;
		setCount_,                                                           // uint32_t                       maxSets;
		poolSizes_.size(),                                                   // uint32_t                       poolSizeCount;
		poolSizes_.data(),                                                   // const VkDescriptorPoolSize*    pPoolSizes;
	};

	VkResult vkRes = vkCreateDescriptorPool( mSettings.device, &descriptorPoolInfo, nullptr, &mDescriptorPool );
}

// ----------------------------------------------------------------------

void of::vk::Context::begin(size_t frame_){
	mSwapIdx = frame_;
	mAlloc->free(frame_);
	mFrames[mSwapIdx].mCurrentMatrixState = {}; // reset matrix state
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
	return{ mMatrixStateBufferInfo };
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
		auto success = mAlloc->allocate( sizeof( MatrixState ), pData, f.mCurrentMatrixStateOffset, mSwapIdx );
		
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



const VkDeviceSize& of::vk::Context::getCurrentMatrixStateOffset(){
	storeCurrentMatrixState();
	return mFrames[mSwapIdx].mCurrentMatrixStateOffset;
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