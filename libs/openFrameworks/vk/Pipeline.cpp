#include "vk/Pipeline.h"
#include "vk/Shader.h"
#include "spooky/SpookyV2.h"
#include <array>

using namespace of;


// ----------------------------------------------------------------------

void vk::GraphicsPipelineState::setup(){
	reset();
}

// ----------------------------------------------------------------------

void vk::GraphicsPipelineState::reset()
{
	mInputAssemblyState = {
		VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, // VkStructureType                            sType;
		nullptr,                                                     // const void*                                pNext;
		0,                                                           // VkPipelineInputAssemblyStateCreateFlags    flags;
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,                         // VkPrimitiveTopology                        topology;
		VK_FALSE,                                                    // VkBool32                                   primitiveRestartEnable;
	};

	mTessellationState = {
		VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,	 // VkStructureType                            sType;
		nullptr,                                                     // const void*                                pNext;
		0,                                                           // VkPipelineTessellationStateCreateFlags     flags;
		0,                                                           // uint32_t                                   patchControlPoints;
	};

	mViewportState = {
		VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,       // VkStructureType                            sType;
		nullptr,                                                     // const void*                                pNext;
		0,                                                           // VkPipelineViewportStateCreateFlags         flags;
		1,                                                           // uint32_t                                   viewportCount;
		nullptr,                                                     // const VkViewport*                          pViewports;
		1,                                                           // uint32_t                                   scissorCount;
		nullptr,                                                     // const VkRect2D*                            pScissors;
	};

	mRasterizationState = {
		VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,  // VkStructureType                            sType;
		nullptr,                                                     // const void*                                pNext;
		0,                                                           // VkPipelineRasterizationStateCreateFlags    flags;
		VK_FALSE,                                                    // VkBool32                                   depthClampEnable;
		VK_FALSE,                                                    // VkBool32                                   rasterizerDiscardEnable;
		VK_POLYGON_MODE_FILL,                                        // VkPolygonMode                              polygonMode;
		VK_CULL_MODE_BACK_BIT,                                       // VkCullModeFlags                            cullMode;
		VK_FRONT_FACE_COUNTER_CLOCKWISE,                             // VkFrontFace                                frontFace;
		VK_FALSE,                                                    // VkBool32                                   depthBiasEnable;
		0.f,                                                         // float                                      depthBiasConstantFactor;
		0.f,                                                         // float                                      depthBiasClamp;
		0.f,                                                         // float                                      depthBiasSlopeFactor;
		1.f,                                                         // float                                      lineWidth;
	};

	mMultisampleState = {
		VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,    // VkStructureType                            sType;
		nullptr,                                                     // const void*                                pNext;
		0,                                                           // VkPipelineMultisampleStateCreateFlags      flags;
		VK_SAMPLE_COUNT_1_BIT,                                       // VkSampleCountFlagBits                      rasterizationSamples;
		VK_FALSE,                                                    // VkBool32                                   sampleShadingEnable;
		0.f,                                                         // float                                      minSampleShading;
		VK_NULL_HANDLE,                                              // const VkSampleMask*                        pSampleMask;
		VK_FALSE,                                                    // VkBool32                                   alphaToCoverageEnable;
		VK_FALSE,                                                    // VkBool32                                   alphaToOneEnable;
	};

	mDepthStencilState = {
		VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,  // VkStructureType                            sType;
		nullptr,                                                     // const void*                                pNext;
		0,                                                           // VkPipelineDepthStencilStateCreateFlags     flags;
		VK_TRUE,                                                     // VkBool32                                   depthTestEnable;
		VK_TRUE,                                                     // VkBool32                                   depthWriteEnable;
		VK_COMPARE_OP_LESS_OR_EQUAL,                                 // VkCompareOp                                depthCompareOp;
		VK_FALSE,                                                    // VkBool32                                   depthBoundsTestEnable;
		VK_FALSE,                                                    // VkBool32                                   stencilTestEnable;
		{                                                            // VkStencilOpState                           front;
			VK_STENCIL_OP_KEEP,                                         // VkStencilOp    failOp;				   
			VK_STENCIL_OP_KEEP,                                         // VkStencilOp    passOp;				   
			VK_STENCIL_OP_KEEP,                                         // VkStencilOp    depthFailOp;			   
			VK_COMPARE_OP_ALWAYS,                                       // VkCompareOp    compareOp;			   
			0,                                                          // uint32_t       compareMask;			   
			0,                                                          // uint32_t       writeMask;			   
			0,                                                          // uint32_t       reference;			   
		},
		{                                                            // VkStencilOpState                           back;
			VK_STENCIL_OP_KEEP,                                         // VkStencilOp    failOp;				   
			VK_STENCIL_OP_KEEP,                                         // VkStencilOp    passOp;				   
			VK_STENCIL_OP_KEEP,                                         // VkStencilOp    depthFailOp;			   
			VK_COMPARE_OP_ALWAYS,                                       // VkCompareOp    compareOp;			   
			0,                                                          // uint32_t       compareMask;			   
			0,                                                          // uint32_t       writeMask;			   
			0,                                                          // uint32_t       reference;			   
		},
		0.f,                                                         // float                                      minDepthBounds;
		0.f,                                                         // float                                      maxDepthBounds;
	};

	mDefaultBlendAttachmentState = {
		VK_FALSE,                                                    // VkBool32                 blendEnable;
		VK_BLEND_FACTOR_ZERO,                                        // VkBlendFactor            srcColorBlendFactor;
		VK_BLEND_FACTOR_ZERO,                                        // VkBlendFactor            dstColorBlendFactor;
		VK_BLEND_OP_ADD,                                             // VkBlendOp                colorBlendOp;
		VK_BLEND_FACTOR_ZERO,                                        // VkBlendFactor            srcAlphaBlendFactor;
		VK_BLEND_FACTOR_ZERO,                                        // VkBlendFactor            dstAlphaBlendFactor;
		VK_BLEND_OP_ADD,                                             // VkBlendOp                alphaBlendOp;
		0xf,                                                         // VkColorComponentFlags    colorWriteMask;
	};

	mColorBlendState = {
		VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,    // VkStructureType                               sType;
		nullptr,                                                     // const void*                                   pNext;
		0,                                                           // VkPipelineColorBlendStateCreateFlags          flags;
		VK_FALSE,                                                    // VkBool32                                      logicOpEnable;
		VK_LOGIC_OP_CLEAR,                                           // VkLogicOp                                     logicOp;
		1,                                                           // uint32_t                                      attachmentCount;
		&mDefaultBlendAttachmentState,                               // const VkPipelineColorBlendAttachmentState*    pAttachments;
		{ 0.f, 0.f, 0.f, 0.f }                                       // float                                         blendConstants[4];
	};

	mDefaultDynamicStates = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	mDynamicState = {
		VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,        // VkStructureType                            sType;
		nullptr,                                                     // const void*                                pNext;
		0,                                                           // VkPipelineDynamicStateCreateFlags          flags;
		2,                                                           // uint32_t                                   dynamicStateCount;
		mDefaultDynamicStates.data()                                 // const VkDynamicState*                      pDynamicStates;
	};	
	
	mRenderPass        = nullptr;
	mSubpass           = 0;
	mBasePipelineIndex = -1;

	mShader.reset();
}

