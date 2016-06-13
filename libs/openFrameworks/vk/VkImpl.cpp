#include "vk/ofVkRenderer.h"
#include "vk/Pipeline.h"
#include "vk/Shader.h"
#include "vk/vkUtils.h"

#include "spirv_cross.hpp"
#include <algorithm>

// ----------------------------------------------------------------------

void ofVkRenderer::setup(){

	// the surface has been assigned by glfwwindow, through glfw,
	// just before this setup() method was called.
	querySurfaceCapabilities();
	// vkprepare:
	createCommandPool();
	
	createSetupCommandBuffer();
	{
		setupSwapChain();
		createCommandBuffers();
		setupDepthStencil();
		// TODO: let's make sure that this is more explicit,
		// and that you can set up your own render passes.
		setupRenderPass();

		// here we create a pipeline cache so that we can create a pipeline from it in preparePipelines
		mPipelineCache = of::vk::createPipelineCache(mDevice,"testPipelineCache.bin");

		mViewport = { 0.f, 0.f, float( mWindowWidth ), float( mWindowHeight ) };
		setupFrameBuffer();
	}
	// submit, then free the setupCommandbuffer.
	flushSetupCommandBuffer();
	
	createSemaphores();

	mContext = make_shared<of::vk::Context>();

	mContext->mRenderer = this;

	mContext->setup();


	// shaders will let us know about descriptorSetLayouts.
	setupShaders();

	// create a descriptor pool from which descriptor sets can be allocated
	setupDescriptorPool();

	// once we know the layout for the descriptorSets, we
	// can allocate them from the pool based on the layout
	// information
	setupDescriptorSets();

	// Create our rendering pipeline used in this example
	// Vulkan uses the concept of rendering pipelines to encapsulate
	// fixed states
	// This replaces OpenGL's huge (and cumbersome) state machine
	// A pipeline is then stored and hashed on the GPU making
	// pipeline changes much faster than having to set dozens of 
	// states
	// In a real world application you'd have dozens of pipelines
	// for every shader set used in a scene
	// Note that there are a few states that are not stored with
	// the pipeline. These are called dynamic states and the 
	// pipeline only stores that they are used with this pipeline,
	// but not their states
	setupPipelines();
	
}

// ----------------------------------------------------------------------

void ofVkRenderer::setupDescriptorSets(){
	
	// descriptor sets are there to describe how uniforms are fed to a pipeline

	// descriptor set is allocated from pool mDescriptorPool
	// based on information from descriptorSetLayout which was derived from shader code reflection 
	// 
	// a descriptorSetLayout describes a descriptor set, it tells us the 
	// number and ordering of descriptors within the set.
	
	{
		std::vector<VkDescriptorSetLayout> dsl( mDescriptorSetLayouts.size() );
		for ( size_t i = 0; i != dsl.size(); ++i ){
			dsl[i] = *mDescriptorSetLayouts[i];
		}

		VkDescriptorSetAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool = mDescriptorPool;		              // pool  : tells us where to allocate from
		allocInfo.descriptorSetCount = dsl.size();  // count : tells us how many descriptor set layouts 
		allocInfo.pSetLayouts = dsl.data();         // layout: tells us how many descriptors, and how these are laid out 
		allocInfo.pNext = VK_NULL_HANDLE;

		mDescriptorSets.resize( mDescriptorSetLayouts.size() );
		vkAllocateDescriptorSets( mDevice, &allocInfo, mDescriptorSets.data() );	// allocates mDescriptorSets
	}
	
	// At this point the descriptors within the set are untyped 
	// so we have to write type information into it, 
	// as well as binding information so the set knows how to ingest data from memory
	
	// TODO: write descriptor information to all *unique* bindings over all shaders
	// make sure to re-use descriptors for shared bindings.

	// get bindings from shader
	auto bindings = mShaders[0]->getBindings();

	std::vector<VkWriteDescriptorSet> writeDescriptorSets(bindings.size());

	{
		// Careful! bufferInfo must be retrieved from somewhere... 
		// this means probably that we shouldn't write to our 
		// descriptors before we know the buffer that is going to 
		// be used with them.
		size_t i = 0;
		for ( auto &b : bindings ){
			writeDescriptorSets[i] = {
				VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,                    // VkStructureType                  sType;
				nullptr,                                                   // const void*                      pNext;
				mDescriptorSets[0],                       //<-- check      // VkDescriptorSet                  dstSet;
				b.second.layout.binding,                  //<-- check      // uint32_t                         dstBinding;
				0,                                                         // uint32_t                         dstArrayElement;
				1,                                                         // uint32_t                         descriptorCount;
				b.second.layout.descriptorType,           //<-- check      // VkDescriptorType                 descriptorType;
				nullptr,                                                   // const VkDescriptorImageInfo*     pImageInfo;
				&mContext->getDescriptorBufferInfo(),                      // const VkDescriptorBufferInfo*    pBufferInfo;
				nullptr,                                                   // const VkBufferView*              pTexelBufferView;

			};
		}
		++i;
	}

	vkUpdateDescriptorSets( mDevice, writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL );	 
}

