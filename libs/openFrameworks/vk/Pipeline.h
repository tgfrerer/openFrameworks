#pragma once

#include "vulkan\vulkan.h"
#include <string>

#include "ofFileUtils.h"
#include "ofLog.h"

/*

A pipeline is a monolithic object that contains all the state
needed for a draw call. You can look at it as a GPU program
combining shader code with vendor-specific code dealing with 
blending, primitive assembly, etc. 

The Pipeline has a layout, that's the "function signature" so
to say, for the uniform parameters. You feed these parameters 
when you bind descriptor sets to the command buffer which you
are currently recording. A pipeline bound to the same command
buffer will then use these inputs.

A pipeline can have some dynamic state, that is state which is
controlled by the command buffer. State which may be dynamic is
pretty limited, and has to be defined when you create a pipeline.

When a pipeline is created it is effectively compiled into a 
GPU program. Different non-dynamic pipeline state needs a different
pipeline. That's why you potentially need a pipeline for all 
possible combinations of states that you may use.

## Mission Statement

This class helps you create pipelines, and it also wraps pipeline
caching, so that pipelines can be requested based on dynamic state
and will be either created dynamically or created upfront. 

This class shall also help you to create pipeline layouts, based
on how your shaders are defined. It will try to match shader 
information gained through reflection (using spriv-cross) with 
descriptorSetLayouts to see if things are compatible.

The API will return VK handles, so that other libraries can be used
on top or alternatively to this one.


*/

namespace of{
namespace vk{

class Shader;

class Pipeline
{
	// The idea is to have the context hold a pipeline in memory, 
	// and with each draw command store the current pipeline's 
	// hash into the command batch. 

	// when we build the command buffer, we need to check 
	// if the current context state is matched by an already 
	// available pipeline. 
	// 
	// if it isn't, we have to compile a pipeline for the command
	// 
	// if it is, we bind that pipeline.

	// you can only create a pipeline object from a shader.
	Pipeline(  );

	// or from another pipeline object for that matter.
	Pipeline operator=( const Pipeline& rhs ){
		// TODO: make proper copy
		Pipeline lhs;
		lhs = rhs;
		return lhs;
	};

private: // these elements are set at pipeline instantiation

	// TODO: static, shader-dependent (build using spirv-cross)
	const std::vector<VkPipelineShaderStageCreateInfo>     mStages = {
		// todo: fill with defaults.
		{
		// default vertex shader state
		},
		{
		// default fragment shader state
		},
	};

	// TODO: static, shader-dependent (build using spirv-cross)
	const VkPipelineVertexInputStateCreateInfo mVertexInputState{};

public:	// default state for pipeline

	VkPipelineInputAssemblyStateCreateInfo mInputAssemblyState {
		VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, // VkStructureType                            sType;
		nullptr,                                                     // const void*                                pNext;
		0,                                                           // VkPipelineInputAssemblyStateCreateFlags    flags;
		VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,                         // VkPrimitiveTopology                        topology;
		VK_FALSE,                                                    // VkBool32                                   primitiveRestartEnable;
	};

	VkPipelineTessellationStateCreateInfo mTessellationState {
		VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO,	 // VkStructureType                            sType;
		nullptr,                                                     // const void*                                pNext;
		0,                                                           // VkPipelineTessellationStateCreateFlags     flags;
		0,                                                           // uint32_t                                   patchControlPoints;
	};																 
	
	VkPipelineViewportStateCreateInfo mViewportState {				 
		VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,       // VkStructureType                            sType;
		nullptr,                                                     // const void*                                pNext;
		0,                                                           // VkPipelineViewportStateCreateFlags         flags;
		1,                                                           // uint32_t                                   viewportCount;
		nullptr,                                                     // const VkViewport*                          pViewports;
		1,                                                           // uint32_t                                   scissorCount;
		nullptr,                                                     // const VkRect2D*                            pScissors;
	};																 
	