// ----------------------------------------------------------------------

VkPipeline vk::GraphicsPipelineState::createPipeline( const VkDevice & device, const VkPipelineCache & pipelineCache, VkPipeline basePipelineHandle_ ){
		VkPipeline pipeline = nullptr;

		// naive: create a pipeline based on current internal state

		// TODO: make sure pipeline is not already in current cache
		//       otherwise return handle to cached pipeline - instead
		//       of moving a new pipeline out, return a handle to 
		//       a borrowed pipeline.

		
		// derive stages from shader
		// TODO: only re-assign if shader has changed.
		auto stageCreateInfo = mShader->getShaderStageCreateInfo();

		VkPipelineCreateFlags createFlags = 0;

		if ( basePipelineHandle_ ){
			// if we already have a base pipeline handle,
			// this means we want to create the next pipeline as 
			// a derivative of the previous pipeline.
			createFlags |= VK_PIPELINE_CREATE_DERIVATIVE_BIT;
		} else{
			// if we have got no base pipeline handle, 
			// we want to signal that this pipeline is not derived from any other, 
			// but may allow derivative pipelines.
			createFlags |= VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
		}

		VkGraphicsPipelineCreateInfo createInfo{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, // VkStructureType                                  sType;
            nullptr,                                         // const void*                                      pNext;
			createFlags,                                     // VkPipelineCreateFlags                            flags;
			uint32_t(stageCreateInfo.size()),                // uint32_t                                         stageCount;
            stageCreateInfo.data(),                          // const VkPipelineShaderStageCreateInfo*           pStages;
            &mShader->getVertexInputState(),                 // const VkPipelineVertexInputStateCreateInfo*      pVertexInputState;
            &mInputAssemblyState,                            // const VkPipelineInputAssemblyStateCreateInfo*    pInputAssemblyState;
            &mTessellationState,                             // const VkPipelineTessellationStateCreateInfo*     pTessellationState;
            &mViewportState,                                 // const VkPipelineViewportStateCreateInfo*         pViewportState;
            &mRasterizationState,                            // const VkPipelineRasterizationStateCreateInfo*    pRasterizationState;
            &mMultisampleState,                              // const VkPipelineMultisampleStateCreateInfo*      pMultisampleState;
            &mDepthStencilState,                             // const VkPipelineDepthStencilStateCreateInfo*     pDepthStencilState;
            &mColorBlendState,                               // const VkPipelineColorBlendStateCreateInfo*       pColorBlendState;
            &mDynamicState,                                  // const VkPipelineDynamicStateCreateInfo*          pDynamicState;
            *mShader->getPipelineLayout(),                   // VkPipelineLayout                                 layout;
            mRenderPass,                                     // VkRenderPass                                     renderPass;
            mSubpass,                                        // uint32_t                                         subpass;
			basePipelineHandle_,                             // VkPipeline                                       basePipelineHandle;
            mBasePipelineIndex                               // int32_t                                          basePipelineIndex;
		};

		auto err = vkCreateGraphicsPipelines( device, pipelineCache, 1, &createInfo, nullptr, &pipeline );

		if ( err != VK_SUCCESS ){
			ofLogError() << "Vulkan error in " << __FILE__ << ", line " << __LINE__;
		}
		
		mDirty = false;
		
        return pipeline ;
}

// ----------------------------------------------------------------------

uint64_t of::vk::GraphicsPipelineState::calculateHash(){

	std::vector<uint64_t> setLayoutKeys = mShader->getSetLayoutKeys();

	std::array<uint64_t, 5> hashTable;

	hashTable[0] = SpookyHash::Hash64( setLayoutKeys.data(), sizeof( uint64_t ) * setLayoutKeys.size(), 0 );
	hashTable[1] = SpookyHash::Hash64( &mRasterizationState, sizeof( mRasterizationState ), 0 );
	hashTable[2] = mShader->getShaderCodeHash();
	hashTable[3] = SpookyHash::Hash64( &mRenderPass, sizeof( mRenderPass ), 0 );
	hashTable[4] = SpookyHash::Hash64( &mSubpass, sizeof( mSubpass ), 0 );


	uint64_t hashOfHashes = SpookyHash::Hash64( hashTable.data(), hashTable.size() * sizeof( uint64_t ), 0 );

	return hashOfHashes;
}

// ----------------------------------------------------------------------