// ----------------------------------------------------------------------

void ofVkRenderer::setupDescriptorPool(){
	// descriptors are allocated from a per-thread pool
	// the pool needs to reserve size based on the 
	// maximum number for each type of descriptor

	// list of all descriptors types and their count
	std::vector<VkDescriptorPoolSize> typeCounts;

	uint32_t maxSets = 0;

	// iterate over descriptorsetlayouts to find out what we need
	// and to populate list above
	{	
		// count all necessary descriptor of all necessary types over
		// all currently known shaders.
		std::map<VkDescriptorType, uint32_t> descriptorTypes;
		
		for ( const auto & s : mShaders ){
			for ( const auto & b : s->getBindings() ){
				if ( descriptorTypes.find( b.second.layout.descriptorType ) == descriptorTypes.end() ){
					// first of this kind
					descriptorTypes[b.second.layout.descriptorType] = 1;
				}
				else{
					++descriptorTypes[b.second.layout.descriptorType];
				}
			}
		}
			
		// accumulate total number of descriptor sets
		// TODO: find out: is this the max number of descriptor sets or the max number of descriptors?
		for ( const auto &t : descriptorTypes ){
			typeCounts.push_back( {t.first, t.second} );
			maxSets += t.second;
		}

	}

	// Create the global descriptor pool
	// All descriptors used in this example are allocated from this pool
	VkDescriptorPoolCreateInfo descriptorPoolInfo = {
		VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,             // VkStructureType                sType;
		nullptr,                                                   // const void*                    pNext;
		0,                                                         // VkDescriptorPoolCreateFlags    flags;
		maxSets,                                                   // uint32_t                       maxSets;
		typeCounts.size(),                                         // uint32_t                       poolSizeCount;
		typeCounts.data(),                                         // const VkDescriptorPoolSize*    pPoolSizes;
	};

	VkResult vkRes = vkCreateDescriptorPool( mDevice, &descriptorPoolInfo, nullptr, &mDescriptorPool );
	assert( !vkRes );
}

// ----------------------------------------------------------------------

void ofVkRenderer::setupShaders(){
	// -- load shaders

	of::vk::Shader::Settings settings{
		mDevice,
		{
			{ VK_SHADER_STAGE_VERTEX_BIT  , "vert.spv" },
			{ VK_SHADER_STAGE_FRAGMENT_BIT, "frag.spv" },
		}
	};

	auto shader = std::make_shared<of::vk::Shader>( settings );
	mShaders.emplace_back( shader );
	auto descriptorSetLayout = shader->createDescriptorSetLayout();
	mDescriptorSetLayouts.emplace_back( descriptorSetLayout );

	// create temporary object which may be borrowed by createPipeline method
	std::vector<VkDescriptorSetLayout> dsl( mDescriptorSetLayouts.size() );
	// fill with elements borrowed from mDescriptorSets	
	std::transform( mDescriptorSetLayouts.begin(), mDescriptorSetLayouts.end(), dsl.begin(), 
		[]( auto & lhs )->VkDescriptorSetLayout { return *lhs; } );

	auto pl = of::vk::createPipelineLayout(mDevice, dsl );

	mPipelineLayouts.emplace_back( pl );

}

// ----------------------------------------------------------------------

void ofVkRenderer::setupPipelines(){
	// Create our rendering pipeline used in this example
	// Vulkan uses the concept of rendering pipelines to encapsulate
	// fixed states
	// This replaces OpenGL's huge (and cumbersome) state machine
	// A pipeline is then stored and hashed on the GPU making
	// pipeline changes much faster than having to set dozens of 
	// states
	// In a real world application you'd have dozens of pipelines
	// for every shader set used in a scene
	// Note that there are a few states that are not stored with
	// the pipeline. These are called dynamic states and the 
	// pipeline only stores that they are used with this pipeline,
	// but not their states
	

	// GraphicsPipelineState comes with sensible defaults
	// and is able to produce pipelines based on its current state.
	// the idea will be to a dynamic version of this object to
	// keep track of current context state and create new pipelines
	// on the fly if needed, or, alternatively, create all pipeline
	// combinatinons upfront based on a .json file which lists each
	// state combination for required pipelines.
	of::vk::GraphicsPipelineState defaultPSO;

	// TODO: let us choose which shader we want to use with our pipeline.
	defaultPSO.mShader           = mShaders[0];
	defaultPSO.mRenderPass       = mRenderPass;
	defaultPSO.mLayout           = mPipelineLayouts[0];
	
	mPipelines.solid = defaultPSO.createPipeline( mDevice, mPipelineCache );
	
}
 
