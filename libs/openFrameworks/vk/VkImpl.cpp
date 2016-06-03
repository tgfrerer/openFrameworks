#include "ofVkRenderer.h"
#include "Pipeline.h"
#include "vulkantools.h"
#include "spirv_cross.hpp"
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
	prepareVertices();

	mContext = make_shared < of::vk::Context >();

	mContext->mRenderer = this;

	mContext->setup();

	// Setup layout of descriptors used in this example
	// Basically connects the different shader stages to descriptors
	// for binding uniform buffers, image samplers, etc.
	// So every shader binding should map to one descriptor set layout
	// binding
	setupDescriptorSetLayout();

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
	preparePipelines();

	// create a descriptor pool from which descriptor sets can be allocated
	setupDescriptorPool();

	// Update descriptor sets determining the shader binding points
	// For every binding point used in a shader there needs to be one
	// descriptor set matching that binding point
	setupDescriptorSet();

}

// ----------------------------------------------------------------------

void ofVkRenderer::endDrawCommandBuffer(){
	vkCmdEndRenderPass( *mDrawCmdBuffer );
	vkEndCommandBuffer( *mDrawCmdBuffer );
}
// ----------------------------------------------------------------------

void ofVkRenderer::beginDrawCommandBuffer(){
	VkCommandBufferBeginInfo cmdBufInfo = {};
	cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmdBufInfo.pNext = NULL;

	VkClearValue clearValues[2];
	clearValues[0].color = mDefaultClearColor;
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderPassBeginInfo = {};
	renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassBeginInfo.pNext = NULL;
	renderPassBeginInfo.renderPass = mRenderPass;
	renderPassBeginInfo.renderArea.offset.x = 0;
	renderPassBeginInfo.renderArea.offset.y = 0;
	renderPassBeginInfo.renderArea.extent.width = mWindowWidth;
	renderPassBeginInfo.renderArea.extent.height = mWindowHeight;
	renderPassBeginInfo.clearValueCount = 2;
	renderPassBeginInfo.pClearValues = clearValues;


	// we have two command buffers because each command buffer 
	// uses a different framebuffer for a target.

	auto currentFrameBufferId = mSwapchain.getCurrentBuffer();

	// Set target frame buffer
	renderPassBeginInfo.framebuffer = mFrameBuffers[currentFrameBufferId];

	vkBeginCommandBuffer( *mDrawCmdBuffer, &cmdBufInfo );


	// VK_SUBPASS_CONTENTS_INLINE means we're putting all our render commands into
	// the primary command buffer - otherwise we would have to call execute on secondary
	// command buffers to draw.
	vkCmdBeginRenderPass( *mDrawCmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE );

	// Update dynamic viewport state
	VkViewport viewport = {};
	viewport.width = (float)mViewport.width;
	viewport.height = (float)mViewport.height;
	viewport.minDepth = ( float ) 0.0f;		   // this is the min depth value for the depth buffer
	viewport.maxDepth = ( float ) 1.0f;		   // this is the dax depth value for the depth buffer  
	vkCmdSetViewport( *mDrawCmdBuffer, 0, 1, &viewport );

	// Update dynamic scissor state
	VkRect2D scissor = {};
	scissor.extent.width = mWindowWidth;
	scissor.extent.height = mWindowHeight;
	scissor.offset.x = 0;
	scissor.offset.y = 0;
	vkCmdSetScissor( *mDrawCmdBuffer, 0, 1, &scissor );

}

// ----------------------------------------------------------------------

void ofVkRenderer::setupDescriptorSet(){
	// descriptor set is allocated from pool mDescriptorPool
	// with bindings described using a descriptorSetLayout defined in mDescriptorSetLayout
	// 
	// a descriptor set has a layout, the layout tells us the number and ordering of descriptors
	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = mDescriptorPool;		// pool  : tells us where to allocate from
	allocInfo.descriptorSetCount = 1;				// count : tells us how many descriptor set layouts 
	allocInfo.pSetLayouts = &mDescriptorSetLayout;	// layout: tells us how many descriptors, and how these are laid out 
	allocInfo.pNext = VK_NULL_HANDLE;

	vkAllocateDescriptorSets( mDevice, &allocInfo, &mDescriptorSet );	// allocates mDescriptorSet

	// at this point the descriptor set is untyped 
	// so we have to write type information into it, as well as binding information

	// Update descriptor sets determining the shader binding points
	// For every binding point used in a shader there needs to be one
	// descriptor set matching that binding point
	VkWriteDescriptorSet writeDescriptorSet = {};

	// Binding 0 : Uniform buffer
	// we make it dynamic so that multiple matrix structs can be stored into this 
	// uniform buffer.
	writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writeDescriptorSet.dstSet = mDescriptorSet;		// dstSet: where to write this information into 
	writeDescriptorSet.descriptorCount = 1;
	writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	writeDescriptorSet.pBufferInfo = &mContext->getDescriptorBufferInfo();
	// Binds this uniform buffer to binding point 0	within the uniform buffer namespace
	writeDescriptorSet.dstBinding = 0;
	vkUpdateDescriptorSets( mDevice, 1, &writeDescriptorSet, 0, NULL );	 // updates mDescriptorSet by most importantly filling in the buffer info
}