	VkPipelineRasterizationStateCreateInfo mRasterizationState {	 
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
	
	VkPipelineMultisampleStateCreateInfo mMultisampleState {													   
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
	
	VkPipelineDepthStencilStateCreateInfo mDepthStencilState{
		VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,  // VkStructureType                            sType;
		nullptr,                                                     // const void*                                pNext;
		0,                                                           // VkPipelineDepthStencilStateCreateFlags     flags;
		VK_TRUE,                                                     // VkBool32                                   depthTestEnable;
		VK_TRUE,                                                     // VkBool32                                   depthWriteEnable;
		VK_COMPARE_OP_LESS_OR_EQUAL,                                 // VkCompareOp                                depthCompareOp;
		VK_FALSE,                                                    // VkBool32                                   depthBoundsTestEnable;
		VK_FALSE,                                                    // VkBool32                                   stencilTestEnable;
		{															 // VkStencilOpState                           front;
			VK_STENCIL_OP_KEEP,                                         // VkStencilOp    failOp;				   
			VK_STENCIL_OP_KEEP,                                         // VkStencilOp    passOp;				   
			VK_STENCIL_OP_KEEP,                                         // VkStencilOp    depthFailOp;			   
			VK_COMPARE_OP_ALWAYS,                                       // VkCompareOp    compareOp;			   
			0,                                                          // uint32_t       compareMask;			   
			0,                                                          // uint32_t       writeMask;			   
			0,                                                          // uint32_t       reference;			   
		},                                                           											   
		mDepthStencilState.front,                                    // VkStencilOpState                           back;
		0.f,                                                         // float                                      minDepthBounds;
		0.f,                                                         // float                                      maxDepthBounds;
	};

	VkPipelineColorBlendAttachmentState mDefaultBlendAttachmentState {
		VK_FALSE,                                                    // VkBool32                 blendEnable;
		VK_BLEND_FACTOR_ZERO,                                        // VkBlendFactor            srcColorBlendFactor;
		VK_BLEND_FACTOR_ZERO,                                        // VkBlendFactor            dstColorBlendFactor;
		VK_BLEND_OP_ADD,                                             // VkBlendOp                colorBlendOp;
		VK_BLEND_FACTOR_ZERO,                                        // VkBlendFactor            srcAlphaBlendFactor;
		VK_BLEND_FACTOR_ZERO,                                        // VkBlendFactor            dstAlphaBlendFactor;
		VK_BLEND_OP_ADD,                                             // VkBlendOp                alphaBlendOp;
		0xf,                                                         // VkColorComponentFlags    colorWriteMask;
	};

	VkPipelineColorBlendStateCreateInfo mColorBlendState {
		VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,    // VkStructureType                               sType;
		nullptr,                                                     // const void*                                   pNext;
		0,                                                           // VkPipelineColorBlendStateCreateFlags          flags;
		VK_FALSE,                                                    // VkBool32                                      logicOpEnable;
		VK_LOGIC_OP_CLEAR,                                           // VkLogicOp                                     logicOp;
		1,                                                           // uint32_t                                      attachmentCount;
		&mDefaultBlendAttachmentState,                               // const VkPipelineColorBlendAttachmentState*    pAttachments;
	    {0.f, 0.f, 0.f, 0.f}                                         // float                                         blendConstants[4];
	};
	
	VkDynamicState mDefaultDynamicStates[2]{
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};

	VkPipelineDynamicStateCreateInfo pDynamicState {
		VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,        // VkStructureType                            sType;
		nullptr,                                                     // const void*                                pNext;
		0,                                                           // VkPipelineDynamicStateCreateFlags          flags;
		2,                                                           // uint32_t                                   dynamicStateCount;
		mDefaultDynamicStates                                        // const VkDynamicState*                      pDynamicStates;
	};
	
	VkPipelineLayout  layout             = nullptr;
	VkRenderPass      renderPass         = nullptr;
	uint32_t          subpass            = 0;
	VkPipeline        basePipelineHandle = nullptr;
	int32_t           basePipelineIndex  = 0;

};

class PipelineHelper
{
	struct Parameters
	{
		// these parametes are borrowed, ownership is outside of this
		// class.
		VkDevice&         device;        // current device
		VkPipelineCache&  pipelineCache; // cache for all pipelines
	} const mParams;

	// We need to keep track of our pipelines internally and hash
	// them. This hash can be used to order draw calls within a 
	// command buffer recording.

	
};

/// \brief  Create a pipeline cache object
/// \detail Optionally load from disk, if filepath given.
/// \note  	Ownership: passed on.
static VkPipelineCache&& createPipelineCache( const VkDevice& device, std::string filePath = "" ){
	VkPipelineCache cache;
	ofBuffer cacheFileBuffer;

	VkPipelineCacheCreateInfo info{
		VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,                // VkStructureType               sType;
		nullptr,                                                     // const void*                   pNext;
		0,                                                           // VkPipelineCacheCreateFlags    flags;
		0,                                                           // size_t                        initialDataSize;
		nullptr,                                                     // const void*                   pInitialData;
	};

	if ( ofFile( filePath ).exists() ){
		cacheFileBuffer = ofBufferFromFile( filePath, true );
		info.initialDataSize = cacheFileBuffer.size();
		info.pInitialData = cacheFileBuffer.getData();
	}

	auto err = vkCreatePipelineCache( device, &info, nullptr, &cache );

	if ( err != VK_SUCCESS ){
		ofLogError() << "Vulkan error in " << __FILE__ << ", line " << __LINE__;
	}

	return std::move( cache );
}

} // namespace vk
} // namespace of