// ----------------------------------------------------------------------

void ofVkRenderer::createSemaphores(){
	
	VkSemaphoreCreateInfo semaphoreCreateInfo = {};
	semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	semaphoreCreateInfo.pNext = VK_NULL_HANDLE;

	// This semaphore ensures that the image is complete
	// before starting to submit again
	VkResult err = vkCreateSemaphore( mDevice, &semaphoreCreateInfo, nullptr, &mSemaphores.presentComplete );
	assert( !err );

	// This semaphore ensures that all commands submitted
	// have been finished before submitting the image to the queue
	err = vkCreateSemaphore( mDevice, &semaphoreCreateInfo, nullptr, &mSemaphores.renderComplete );
	assert( !err );
}

// ----------------------------------------------------------------------

void ofVkRenderer::querySurfaceCapabilities(){

	// we need to find out if the current physical device supports 
	// PRESENT
	
	VkBool32 presentSupported = VK_FALSE;
	vkGetPhysicalDeviceSurfaceSupportKHR( mPhysicalDevice, mVkGraphicsFamilyIndex, mWindowSurface, &presentSupported );

	// find out which color formats are supported

	// Get list of supported surface formats
	uint32_t formatCount;
	auto err = vkGetPhysicalDeviceSurfaceFormatsKHR( mPhysicalDevice, mWindowSurface, &formatCount, NULL );

	if ( err != VK_SUCCESS || formatCount == 0 ){
		ofLogError() << "Vulkan error: No valid format was found.";
		ofExit( 1 );
	}

	std::vector<VkSurfaceFormatKHR> surfaceFormats( formatCount );
	err = vkGetPhysicalDeviceSurfaceFormatsKHR( mPhysicalDevice, mWindowSurface, &formatCount, surfaceFormats.data() );
	assert( !err );

	// If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
	// there is no preferered format, so we assume VK_FORMAT_B8G8R8A8_UNORM
	if ( ( formatCount == 1 ) && ( surfaceFormats[0].format == VK_FORMAT_UNDEFINED ) ){
		mWindowColorFormat.format = VK_FORMAT_B8G8R8A8_UNORM;
	}
	else{
		// Always select the first available color format
		// If you need a specific format (e.g. SRGB) you'd need to
		// iterate over the list of available surface format and
		// check for its presence
		mWindowColorFormat.format = surfaceFormats[0].format;
	}
	mWindowColorFormat.colorSpace = surfaceFormats[0].colorSpace;

	ofLog() << "Present supported: " << ( presentSupported ? "TRUE" : "FALSE" );
}

// ----------------------------------------------------------------------

void ofVkRenderer::createCommandPool(){
	// create a command pool
	VkCommandPoolCreateInfo poolInfo
	{
		VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,      // VkStructureType             sType;
		nullptr,                                         // const void*                 pNext;
		VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, // VkCommandPoolCreateFlags    flags;
		0,                                               // uint32_t                    queueFamilyIndex;
	};
		 
	// VkCommandPoolCreateFlags --> tells us how persistent the commands living in this pool are going to be
	vkCreateCommandPool( mDevice, &poolInfo, nullptr, &mCommandPool );
}

// ----------------------------------------------------------------------

void ofVkRenderer::createSetupCommandBuffer(){
	if ( mSetupCommandBuffer != VK_NULL_HANDLE ){
		vkFreeCommandBuffers( mDevice, mCommandPool, 1, &mSetupCommandBuffer );
		mSetupCommandBuffer = VK_NULL_HANDLE;
	}

	VkCommandBufferAllocateInfo info = {
		VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, // VkStructureType         sType;
		nullptr,                                        // const void*             pNext;
		mCommandPool,                                   // VkCommandPool           commandPool;
		VK_COMMAND_BUFFER_LEVEL_PRIMARY,                // VkCommandBufferLevel    level;
		1,                                              // uint32_t                commandBufferCount;
	};

 	// allocate one command buffer (as stated above) and store the handle to 
	// the newly allocated buffer into mSetupCommandBuffer
	vkAllocateCommandBuffers( mDevice, &info, &mSetupCommandBuffer );

	// todo : Command buffer is also started here, better put somewhere else
	// todo : Check if necessaray at all...
	VkCommandBufferBeginInfo cmdBufInfo = {};
	cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	// todo : check null handles, flags?

	auto vkRes = vkBeginCommandBuffer( mSetupCommandBuffer, &cmdBufInfo );
	assert( !vkRes );
};