// ----------------------------------------------------------------------

void ofVkRenderer::setupDescriptorSetLayout(){
	// Setup layout of descriptors used in this example
	// Basically connects the different shader stages to descriptors
	// for binding uniform buffers, image samplers, etc.
	// So every shader binding should map to one descriptor set layout
	// binding

	// Binding 0 : Uniform buffer (Vertex shader)
	VkDescriptorSetLayoutBinding layoutBinding = {};
	layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
	layoutBinding.descriptorCount = 1;
	layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
	layoutBinding.pImmutableSamplers = NULL;

	VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo = {};
	descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	descriptorSetLayoutInfo.pNext = NULL;
	descriptorSetLayoutInfo.bindingCount = 1;
	descriptorSetLayoutInfo.pBindings = &layoutBinding;

	VkResult err = vkCreateDescriptorSetLayout( mDevice, &descriptorSetLayoutInfo, NULL, &mDescriptorSetLayout );
	assert( !err );
}

// ----------------------------------------------------------------------

void ofVkRenderer::setupDescriptorPool(){
	// descriptors are allocated from a per-thread pool
	// the pool needs to reserve size based on the 
	// maximum number for each type of descriptor

	std::vector<VkDescriptorPoolSize> typeCounts = {
		{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC , 1 },
		//{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 }, // use this type of descriptors for "classic" texture samplers
	};

	// Create the global descriptor pool
	// All descriptors used in this example are allocated from this pool
	VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
	descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	descriptorPoolInfo.pNext = NULL;
	descriptorPoolInfo.poolSizeCount = typeCounts.size();
	descriptorPoolInfo.pPoolSizes = typeCounts.data();
	// Set the max. number of sets that can be requested
	// Requesting descriptors beyond maxSets will result in an error
	descriptorPoolInfo.maxSets = 1;

	VkResult vkRes = vkCreateDescriptorPool( mDevice, &descriptorPoolInfo, nullptr, &mDescriptorPool );
	assert( !vkRes );
}

// ----------------------------------------------------------------------

