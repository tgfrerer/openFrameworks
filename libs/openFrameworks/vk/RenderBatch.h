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
	};
private:
	friend class RenderBatch;

	const Settings mSettings;

	::vk::Device                       mDevice;

	struct VirtualFrame
	{
		::vk::CommandPool              commandPool;
		::vk::QueryPool                queryPool;
		::vk::DescriptorPool           descriptorPool;

		/* 
		
		we need facilities to overspill if there are not 
		enough descriptors available to allocate from our
		current pool.

		On next frame, the overspill pools get consolidated
		into the main descriptor pool for the frame, so that
		allocations can happen more freely.

		This only happens if descriptor pools are marked as dirty.
		
		*/

		// Cache for descriptor sets seen by the current virtual frame
		// Needs to be reset when descriptorPool changes.
		// lifetime of DescriptorSets controlled by descriptorPool - 
		// if DescriptorPool resets, cache resets.
		std::map<uint64_t, ::vk::DescriptorSet> descriptorSetCache;
		std::vector<::vk::DescriptorPool> overSpillPools;
	};

	// Bitfield indicating whether the descriptor pool for a virtual frame is dirty 
	// Each bit represents a virtual frame index. 
	// We're not expecting more than 64 virtual frames (more than 3 seldom make sense)
	
	uint64_t mDescriptorPoolsDirty = -1; // -1 == all bits '1' == all dirty

	std::vector<VirtualFrame>          mVirtualFrames;
	std::unique_ptr<of::vk::Allocator> mTransientMemory;
	size_t                             mCurrentVirtualFrame = 0;

	//! TODO: implement
	// Fetches descriptor either from cache - or allocates and initialises a descriptor pool based on DescriptorSetData.
	const ::vk::DescriptorSet getDescriptorSet( uint64_t descriptorSetHash, const DrawCommandInfo::DescriptorSetData& descriptorSetData );

	::vk::CommandPool& commandPool(){
		return mVirtualFrames[mCurrentVirtualFrame].commandPool;
	}

public:
	
	//!TODO: implement.
	RenderContext( const Settings& settings ){};

};

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

	RenderBatch( RenderContext& rpc )
	:mRenderContext(&rpc){
	}

private:
	

	// current draw state
	std::unique_ptr<of::vk::GraphicsPipelineState> currentPipelineState;
	std::shared_ptr<::vk::Pipeline>                mCurrentPipeline;

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

	::vk::RenderPassBeginInfo renderPassBeginInfo;
	renderPassBeginInfo
		.setRenderPass( mVkRenderPass )
		.setFramebuffer( mVkFramebuffer )
		//!TODO .setRenderArea( {} )
		.setClearValueCount( 0 )
		.setPClearValues( nullptr )
		;

	mVkCmd.beginRenderPass(renderPassBeginInfo,::vk::SubpassContents::eInline);
	//!TODO : begin render pass
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
	//!TODO : begin command buffer
	
	::vk::CommandBufferAllocateInfo commandBufferAllocateInfo;
	commandBufferAllocateInfo
		.setCommandPool( mRenderContext->commandPool())
		.setLevel( ::vk::CommandBufferLevel::ePrimary )
		.setCommandBufferCount( 1 )
		;

	mVkCmd = (mRenderContext->mDevice.allocateCommandBuffers( commandBufferAllocateInfo )).front();
	mVkCmd.begin( { ::vk::CommandBufferUsageFlagBits::eOneTimeSubmit } );

}

// ----------------------------------------------------------------------

inline void of::RenderBatch::endCommandBuffer(){
	ofLog() << "end   command buffer";
}

// ----------------------------------------------------------------------


} // end namespace of