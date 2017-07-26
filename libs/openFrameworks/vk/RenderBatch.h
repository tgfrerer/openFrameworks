#pragma once

#include <memory>
#include "ofLog.h"
#include "vk/Pipeline.h"
#include "vk/Allocator.h"
#include "vk/DrawCommand.h"
#include "vk/Context.h"

namespace of {
namespace vk{

// ------------------------------------------------------------

class RenderBatch
{
public:
	struct Settings
	{
		Context *                     context = nullptr;
		::vk::RenderPass              renderPass;
		std::vector<::vk::ImageView>  framebufferAttachments;
		uint32_t                      framebufferAttachmentsWidth  = 0;
		uint32_t                      framebufferAttachmentsHeight = 0;
		::vk::Rect2D                  renderArea {};
		std::vector<::vk::ClearValue> clearValues; // clear values for each attachment

		Settings& setContext( Context* ctx ){
			context = ctx;
			return *this;
		}
		Settings& setRenderPass( const ::vk::RenderPass & renderPass_ ){
			renderPass = renderPass_;
			return *this;
		}
		Settings& setFramebufferAttachments( const std::vector<::vk::ImageView>& framebufferAttachments_ ){
			framebufferAttachments = framebufferAttachments_;
			return *this;
		}
		Settings& setFramebufferAttachmentsWidth( uint32_t width_ ){
			framebufferAttachmentsWidth = width_;
			return *this;
		}
		Settings& setFramebufferAttachmentsHeight( uint32_t height_ ){
			framebufferAttachmentsHeight = height_;
			return *this;
		}
		Settings& setFramebufferAttachmentsExtent( uint32_t width_, uint32_t height_ ){
			framebufferAttachmentsWidth  = width_;
			framebufferAttachmentsHeight = height_;
			return *this;
		}
		Settings& setRenderArea( const ::vk::Rect2D& renderArea_ ){
			renderArea = renderArea_;
			return *this;
		}
		Settings& setRenderAreaOffset( const ::vk::Offset2D & offset_ ){
			renderArea.setOffset( offset_ );
			return *this;
		}
		Settings& setRenderAreaExtent( const ::vk::Extent2D & extent_ ){
			renderArea.setExtent( extent_ );
			return *this;
		}
		Settings& setRenderAreaExtent( uint32_t width_, uint32_t height_ ){
			renderArea.setExtent( { width_, height_ } );
			return *this;
		}
		Settings& setClearValues( const std::vector<::vk::ClearValue>& clearValues_ ){
			clearValues = clearValues_;
			return *this;
		}
		Settings& addFramebufferAttachment( const ::vk::ImageView& imageView ){
			framebufferAttachments.push_back( imageView );
			return *this;
		}
		template<typename ColorT>
		Settings& addClearColorValue( const ColorT& color_ ){
			static_assert(sizeof(ColorT) == sizeof(::vk::ClearColorValue), "Color type must be compatible with VkClearColorValue");
			clearValues.emplace_back(reinterpret_cast<const ::vk::ClearColorValue&> (color_));
			return *this;
		}
		Settings& addClearDepthStencilValue( const ::vk::ClearDepthStencilValue depthStencilValue_ ){
			clearValues.emplace_back( depthStencilValue_ );
			return *this;
		}
	};

private:
	/*
	
	A Batch maps to a Primary Command buffer which begins and ends a RenderPass -
	as such, it also maps to a framebuffer.

	Batch is an object which processes draw instructions
	received through draw command objects.

	Batch's mission is to create a single command buffer from all draw commands
	it accumulates, and it aims to minimize the number of pipeline switches
	between draw calls.

	*/

	const Settings         mSettings;

	::vk::Framebuffer      mFramebuffer;

	uint32_t               mVkSubPassId = 0;
	std::list<DrawCommand> mDrawCommands;

	// vulkan command buffer mapped to this batch.
	::vk::CommandBuffer mVkCmd;

	// renderbatch must be constructed from a valid context.
	RenderBatch() = delete;

public:

	RenderBatch( RenderBatch::Settings& settings );

	~RenderBatch(){
		if ( !mDrawCommands.empty() ){
			ofLogWarning() << "Unsubmitted draw commands leftover in RenderBatch";
		}
	}

	uint32_t nextSubPass();
	
	RenderBatch & draw( const DrawCommand& dc);

	// explicit draw - parameters override DrawCommand State
	RenderBatch & draw( const DrawCommand& dc, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance );
	
	// explicit indexed draw - parameters override DrawCommand State
	RenderBatch & draw( const DrawCommand& dc, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance );

	//RenderBatch & drawIndirect( const DrawCommand& dc );
	//RenderBatch & drawIndexedIndirect( const DrawCommand& dc );
	
	// Begin command buffer, begin renderpass, 
	// and also setup default values for scissor and viewport.
	void begin();

	// End renderpass, processes draw commands added to batch, and submits batch translated into commandBuffer to 
	// context's command buffer queue.
	void end();

	// Return vulkan command buffer mapped to this batch
	// n.b. this flushes, i.e. processes all draw commands queued up until this command is called.
	::vk::CommandBuffer& getVkCommandBuffer();

	// return context associated with this batch
	Context* getContext();

private:

	void finalizeDrawCommand( of::vk::DrawCommand &dc );
	void processDrawCommands( );

};

// ----------------------------------------------------------------------

inline Context * RenderBatch::getContext(){
	return mSettings.context;
}


// ----------------------------------------------------------------------
// Inside of a renderpass, draw commands may be sorted, to minimize pipeline and binding swaps.
// so endRenderPass should be the point at which the commands are recorded into the command buffer
// If the renderpass allows re-ordering.
inline uint32_t RenderBatch::nextSubPass(){
	return ++mVkSubPassId;
}

// ----------------------------------------------------------------------

} // end namespce of::vk
} // end namespace of