void ofVkRenderer::preparePipelines(){
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
	
	VkResult err;
	

	auto  loadShader = [&shaderModules = mShaderModules, &device = mDevice]( const char * fileName, VkShaderStageFlagBits stage ) -> VkPipelineShaderStageCreateInfo{
		VkPipelineShaderStageCreateInfo shaderStage = {};
		shaderStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shaderStage.stage = stage;
		shaderStage.module = vkTools::loadShader( fileName, device, stage );
		shaderStage.pName = "main"; // todo : make param
		assert( shaderStage.module != NULL );
		shaderModules.push_back( shaderStage.module );
		return shaderStage;
	};

	// Load shaders	--------

	// Shaders are loaded from the SPIR-V format, which can be generated from glsl
	VkPipelineShaderStageCreateInfo shaderStages[2] = { {},{} };
	//shaderStages[0] = loadShaderGLSL( ofToDataPath( "triangle.vert" ).c_str(), VK_SHADER_STAGE_VERTEX_BIT );
	//shaderStages[1] = loadShaderGLSL( ofToDataPath( "triangle.frag" ).c_str(), VK_SHADER_STAGE_FRAGMENT_BIT );
	shaderStages[0] = loadShader( ofToDataPath( "test.vert.spv" ).c_str(), VK_SHADER_STAGE_VERTEX_BIT );
	shaderStages[1] = loadShader( ofToDataPath( "test.frag.spv" ).c_str(), VK_SHADER_STAGE_FRAGMENT_BIT );

	{
		auto vertBuf = ofBufferFromFile( ofToDataPath( "test.vert.spv" ), true );
		int sizeNeeded = vertBuf.size() / sizeof( uint32_t );
		vector<uint32_t> shaderWords( (uint32_t*)vertBuf.getData(), (uint32_t*)vertBuf.getData() + sizeNeeded );

		spirv_cross::Compiler compiler( std::move( shaderWords ) );
		auto shaderResources = compiler.get_shader_resources();

		shaderResources.uniform_buffers.size();

		for ( auto &ubo : shaderResources.uniform_buffers ){
			ostringstream os;
			
			// returns a bitmask 
			uint64_t decorationMask = compiler.get_decoration_mask( ubo.id );

			if ( ( 1ull << spv::DecorationDescriptorSet ) & decorationMask ){
				uint32_t set = compiler.get_decoration( ubo.id, spv::DecorationDescriptorSet );
				os << ", set = " << set;
			}

			if ( ( 1ull << spv::DecorationBinding ) & decorationMask ){
				uint32_t binding = compiler.get_decoration( ubo.id, spv::DecorationBinding );
				os << ", binding = " << binding;
			}
			
			ofLog() << "Uniform Block: '" << ubo.name << "'" << os.str();
		}

		// TODO: 
		// build a pipeline layout based on the reflected shader stage information

		// Create the pipeline layout that is used to generate the rendering pipelines that
		// are based on the descriptor set layout
		//
		// In a more complex scenario you would have different pipeline layouts for different
		// descriptor set layouts that could be reused
		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo {};
		pPipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pPipelineLayoutCreateInfo.pNext = NULL;
		pPipelineLayoutCreateInfo.setLayoutCount = 1;
		// note that the pipeline is not created from the descriptorSet, 
		// but from the *layout* of the descriptorSet - really, 
		// we should use above reflection to do this, to generate the descriptorSetLayout
		pPipelineLayoutCreateInfo.pSetLayouts = &mDescriptorSetLayout;
		pPipelineLayoutCreateInfo.pushConstantRangeCount = 0;
		pPipelineLayoutCreateInfo.pPushConstantRanges = VK_NULL_HANDLE;

		vkCreatePipelineLayout( mDevice, &pPipelineLayoutCreateInfo, nullptr, &mPipelineLayout );
	}

	VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};

	pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	// The layout used for this pipeline -- this needs 
	pipelineCreateInfo.layout = mPipelineLayout;
	// Renderpass this pipeline is attached to
	pipelineCreateInfo.renderPass = mRenderPass;

	// Vertex input state
	// Describes the topoloy used with this pipeline
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
	inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	// This pipeline renders vertex data as triangle lists
	inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

	// Rasterization state
	VkPipelineRasterizationStateCreateInfo rasterizationState = {};
	rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	// Solid polygon mode
	rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
	// enable backface culling
	rasterizationState.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterizationState.frontFace = VK_FRONT_FACE_CLOCKWISE;
	rasterizationState.depthClampEnable = VK_FALSE;
	rasterizationState.rasterizerDiscardEnable = VK_FALSE;
	rasterizationState.depthBiasEnable = VK_FALSE;
	rasterizationState.lineWidth = 1.f;

	// Color blend state
	// Describes blend modes and color masks
	VkPipelineColorBlendStateCreateInfo colorBlendState = {};
	colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	// One blend attachment state
	// Blending is not used in this example
	VkPipelineColorBlendAttachmentState blendAttachmentState[1] = {};
	blendAttachmentState[0].colorWriteMask = 0xf;
	blendAttachmentState[0].blendEnable = VK_FALSE;
	colorBlendState.attachmentCount = 1;
	colorBlendState.pAttachments = blendAttachmentState;

	// Viewport state
	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	// One viewport
	viewportState.viewportCount = 1;
	// One scissor rectangle
	viewportState.scissorCount = 1;

	// Enable dynamic states
	// Describes the dynamic states to be used with this pipeline
	// Dynamic states can be set even after the pipeline has been created
	// So there is no need to create new pipelines just for changing
	// a viewport's dimensions or a scissor box
	VkPipelineDynamicStateCreateInfo dynamicState = {};
	// The dynamic state properties themselves are stored in the command buffer
	std::vector<VkDynamicState> dynamicStates {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.pDynamicStates = dynamicStates.data();
	dynamicState.dynamicStateCount = dynamicStates.size();

	// Depth and stencil state
	// Describes depth and stenctil test and compare ops
	VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
	// Basic depth compare setup with depth writes and depth test enabled
	// No stencil used 
	depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencilState.depthTestEnable = VK_TRUE;
	depthStencilState.depthWriteEnable = VK_TRUE;
	depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
	depthStencilState.depthBoundsTestEnable = VK_FALSE;
	depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
	depthStencilState.back.passOp = VK_STENCIL_OP_KEEP;
	depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;
	depthStencilState.stencilTestEnable = VK_FALSE;
	depthStencilState.front = depthStencilState.back;

	// Multi sampling state
	VkPipelineMultisampleStateCreateInfo multisampleState = {};
	multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampleState.pSampleMask = NULL;
	// No multi sampling used in this example
	multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	// Assign states
	// Two shader stages: vertex, fragment
	pipelineCreateInfo.stageCount = 2;
	// Assign pipeline state create information
	pipelineCreateInfo.pVertexInputState = &mVertexInfo.vi;
	pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
	pipelineCreateInfo.pRasterizationState = &rasterizationState;
	pipelineCreateInfo.pColorBlendState = &colorBlendState;
	pipelineCreateInfo.pMultisampleState = &multisampleState;
	pipelineCreateInfo.pViewportState = &viewportState;
	pipelineCreateInfo.pDepthStencilState = &depthStencilState;
	pipelineCreateInfo.pStages = shaderStages;
	pipelineCreateInfo.renderPass = mRenderPass;
	pipelineCreateInfo.pDynamicState = &dynamicState;	// allows us to dynamically assign viewport and other states defined in dynamicState
	
	// Create rendering pipeline

	// tig: we can create the pipeline without using a pipeline cache
	// err = vkCreateGraphicsPipelines( mDevice, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &mPipelines.solid );
	//
	// if we use the cache, that means that caching is enabled "for the duration of this command" (i.e. "vkCreateGraphicsPipelines"), and 
	// pipeline creation might be quicker if the same or a simililar pipeline has been generated previously.
	// The cache can also be saved out and retrieved, which can make it faster to generate pipelines for the 
	// next run of the application.
	err = vkCreateGraphicsPipelines( mDevice, mPipelineCache, 1, &pipelineCreateInfo, nullptr, &mPipelines.solid );
	assert( !err );
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
	VkCommandPoolCreateInfo poolInfo{};
	poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	poolInfo.queueFamilyIndex = mVkGraphicsFamilyIndex;
	poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;  // tells us how persistent the commands living in this pool are going to be
	poolInfo.pNext = VK_NULL_HANDLE;
	vkCreateCommandPool( mDevice, &poolInfo, nullptr, &mCommandPool );
}