// ----------------------------------------------------------------------

void ofVkRenderer::setupSwapChain(){
	mSwapchain.setup( mInstance, mDevice, mPhysicalDevice, mWindowSurface, mWindowColorFormat, mSetupCommandBuffer, mWindowWidth, mWindowHeight );
};

// ----------------------------------------------------------------------

void ofVkRenderer::createCommandBuffers(){
	VkCommandBufferAllocateInfo allocInfo;
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.commandPool = mCommandPool;
	allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocInfo.commandBufferCount = 1;
	allocInfo.pNext = VK_NULL_HANDLE;

	VkResult err = VK_SUCCESS;
	// Pre present
	err = vkAllocateCommandBuffers( mDevice, &allocInfo, &mPrePresentCommandBuffer );
	assert( !err);
	// Post present
	err = vkAllocateCommandBuffers( mDevice, &allocInfo, &mPostPresentCommandBuffer );
	assert( !err );
};

// ----------------------------------------------------------------------

bool  ofVkRenderer::getMemoryAllocationInfo( const VkMemoryRequirements& memReqs, VkFlags memProps, VkMemoryAllocateInfo& memInfo ) const
{
	memInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memInfo.pNext = NULL;

	if ( !memReqs.size ){
		memInfo.allocationSize = 0;
		memInfo.memoryTypeIndex = ~0;
		return true;
	}

	// Find an available memory type that satifies the requested properties.
	uint32_t memoryTypeIndex;
	for ( memoryTypeIndex = 0; memoryTypeIndex < mPhysicalDeviceMemoryProperties.memoryTypeCount; ++memoryTypeIndex ){
		if ( ( memReqs.memoryTypeBits & ( 1 << memoryTypeIndex ) ) &&
			( mPhysicalDeviceMemoryProperties.memoryTypes[memoryTypeIndex].propertyFlags & memProps ) == memProps ){
			break;
		}
	}
	if ( memoryTypeIndex >= mPhysicalDeviceMemoryProperties.memoryTypeCount ){
		assert( 0 && "memorytypeindex not found" );
		return false;
	}

	memInfo.allocationSize = memReqs.size;
	memInfo.memoryTypeIndex = memoryTypeIndex;

	return true;
}

// ----------------------------------------------------------------------

void ofVkRenderer::setupDepthStencil(){
	VkImageCreateInfo image = {};
	image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	image.pNext = NULL;
	image.imageType = VK_IMAGE_TYPE_2D;
	image.format = mDepthFormat;
	image.extent = { mWindowWidth, mWindowHeight, 1 };
	image.mipLevels = 1;
	image.arrayLayers = 1;
	image.samples = VK_SAMPLE_COUNT_1_BIT;
	image.tiling = VK_IMAGE_TILING_OPTIMAL;
	image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	image.flags = 0;

	VkImageViewCreateInfo depthStencilView = {};
	depthStencilView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	depthStencilView.pNext = NULL;
	depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
	depthStencilView.format = mDepthFormat;
	depthStencilView.flags = 0;
	depthStencilView.subresourceRange = {};
	depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
	depthStencilView.subresourceRange.baseMipLevel = 0;
	depthStencilView.subresourceRange.levelCount = 1;
	depthStencilView.subresourceRange.baseArrayLayer = 0;
	depthStencilView.subresourceRange.layerCount = 1;

	VkMemoryRequirements memReqs;
	VkResult err;

	err = vkCreateImage( mDevice, &image, nullptr, &mDepthStencil.image );
	assert( !err );
	vkGetImageMemoryRequirements( mDevice, mDepthStencil.image, &memReqs );

	VkMemoryAllocateInfo memInfo;
	getMemoryAllocationInfo( memReqs, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memInfo );
	
	err = vkAllocateMemory( mDevice, &memInfo, nullptr, &mDepthStencil.mem );
	assert( !err );

	err = vkBindImageMemory( mDevice, mDepthStencil.image, mDepthStencil.mem, 0 );
	assert( !err );

	auto transferBarrier = of::vk::createImageBarrier(
		mDepthStencil.image,
		VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL 
	);

	// Append pipeline barrier to current setup commandBuffer
	vkCmdPipelineBarrier(
		mSetupCommandBuffer,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &transferBarrier );

		/*VkCommandBuffer                             commandBuffer,
		VkPipelineStageFlags                        srcStageMask,
		VkPipelineStageFlags                        dstStageMask,
		VkDependencyFlags                           dependencyFlags,
		uint32_t                                    memoryBarrierCount,
		const VkMemoryBarrier*                      pMemoryBarriers,
		uint32_t                                    bufferMemoryBarrierCount,
		const VkBufferMemoryBarrier*                pBufferMemoryBarriers,
		uint32_t                                    imageMemoryBarrierCount,
		const VkImageMemoryBarrier*                 pImageMemoryBarriers*/


	depthStencilView.image = mDepthStencil.image;

	err = vkCreateImageView( mDevice, &depthStencilView, nullptr, &mDepthStencil.view );
	assert( !err );
};

