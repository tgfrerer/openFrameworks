#include "vk/Pipeline.h"
#include "vk/Shader.h"

using namespace of;

VkPipeline vk::GraphicsPipelineState::createPipeline( const VkDevice & device, const VkPipelineCache & pipelineCache, VkPipeline basePipelineHandle_ ){
		VkPipeline pipeline = nullptr;

		// naive: create a pipeline based on current internal state

		// TODO: make sure pipeline is not already in current cache
		//       otherwise return handle to cached pipeline - instead
		//       of moving a new pipeline out, return a handle to 
		//       a borrowed pipeline.

		
		// derive stages from shader
		// TODO: only re-assign if shader has changed.
		auto & stageCreateInfo = mShader->getShaderStageCreateInfo();

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
			createFlags,      // VkPipelineCreateFlags                            flags;
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
            *mLayout,                                        // VkPipelineLayout                                 layout;
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

