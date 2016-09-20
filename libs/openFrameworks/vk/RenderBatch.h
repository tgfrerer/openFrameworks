#pragma once

#include <memory>
#include "ofLog.h"
#include "vk/Pipeline.h"
#include "vk/vkAllocator.h"
#include "vk/DrawCommand.h"

namespace of {

class RenderBatch;
//class DrawCommand;


// ------------------------------------------------------------

class RenderContext
{
public:
	struct Settings
	{
		of::vk::Allocator::Settings transientMemoryAllocatorSettings;
		std::shared_ptr<::vk::PipelineCache>         pipelineCache;
		::vk::Rect2D                renderArea;
	};
private:
	friend class RenderBatch;

	const Settings mSettings;
	const ::vk::Device&                         mDevice = mSettings.transientMemoryAllocatorSettings.device;

	struct VirtualFrame
	{
		::vk::CommandPool                       commandPool;
		::vk::QueryPool                         queryPool;
		::vk::Framebuffer                       frameBuffer;
		std::list<::vk::DescriptorPool>         descriptorPools;
		std::map<uint64_t, ::vk::DescriptorSet> descriptorSetCache;
		::vk::Semaphore                         semaphoreImageAcquired;
		::vk::Semaphore                         semaphoreRenderComplete;
		::vk::Fence                             fence;
	};

	// Max number of descriptors per type
	// Array index == descriptor type
	std::array<uint32_t, VK_DESCRIPTOR_TYPE_RANGE_SIZE> mDescriptorPoolSizes;

	// Number of descriptors left available for allocation from mDescriptorPool.
	// Array index == descriptor type
	std::array<uint32_t, VK_DESCRIPTOR_TYPE_RANGE_SIZE> mAvailableDescriptorCounts;

	// Max number of sets which can be allocated from the main per-frame descriptor pool
	uint32_t mDescriptorPoolMaxSets = 0;

	// Bitfield indicating whether the descriptor pool for a virtual frame is dirty 
	// Each bit represents a virtual frame index. 
	// We're not expecting more than 64 virtual frames (more than 3 seldom make sense)
	uint64_t mDescriptorPoolsDirty = 0; // -1 == all bits '1' == all dirty

	std::vector<VirtualFrame>          mVirtualFrames;
	std::unique_ptr<of::vk::Allocator> mTransientMemory;

	size_t                             mCurrentVirtualFrame = 0;
	const ::vk::Rect2D&                mRenderArea = mSettings.renderArea;
	

	// Fetch descriptor either from cache - or allocate and initialise a descriptor based on DescriptorSetData.
	const ::vk::DescriptorSet getDescriptorSet( uint64_t descriptorSetHash, size_t setId, const of::DrawCommand & drawCommand );

	// Re-consolidate descriptor pools if necessary
	void updateDescriptorPool( );

public:
	
	RenderContext( const Settings& settings );
	~RenderContext(){
			for ( auto & vf : mVirtualFrames ){
				if ( vf.commandPool ){
					mDevice.destroyCommandPool( vf.commandPool );
				}
				for ( auto & pool : vf.descriptorPools ){
					mDevice.destroyDescriptorPool( pool );
				}
				if ( vf.semaphoreImageAcquired ){
					mDevice.destroySemaphore( vf.semaphoreImageAcquired );
				}
				if ( vf.semaphoreRenderComplete ){
					mDevice.destroySemaphore( vf.semaphoreRenderComplete );
				}
				if ( vf.fence ){
					mDevice.destroyFence( vf.fence );
				}
				if ( vf.frameBuffer ){
					mDevice.destroyFramebuffer( vf.frameBuffer );
				}
			}
			mVirtualFrames.clear();
			mTransientMemory->reset();
	};

	::vk::CommandPool & getCommandPool();
	
	::vk::Fence & getFence();
	::vk::Semaphore & getImageAcquiredSemaphore();
	::vk::Semaphore & getSemaphoreRenderComplete();
	::vk::Framebuffer & getFramebuffer();
	const ::vk::Rect2D & getRenderArea() const;
	const std::unique_ptr<of::vk::Allocator> &  of::RenderContext::getAllocator();