// ----------------------------------------------------------------------

void ofVkRenderer::setupRenderPass(){
	VkAttachmentDescription attachments[2] = {};

	// Color attachment
	attachments[0].format = mWindowColorFormat.format;
	attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	// Depth attachment
	attachments[1].format = mDepthFormat;
	attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
	attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkAttachmentReference colorReference = {};
	colorReference.attachment = 0;
	colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	VkAttachmentReference depthReference = {};
	depthReference.attachment = 1;
	depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.flags = 0;
	subpass.inputAttachmentCount = 0;
	subpass.pInputAttachments = NULL;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorReference;
	subpass.pResolveAttachments = NULL;
	subpass.pDepthStencilAttachment = &depthReference;
	subpass.preserveAttachmentCount = 0;
	subpass.pPreserveAttachments = NULL;

	VkRenderPassCreateInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	renderPassInfo.pNext = NULL;
	renderPassInfo.attachmentCount = 2;
	renderPassInfo.pAttachments = attachments;
	renderPassInfo.subpassCount = 1;
	renderPassInfo.pSubpasses = &subpass;
	renderPassInfo.dependencyCount = 0;
	renderPassInfo.pDependencies = NULL;

	VkResult err;

	err = vkCreateRenderPass(mDevice, &renderPassInfo, nullptr, &mRenderPass);
	assert(!err);
};


// ----------------------------------------------------------------------

void ofVkRenderer::setupFrameBuffer(){

	// Create frame buffers for every swap chain frame
	mFrameBuffers.resize( mSwapchain.getImageCount() );
	for ( uint32_t i = 0; i < mFrameBuffers.size(); i++ ){
		// This is where we connect the framebuffer with the presentable image buffer
		// which is handled by the swapchain.
		// TODO: the swapchain should own this frame buffer, 
		// and allow us to reference it.
		// maybe this needs to move into the swapchain.
		
		VkImageView attachments[2];
		// attachment0 shall be the image view for the image buffer to the corresponding swapchain image view
		attachments[0] = mSwapchain.getBuffer(i).view;
		// attachment1 shall be the image view for the depthStencil buffer
		attachments[1] = mDepthStencil.view;

		VkFramebufferCreateInfo frameBufferCreateInfo = {};
		frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		frameBufferCreateInfo.pNext = NULL;
		frameBufferCreateInfo.renderPass = mRenderPass;
		frameBufferCreateInfo.attachmentCount = 2;
		frameBufferCreateInfo.pAttachments = attachments;
		frameBufferCreateInfo.width = mWindowWidth;
		frameBufferCreateInfo.height = mWindowHeight;
		frameBufferCreateInfo.layers = 1;

		// create a framebuffer for each swap chain frame
		VkResult err = vkCreateFramebuffer( mDevice, &frameBufferCreateInfo, nullptr, &mFrameBuffers[i] );
		assert( !err );
	}
};

// ----------------------------------------------------------------------

void ofVkRenderer::flushSetupCommandBuffer(){
	VkResult err;

	if ( mSetupCommandBuffer == VK_NULL_HANDLE )
		return;

	err = vkEndCommandBuffer( mSetupCommandBuffer );
	assert( !err );

	VkSubmitInfo submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &mSetupCommandBuffer;

	err = vkQueueSubmit( mQueue, 1, &submitInfo, VK_NULL_HANDLE );
	assert( !err );

	err = vkQueueWaitIdle( mQueue );
	assert( !err );

	vkFreeCommandBuffers( mDevice, mCommandPool, 1, &mSetupCommandBuffer );
	mSetupCommandBuffer = VK_NULL_HANDLE; // todo : check if still necessary
};

// ----------------------------------------------------------------------

