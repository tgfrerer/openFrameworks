#pragma once

#include <memory>
#include "ofLog.h"
#include "vk/Pipeline.h"
#include "vk/vkAllocator.h"

namespace of {

class RenderBatch;
class DrawCommand;


// ------------------------------------------------------------

class RenderContext
{
public:
	struct Settings
	{
		of::vk::Allocator::Settings transientMemoryAllocatorSettings;
		::vk::PipelineCache         pipelineCache;
		::vk::Rect2D                renderArea;
	};
private:
	friend class RenderBatch;

	const Settings mSettings;

	const ::vk::Device&                         mDevice = mSettings.transientMemoryAllocatorSettings.device;
	const ::vk::PipelineCache&                  mGlobalPipelineCache = mSettings.pipelineCache;

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
	uint64_t mDescriptorPoolsDirty = -1; // -1 == all bits '1' == all dirty

	std::vector<VirtualFrame>          mVirtualFrames;
	std::unique_ptr<of::vk::Allocator> mTransientMemory;

	size_t                             mCurrentVirtualFrame = 0;

	const ::vk::Rect2D&                mRenderArea = mSettings.renderArea;
	

	// Fetch descriptor either from cache - or allocate and initialise a descriptor based on DescriptorSetData.
	const ::vk::DescriptorSet getDescriptorSet( uint64_t descriptorSetHash, size_t setId, const std::unique_ptr<of::DrawCommand> & drawCommand );

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
			}
			mVirtualFrames.clear();
	};

	::vk::CommandPool & getCommandPool();
	
	::vk::Fence & getFence();
	::vk::Semaphore & getImageAcquiredSemaphore();
	::vk::Semaphore & getSemaphoreRenderComplete();
	::vk::Framebuffer & getFramebuffer();
	const ::vk::Rect2D & getRenderArea() const;

	void setup();
	void begin();
	void swap();
	
};

// ------------------------------------------------------------

inline ::vk::Fence & of::RenderContext::getFence(){
	return mVirtualFrames.at( mCurrentVirtualFrame ).fence;
}

inline ::vk::Semaphore & RenderContext::getImageAcquiredSemaphore(){
	return mVirtualFrames.at( mCurrentVirtualFrame ).semaphoreImageAcquired;
}

inline ::vk::Semaphore & RenderContext::getSemaphoreRenderComplete(){
	return mVirtualFrames.at( mCurrentVirtualFrame ).semaphoreRenderComplete;
}

inline ::vk::Framebuffer & RenderContext::getFramebuffer(){
	return mVirtualFrames.at( mCurrentVirtualFrame ).frameBuffer;
}


// ------------------------------------------------------------

class CommandBufferContext
{
	friend class RenderPassContext;
	CommandBufferContext() = delete;
	of::RenderBatch * batch;
public:
	
	CommandBufferContext( of::RenderBatch & batch_ );
	~CommandBufferContext();
};

// ------------------------------------------------------------

class RenderPassContext
{
	RenderPassContext() = delete;
	of::RenderBatch * batch;

public:

	void draw( const std::unique_ptr<of::DrawCommand>& dc );;

	uint32_t nextSubpass();

	RenderPassContext( of::CommandBufferContext& cmdCtx_, const ::vk::RenderPass vkRenderPass_, const ::vk::Framebuffer vkFramebuffer_ );
	~RenderPassContext();
};


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
		submit();
	}

private:
	
	// current draw state
	std::unique_ptr<of::vk::GraphicsPipelineState>                  mCurrentPipelineState;
	std::unordered_map<uint64_t,std::shared_ptr<::vk::Pipeline>>    mPipelineCache;

	uint32_t            mVkSubPassId = 0;
	::vk::CommandBuffer mVkCmd;

	::vk::RenderPass    mVkRenderPass;  // current renderpass
	::vk::Framebuffer   mVkFramebuffer;  // current framebuffer
	

private:

	friend class of::RenderPassContext;
	friend class of::CommandBufferContext;

	void beginRenderPass( const ::vk::RenderPass vkRenderPass_, const ::vk::Framebuffer vkFramebuffer_ );
	void endRenderPass();
	void beginCommandBuffer();
	void endCommandBuffer();

	void submit();

	uint32_t nextSubPass();

	void draw( const std::unique_ptr<of::DrawCommand>& dc );
};

// ----------------------------------------------------------------------


inline void of::RenderBatch::beginRenderPass(const ::vk::RenderPass vkRenderPass_, const ::vk::Framebuffer vkFramebuffer_ ){
	
	ofLog() << "begin renderpass";
	
	mVkSubPassId = 0;

	// todo: error checking: there should not be a current renderpass

	if ( mVkRenderPass || mVkFramebuffer ){
		ofLogError() << "cannot begin renderpass whilst renderpass already open.";
		return;
	}

	mVkRenderPass = vkRenderPass_;
	mVkFramebuffer = vkFramebuffer_;
	
	

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
	// TODO: implement next subpass
	// TODO: consolidate/re-order draw commands if buffered
	return ++mVkSubPassId;
}

// ----------------------------------------------------------------------

inline void of::RenderBatch::endRenderPass(){
	// TODO: consolidate/re-order draw commands if buffered
	ofLog() << "end   renderpass";
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