	void setup();
	void begin();
	void swap();
	
};

// ------------------------------------------------------------

inline ::vk::Fence & of::RenderContext::getFence(){
	return mVirtualFrames.at( mCurrentVirtualFrame ).fence;
}

inline ::vk::Semaphore &  of::RenderContext::getImageAcquiredSemaphore(){
	return mVirtualFrames.at( mCurrentVirtualFrame ).semaphoreImageAcquired;
}

inline ::vk::Semaphore &  of::RenderContext::getSemaphoreRenderComplete(){
	return mVirtualFrames.at( mCurrentVirtualFrame ).semaphoreRenderComplete;
}

inline ::vk::Framebuffer &  of::RenderContext::getFramebuffer(){
	return mVirtualFrames.at( mCurrentVirtualFrame ).frameBuffer;
}

inline const ::vk::Rect2D &  of::RenderContext::getRenderArea() const{
	return mRenderArea;
}

inline const std::unique_ptr<of::vk::Allocator> & of::RenderContext::getAllocator() {
	return mTransientMemory;
}


// ------------------------------------------------------------

class RenderBatch
{
	/*

	Batch is an object which processes draw instructions
	received through draw command objects.

	Batch's mission is to create a command buffer where
	the number of pipeline changes is minimal.

	*/
	RenderContext * mRenderContext;

	RenderBatch() = delete;

public:

	RenderBatch( RenderContext& rpc );

	~RenderBatch(){
		// todo: check if batch was submitted already - if not, submit.
		// submit();
	}

private:
	
	// current draw state
	std::unique_ptr<of::vk::GraphicsPipelineState>                  mCurrentPipelineState;
	std::unordered_map<uint64_t,std::shared_ptr<::vk::Pipeline>>    mPipelineCache;

	uint32_t            mVkSubPassId = 0;
	::vk::CommandBuffer mVkCmd;

	::vk::RenderPass    mVkRenderPass;  // current renderpass
	
	std::list<of::DrawCommand> mDrawCommands;

	void processDrawCommands();


	void beginRenderPass( const ::vk::RenderPass& vkRenderPass_, const ::vk::Framebuffer& vkFramebuffer_, const ::vk::Rect2D& renderArea_ );
	void endRenderPass();
	void beginCommandBuffer();
	void endCommandBuffer();

public:


	void submit();

	uint32_t nextSubPass();

	void draw( const of::DrawCommand& dc );
};

// ----------------------------------------------------------------------




inline void of::RenderBatch::beginRenderPass(const ::vk::RenderPass& vkRenderPass_, const ::vk::Framebuffer& vkFramebuffer_, const ::vk::Rect2D& renderArea_){
	
	ofLog() << "begin renderpass";
	
	mVkSubPassId = 0;

	if ( mVkRenderPass ){
		ofLogError() << "cannot begin renderpass whilst renderpass already open.";
		return;
	}

	mVkRenderPass = vkRenderPass_;

	//!TODO: get correct clear values, and clear value count
	std::array<::vk::ClearValue, 2> clearValues;
	clearValues[0].setColor(  reinterpret_cast<const ::vk::ClearColorValue&>(ofFloatColor::blueSteel) );
	clearValues[1].setDepthStencil( { 1.f, 0 } );

	::vk::RenderPassBeginInfo renderPassBeginInfo;
	renderPassBeginInfo
		.setRenderPass( vkRenderPass_ )
		.setFramebuffer( vkFramebuffer_ )
		.setRenderArea( renderArea_ )
		.setClearValueCount( clearValues.size() )
		.setPClearValues( clearValues.data() )
		;

	mVkCmd.beginRenderPass( renderPassBeginInfo, ::vk::SubpassContents::eInline );
}

// ----------------------------------------------------------------------
// Inside of a renderpass, draw commands may be sorted, to minimize pipeline and binding swaps.
// so endRenderPass should be the point at which the commands are recorded into the command buffer
// If the renderpass allows re-ordering.
inline uint32_t RenderBatch::nextSubPass(){
	return ++mVkSubPassId;
}

// ----------------------------------------------------------------------

inline void of::RenderBatch::endRenderPass(){
	// TODO: consolidate/re-order draw commands if buffered
	ofLog() << "end   renderpass";
	mVkCmd.endRenderPass();
}

// ----------------------------------------------------------------------

inline void of::RenderBatch::beginCommandBuffer(){
	ofLog() << "begin command buffer";
	

	if ( !mVkCmd ){
		::vk::CommandBufferAllocateInfo commandBufferAllocateInfo;
		commandBufferAllocateInfo
			.setCommandPool( mRenderContext->getCommandPool() )
			.setLevel( ::vk::CommandBufferLevel::ePrimary )
			.setCommandBufferCount( 1 )
			;
			mVkCmd = (mRenderContext->mDevice.allocateCommandBuffers( commandBufferAllocateInfo )).front();
	}

	mVkCmd.begin( { ::vk::CommandBufferUsageFlagBits::eOneTimeSubmit } );

}

// ----------------------------------------------------------------------

inline void of::RenderBatch::endCommandBuffer(){
	ofLog() << "end   command buffer";
	mVkCmd.end();
}



// ----------------------------------------------------------------------


} // end namespace of