void ofVkRenderer::beginDrawCommandBuffer(){
	VkCommandBufferBeginInfo cmdBufInfo = {};
	cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBufInfo.pNext = NULL;

	// Set target frame buffer

	vkBeginCommandBuffer( *mDrawCmdBuffer, &cmdBufInfo );

	// Update dynamic viewport state
	VkViewport viewport = {};
	viewport.width = (float)mViewport.width;
	viewport.height = (float)mViewport.height;
	viewport.minDepth = ( float ) 0.0f;		   // this is the min depth value for the depth buffer
	viewport.maxDepth = ( float ) 1.0f;		   // this is the max depth value for the depth buffer  
	vkCmdSetViewport( *mDrawCmdBuffer, 0, 1, &viewport );

	// Update dynamic scissor state
	VkRect2D scissor = {};
	scissor.extent.width = mWindowWidth;
	scissor.extent.height = mWindowHeight;
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	vkCmdSetScissor( *mDrawCmdBuffer, 0, 1, &scissor );

	beginRenderPass();
}

// ----------------------------------------------------------------------

void ofVkRenderer::endDrawCommandBuffer(){
	endRenderPass();
	vkEndCommandBuffer( *mDrawCmdBuffer );
}

// ----------------------------------------------------------------------

void ofVkRenderer::beginRenderPass(){
	VkClearValue clearValues[2];
	clearValues[0].color = mDefaultClearColor;
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRect2D renderArea{
		{ 0, 0 },								  // VkOffset2D
		{ mWindowWidth, mWindowHeight },		  // VkExtent2D
	};

	// we have two command buffers because each command buffer 
	// uses a different framebuffer for a target.
	auto currentFrameBufferId = mSwapchain.getCurrentBuffer();

	VkRenderPassBeginInfo renderPassBeginInfo = {
		VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, // VkStructureType        sType;
		nullptr,                                  // const void*            pNext;
		mRenderPass,                              // VkRenderPass           renderPass;
		mFrameBuffers[currentFrameBufferId],      // VkFramebuffer          framebuffer;
		renderArea,                               // VkRect2D               renderArea;
		2,                                        // uint32_t               clearValueCount;
		clearValues,                              // const VkClearValue*    pClearValues;
	};

	// VK_SUBPASS_CONTENTS_INLINE means we're putting all our render commands into
	// the primary command buffer - otherwise we would have to call execute on secondary
	// command buffers to draw.
	vkCmdBeginRenderPass( *mDrawCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );
};

// ----------------------------------------------------------------------

void ofVkRenderer::endRenderPass(){
	vkCmdEndRenderPass( *mDrawCmdBuffer );
};

// ----------------------------------------------------------------------

void ofVkRenderer::startRender(){
	
	// vkDeviceWaitIdle( mDevice );

	// remove any transient buffer objects from one frame ago.
	mTransientBufferObjects.clear();

	// start of new frame
	VkResult err;

	// + block cpu until swapchain can get next image, 
	// + get index for swapchain image we may render into,
	// + signal presentComplete once the image has been acquired
	err = mSwapchain.acquireNextImage( mSemaphores.presentComplete, &mCurrentFramebufferIndex );
	assert( !err );

	{

		if ( mDrawCmdBuffer ){
			// if command buffer has been previously recorded, we want to re-use it.
			vkResetCommandBuffer( *mDrawCmdBuffer, 0 );
		} else{
			mDrawCmdBuffer = std::shared_ptr<VkCommandBuffer>( new( VkCommandBuffer ), [&dev = mDevice, &pool = mCommandPool]( auto * buf ){
				vkFreeCommandBuffers( dev, pool, 1, buf );
				delete( buf );
				buf = nullptr;
			} );
			// re-allocate command buffer for drawing.
			VkCommandBufferAllocateInfo allocInfo = {};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.pNext = nullptr;
			allocInfo.commandPool = mCommandPool;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandBufferCount = 1;
			vkAllocateCommandBuffers( mDevice, &allocInfo, mDrawCmdBuffer.get() );
		}

	}
	
	beginDrawCommandBuffer();
	mContext->begin();

}

// ----------------------------------------------------------------------