// ----------------------------------------------------------------------

void ofVkRenderer::createSetupCommandBuffer(){
	if ( mSetupCommandBuffer != VK_NULL_HANDLE ){
		vkFreeCommandBuffers( mDevice, mCommandPool, 1, &mSetupCommandBuffer );
		mSetupCommandBuffer = VK_NULL_HANDLE;
	}

	VkCommandBufferAllocateInfo info = {};
	info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	info.commandPool = mCommandPool;
	info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	info.commandBufferCount = 1;
	info.pNext = VK_NULL_HANDLE;

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
	vkTools::setImageLayout(
		mSetupCommandBuffer,
		mDepthStencil.image,
		VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
		VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );

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
		// re-allocate command buffer for drawing.
		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = mCommandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;

		allocInfo.pNext = VK_NULL_HANDLE;

		mDrawCmdBuffer = std::shared_ptr<VkCommandBuffer>( new( VkCommandBuffer ), [&dev = mDevice, &pool = mCommandPool]( auto * buf ){
			vkFreeCommandBuffers( dev, pool, 1, buf );
			delete( buf );
			buf = nullptr;
		} );
		vkAllocateCommandBuffers( mDevice, &allocInfo, mDrawCmdBuffer.get() );
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
		//beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer( mPrePresentCommandBuffer, &beginInfo );

		vkTools::setImageLayout( mPrePresentCommandBuffer,
			mSwapchain.getBuffer(mCurrentFramebufferIndex).imageRef,
			VK_IMAGE_ASPECT_COLOR_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );

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
		VK_FLAGS_NONE,
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

void ofVkRenderer::prepareVertices(){// Setups vertex and index buffers for an indexed triangle,
	// uploads them to the VRAM and sets binding points and attribute
	// descriptions to match locations inside the shaders
	struct Vertex
	{
		ofVec3f pos;
		ofVec3f col;
	};
	// first Null out mVertices memory location
	memset( &mVertexInfo, 0, sizeof( mVertexInfo ) );

	// Binding descrition: 
	// how memory is mapped to the input assembly

	// Binding description: pos
	mVertexInfo.binding.resize( 2 );
	mVertexInfo.binding[0].binding = static_cast<uint32_t>( VertexAttribLocation::Position );
	mVertexInfo.binding[0].stride = sizeof(Vertex::pos);
	mVertexInfo.binding[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	// Binding description: col 
	mVertexInfo.binding[1].binding = static_cast<uint32_t>( VertexAttribLocation::Color );
	mVertexInfo.binding[1].stride = sizeof( Vertex::col );
	mVertexInfo.binding[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

	// Attribute descriptions

	// how memory is read from the input assembly
	
	mVertexInfo.attribute.resize( 2 );
	// Location 0 : Position
	mVertexInfo.attribute[0].binding = static_cast<uint32_t>( VertexAttribLocation::Position );
	mVertexInfo.attribute[0].location = 0;
	mVertexInfo.attribute[0].format = VK_FORMAT_R32G32B32_SFLOAT;
	mVertexInfo.attribute[0].offset = 0;
	
	// Location 1 : Color
	mVertexInfo.attribute[1].binding = static_cast<uint32_t>( VertexAttribLocation::Color );
	mVertexInfo.attribute[1].location = 1;
	mVertexInfo.attribute[1].format = VK_FORMAT_R32G32B32_SFLOAT;
	mVertexInfo.attribute[1].offset = 0; // sizeof( float ) * 3 // note: do this if using interleaved vertex data.

	// define how vertices are going to be bound to pipeline
	// by specifying the pipeline vertex input state
	mVertexInfo.vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	mVertexInfo.vi.pNext = VK_NULL_HANDLE;
	mVertexInfo.vi.vertexBindingDescriptionCount   = mVertexInfo.binding.size();
	mVertexInfo.vi.pVertexBindingDescriptions      = mVertexInfo.binding.data();
	mVertexInfo.vi.vertexAttributeDescriptionCount = mVertexInfo.attribute.size();
	mVertexInfo.vi.pVertexAttributeDescriptions    = mVertexInfo.attribute.data();
}

// ----------------------------------------------------------------------

void ofVkRenderer::draw( const ofMesh & vertexData, ofPolyRenderMode renderType, bool useColors, bool useTextures, bool useNormals ) const{

	// create transitent buffers to hold 
	// + indices
	// + positions
	// + normals

	uint32_t dynamicOffsets[1] = { 0 };
	dynamicOffsets[0] = mContext->getCurrentMatrixStateOffset();

	// Bind uniforms (the first set contains the matrices)
	vkCmdBindDescriptorSets(
		*mDrawCmdBuffer,
		VK_PIPELINE_BIND_POINT_GRAPHICS, // use graphics, not compute pipeline
		mPipelineLayout, 		// which pipeline layout (contains the bindings programmed from an sequence of descriptor sets )
		0, 						// firstset: first set index (of the above) to bind to - mDescriptorSet[0] will be bound to pipeline layout [firstset]
		1, 						// setCount: how many sets to bind
		&mDescriptorSet, 		// the descriptor sets to match up with our mPipelineLayout (need to be compatible)
		1, 						// dynamic offsets count how many dynamic offsets
		dynamicOffsets 			// dynamic offsets for each 
	);


	// Bind the rendering pipeline (including the shaders)
	vkCmdBindPipeline( *mDrawCmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, mPipelines.solid );

	// Bind triangle vertices
	// todo: offsets are the offsets into the vertex data buffers used to store data for the
	// mesh - these can be handled in the same way as the offsets into the matrix uniform buffer.
	VkDeviceSize offsets[2]{ 0, 0 };

	auto tempPositions = TransientVertexBuffer::create( const_cast<ofVkRenderer*>( this ), vertexData.getVertices() );
	auto tempColors    = TransientVertexBuffer::create( const_cast<ofVkRenderer*>( this ), vertexData.getNormals() );

	VkBuffer vertexBuffers[2] = {
	  tempPositions->buf,
	  tempColors->buf,
	};
	vkCmdBindVertexBuffers( *mDrawCmdBuffer, static_cast<uint32_t>( VertexAttribLocation::Position ), 2, vertexBuffers, offsets );

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