void ofVkRenderer::finishRender(){
	VkResult err;
	VkSubmitInfo submitInfo = {};

	// submit current model view and projection matrices
	
	mContext->end();
	endDrawCommandBuffer();

	// Submit the draw command buffer
	//
	// The submit info structure contains a list of
	// command buffers and semaphores to be submitted to a queue
	// If you want to submit multiple command buffers, pass an array
	VkPipelineStageFlags pipelineStages = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;

	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.pWaitDstStageMask = &pipelineStages;
	submitInfo.waitSemaphoreCount = 1;
	// we have to wait until the image has been acquired - that's when this semaphore is signalled.
	submitInfo.pWaitSemaphores = &mSemaphores.presentComplete;
	// Submit the currently active command buffer
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = mDrawCmdBuffer.get();
	// The signal semaphore is used during queue presentation
	// to ensure that the image is not rendered before all
	// commands have been submitted
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &mSemaphores.renderComplete;

	// Submit to the graphics queue	- 
	err = vkQueueSubmit( mQueue, 1, &submitInfo, VK_NULL_HANDLE );
	assert( !err );

	{  // pre-present

		VkCommandBufferBeginInfo beginInfo = {};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		// beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer( mPrePresentCommandBuffer, &beginInfo );
		{
			auto transferBarrier = of::vk::createImageBarrier(	
				mSwapchain.getBuffer(mCurrentFramebufferIndex).imageRef,
				VK_IMAGE_ASPECT_COLOR_BIT,
				VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
				VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );

			// Append pipeline barrier to commandBuffer
			vkCmdPipelineBarrier(
				mPrePresentCommandBuffer,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				0,
				0, nullptr,
				0, nullptr,
				1, &transferBarrier );
		}
		vkEndCommandBuffer( mPrePresentCommandBuffer );
		// Submit to the queue
		VkSubmitInfo submitInfo = {};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &mPrePresentCommandBuffer;

		err = vkQueueSubmit( mQueue, 1, &submitInfo, VK_NULL_HANDLE );
	}

	// Present the current buffer to the swap chain
	// We pass the signal semaphore from the submit info
	// to ensure that the image is not rendered until
	// all commands have been submitted
	auto presentResult = mSwapchain.queuePresent( mQueue, mCurrentFramebufferIndex, mSemaphores.renderComplete );
	
	// Add a post present image memory barrier
	// This will transform the frame buffer color attachment back
	// to it's initial layout after it has been presented to the
	// windowing system
	// See buildCommandBuffers for the pre present barrier that 
	// does the opposite transformation 
	VkImageMemoryBarrier postPresentBarrier = {};
	postPresentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	postPresentBarrier.pNext = NULL;
	postPresentBarrier.srcAccessMask = 0;
	postPresentBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	postPresentBarrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	postPresentBarrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	postPresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	postPresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	postPresentBarrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
	postPresentBarrier.image = mSwapchain.getBuffer(mCurrentFramebufferIndex).imageRef;

	// Use dedicated command buffer from example base class for submitting the post present barrier
	VkCommandBufferBeginInfo cmdBufInfo = {};
	cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	err = vkBeginCommandBuffer( mPostPresentCommandBuffer, &cmdBufInfo );

	// Put post present barrier into command buffer
	vkCmdPipelineBarrier(
		mPostPresentCommandBuffer,
		VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
		VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		0,
		0, nullptr,
		0, nullptr,
		1, &postPresentBarrier );

	err = vkEndCommandBuffer( mPostPresentCommandBuffer );

	// Submit to the queue
	submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &mPostPresentCommandBuffer;

	err = vkQueueSubmit( mQueue, 1, &submitInfo, VK_NULL_HANDLE );

	err = vkQueueWaitIdle( mQueue );
	
	// vkDeviceWaitIdle( mDevice );

}

// ----------------------------------------------------------------------

void ofVkRenderer::draw( const ofMesh & vertexData, ofPolyRenderMode renderType, bool useColors, bool useTextures, bool useNormals ) const{

	// create transitent buffers to hold 
	// + indices
	// + positions
	// + normals

	uint32_t dynamicOffsets[1] = { 0 };
	dynamicOffsets[0] = mContext->getCurrentMatrixStateOffset();

	auto & currentShader = mShaders[0];

	vector<VkDescriptorSet>  currentlyBoundDescriptorsets = {
		mDescriptorSets[0],						 // default matrix uniforms
		                                         // if there were any other uniforms bound
	};

	// Bind uniforms (the first set contains the matrices)
	vkCmdBindDescriptorSets(
		*mDrawCmdBuffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS,     // use graphics, not compute pipeline
		*mPipelineLayouts[0],  // which pipeline layout (contains the bindings programmed from an sequence of descriptor sets )
		0, 						             // firstset: first set index (of the above) to bind to - mDescriptorSet[0] will be bound to pipeline layout [firstset]
		currentlyBoundDescriptorsets.size(), // setCount: how many sets to bind
		currentlyBoundDescriptorsets.data(), // the descriptor sets to match up with our mPipelineLayout (need to be compatible)
		1, 						             // dynamic offsets count how many dynamic offsets
		dynamicOffsets 			             // dynamic offsets for each 
	);

	// Bind the rendering pipeline (including the shaders)
	vkCmdBindPipeline( *mDrawCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelines.solid );

	// Bind triangle vertices
	// todo: offsets are the offsets into the vertex data buffers used to store data for the
	// mesh - these can be handled in the same way as the offsets into the matrix uniform buffer.
	VkDeviceSize offsets[2]{ 0, 0 };

	auto tempPositions = TransientVertexBuffer::create( const_cast<ofVkRenderer*>( this ), vertexData.getVertices() );
	auto tempColors    = TransientVertexBuffer::create( const_cast<ofVkRenderer*>( this ), vertexData.getNormals() );

	std::array<VkBuffer, 2> vertexBuffers = {
	  tempPositions->buf,
	  tempColors->buf,
	};
	vkCmdBindVertexBuffers( *mDrawCmdBuffer, 0, vertexBuffers.size(), vertexBuffers.data(), offsets );

	// This transient buffer will: 
	// + upload the vector to GPU memory.
	// + automatically get deleted on the next frame.
	auto tempIndices = TransientIndexBuffer::create( const_cast<ofVkRenderer*>( this ), vertexData.getIndices() );
	// Bind triangle indices
	vkCmdBindIndexBuffer( *mDrawCmdBuffer, tempIndices->buf, 0, VK_INDEX_TYPE_UINT32 );

	// Draw indexed triangle
	vkCmdDrawIndexed( *mDrawCmdBuffer, tempIndices->num_elements, 1, 0, 0, 1 );
}  

// ----------------------------------------------------------------------

shared_ptr<ofVkRenderer::BufferObject> ofVkRenderer::TransientVertexBuffer::create(  ofVkRenderer* renderer_, const vector<ofVec3f>& vec_ ){
	
	auto &device_ = renderer_->getVkDevice();

	auto obj = shared_ptr<TransientVertexBuffer>( new TransientVertexBuffer, [&device = device_](BufferObject* obj){
		// destructor
		
		vkFreeMemory( device, obj->mem, nullptr );
		vkDestroyBuffer( device, obj->buf, nullptr );

		delete obj;
		obj = nullptr;
	} );

	VkBufferCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	createInfo.pNext = nullptr;
	createInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	createInfo.flags = 0;
	createInfo.size = vec_.size() * sizeof( ofVec3f );

	vkCreateBuffer( device_, &createInfo, nullptr, &obj->buf );
	
	
	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements( device_, obj->buf, &memReqs );

	VkMemoryAllocateInfo allocInfo; 
	renderer_->getMemoryAllocationInfo( memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, allocInfo );

	// allocate memory
	vkAllocateMemory( device_, &allocInfo, nullptr, &obj->mem );
	
	// upload vector data to gpu
	void* data;
	vkMapMemory( device_, obj->mem, 0, allocInfo.allocationSize, 0, &data );
	memcpy( data, vec_.data(), createInfo.size );
	vkUnmapMemory( device_, obj->mem );

	// bind memory to buffer
	vkBindBufferMemory( device_, obj->buf, obj->mem, 0 );

	obj->num_elements = vec_.size();

	// push element onto list of transient buffer objects
	// these will get deleted after the next frame.
	renderer_->mTransientBufferObjects.push_back( obj );

	return obj;
}
// ----------------------------------------------------------------------

shared_ptr<ofVkRenderer::BufferObject> ofVkRenderer::TransientIndexBuffer::create(  ofVkRenderer* renderer_, const vector<uint32_t> & vec_ ){
	auto &device_ = renderer_->getVkDevice();

	auto obj = shared_ptr<TransientIndexBuffer>( new TransientIndexBuffer, [&device = device_]( TransientIndexBuffer* obj ){
		// destructor

		vkFreeMemory( device, obj->mem, nullptr );
		vkDestroyBuffer( device, obj->buf, nullptr );

		delete obj;
		obj = nullptr;
	} );

	VkBufferCreateInfo createInfo{};
	createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	createInfo.pNext = nullptr;
	createInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	createInfo.flags = 0;
	createInfo.size = vec_.size() * sizeof( uint32_t );

	vkCreateBuffer( device_, &createInfo, nullptr, &obj->buf );

	VkMemoryRequirements memReqs;
	vkGetBufferMemoryRequirements( device_, obj->buf, &memReqs );

	VkMemoryAllocateInfo allocInfo;
	renderer_->getMemoryAllocationInfo( memReqs, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, allocInfo );

	// allocate memory
	vkAllocateMemory( device_, &allocInfo, nullptr, &obj->mem );

	// upload vector data to gpu
	void* data;
	vkMapMemory( device_, obj->mem, 0, allocInfo.allocationSize, 0, &data );
	memcpy( data, vec_.data(), createInfo.size );
	vkUnmapMemory( device_, obj->mem );
	
	// bind memory to buffer
	vkBindBufferMemory( device_, obj->buf, obj->mem, 0 );
	obj->num_elements = vec_.size();

	// push element onto list of transient buffer objects
	// these will get deleted after the next frame.
	renderer_->mTransientBufferObjects.push_back( obj );

	return obj;
}
// ----------------------------------------------------------------------
