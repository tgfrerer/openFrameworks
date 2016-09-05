#include "vk/Pipeline.h"
#include "vk/Shader.h"
#include "spooky/SpookyV2.h"
#include <array>

// ----------------------------------------------------------------------

void of::vk::GraphicsPipelineState::setup(){
	reset();
}

// ----------------------------------------------------------------------

void of::vk::GraphicsPipelineState::reset()
{
	mInputAssemblyState = ::vk::PipelineInputAssemblyStateCreateInfo();
	mInputAssemblyState
		.setFlags( ::vk::PipelineInputAssemblyStateCreateFlags() )
		.setTopology( ::vk::PrimitiveTopology::eTriangleList )
		.setPrimitiveRestartEnable( VK_FALSE )
		;

	mTessellationState = ::vk::PipelineTessellationStateCreateInfo();
	mTessellationState
		.setFlags( ::vk::PipelineTessellationStateCreateFlags() )
		.setPatchControlPoints( 0 )
		;

	// viewport and scissor are tracked as dynamic states, so this object
	// will not get used.
	mViewportState = ::vk::PipelineViewportStateCreateInfo();
	mViewportState
		.setViewportCount( 1 )
		.setPViewports( nullptr )
		.setScissorCount( 1 )
		.setPScissors( nullptr )
		;

	mRasterizationState = ::vk::PipelineRasterizationStateCreateInfo();
	mRasterizationState
		.setFlags( ::vk::PipelineRasterizationStateCreateFlags() )
		.setDepthClampEnable( VK_FALSE )
		.setRasterizerDiscardEnable( VK_FALSE )
		.setPolygonMode( ::vk::PolygonMode::eFill )
		.setCullMode( ::vk::CullModeFlagBits::eBack )
		.setFrontFace( ::vk::FrontFace::eCounterClockwise )
		.setDepthBiasEnable( VK_FALSE )
		.setDepthBiasConstantFactor( 0.f )
		.setDepthBiasClamp( 0.f )
		.setDepthBiasSlopeFactor( 1.f )
		.setLineWidth( 1.f )
		;

	mMultisampleState = ::vk::PipelineMultisampleStateCreateInfo();
	mMultisampleState
		.setFlags( ::vk::PipelineMultisampleStateCreateFlags() )
		.setRasterizationSamples( ::vk::SampleCountFlagBits::e1 )
		.setSampleShadingEnable( VK_FALSE )
		.setMinSampleShading( 0.f )
		.setPSampleMask( nullptr )
		.setAlphaToCoverageEnable( VK_FALSE )
		.setAlphaToOneEnable( VK_FALSE )
		;

	::vk::StencilOpState stencilOpState;
	stencilOpState
		.setFailOp( ::vk::StencilOp::eKeep )
		.setPassOp( ::vk::StencilOp::eKeep )
		.setDepthFailOp( ::vk::StencilOp::eKeep )
		.setCompareOp( ::vk::CompareOp::eAlways )
		.setCompareMask( 0 )
		.setWriteMask( 0 )
		.setReference( 0 )
		;

	mDepthStencilState = ::vk::PipelineDepthStencilStateCreateInfo();
	mDepthStencilState
		.setFlags(::vk::PipelineDepthStencilStateCreateFlags())
		.setDepthTestEnable( VK_TRUE )
		.setDepthWriteEnable( VK_TRUE)
		.setDepthCompareOp( ::vk::CompareOp::eLessOrEqual )
		.setDepthBoundsTestEnable( VK_FALSE )
		.setStencilTestEnable( VK_FALSE )
		.setFront( stencilOpState )
		.setBack( stencilOpState )
		.setMinDepthBounds( 0.f )
		.setMaxDepthBounds( 0.f )
		;

	mDefaultBlendAttachmentState = ::vk::PipelineColorBlendAttachmentState();
	mDefaultBlendAttachmentState
		.setBlendEnable( VK_FALSE )
		.setSrcColorBlendFactor( ::vk::BlendFactor::eZero )
		.setDstColorBlendFactor( ::vk::BlendFactor::eZero )
		.setColorBlendOp( ::vk::BlendOp::eAdd)
		.setSrcAlphaBlendFactor( ::vk::BlendFactor::eZero )
		.setDstAlphaBlendFactor( ::vk::BlendFactor::eZero )
		.setAlphaBlendOp( ::vk::BlendOp::eAdd)
		.setColorWriteMask( 
			::vk::ColorComponentFlagBits::eR | 
			::vk::ColorComponentFlagBits::eG | 
			::vk::ColorComponentFlagBits::eB | 
			::vk::ColorComponentFlagBits::eA 
		)
		;

	mColorBlendState = ::vk::PipelineColorBlendStateCreateInfo();
	mColorBlendState
		.setFlags( ::vk::PipelineColorBlendStateCreateFlags() )
		.setLogicOpEnable( VK_FALSE )
		.setLogicOp( ::vk::LogicOp::eClear )
		.setAttachmentCount( 1 )
		.setPAttachments( &mDefaultBlendAttachmentState )
		.setBlendConstants( {0.f,0.f,0.f,0.f} )
		;

	mDefaultDynamicStates = {
		::vk::DynamicState::eScissor,
		::vk::DynamicState::eViewport,
	};

	mDynamicState = ::vk::PipelineDynamicStateCreateInfo();
	mDynamicState
		.setFlags( ::vk::PipelineDynamicStateCreateFlags() )
		.setDynamicStateCount( mDefaultDynamicStates.size() )
		.setPDynamicStates( mDefaultDynamicStates.data() )
		;
	
	mRenderPass        = nullptr;
	mSubpass           = 0;
	mBasePipelineIndex = -1;

	mShader.reset();
}

// ----------------------------------------------------------------------

::vk::Pipeline of::vk::GraphicsPipelineState::createPipeline( const ::vk::Device & device, const ::vk::PipelineCache & pipelineCache, ::vk::Pipeline basePipelineHandle_ ){
		::vk::Pipeline pipeline;

		// naive: create a pipeline based on current internal state

		// TODO: make sure pipeline is not already in current cache
		//       otherwise return handle to cached pipeline - instead
		//       of moving a new pipeline out, return a handle to 
		//       a borrowed pipeline.

		
		// derive stages from shader
		// TODO: only re-assign if shader has changed.
		auto stageCreateInfo = mShader->getShaderStageCreateInfo();

		::vk::PipelineCreateFlags createFlags;

		if ( basePipelineHandle_ ){
			// if we already have a base pipeline handle,
			// this means we want to create the next pipeline as 
			// a derivative of the previous pipeline.
			createFlags |= ::vk::PipelineCreateFlagBits::eDerivative;
		} else{
			// if we have got no base pipeline handle, 
			// we want to signal that this pipeline is not derived from any other, 
			// but may allow derivative pipelines.
			createFlags |= ::vk::PipelineCreateFlagBits::eAllowDerivatives;
		}

		::vk::GraphicsPipelineCreateInfo pipelineCreateInfo;
		
		pipelineCreateInfo
			.setFlags               ( createFlags )
			.setStageCount          ( stageCreateInfo.size() )
			.setPStages             ( stageCreateInfo.data() )
			.setPVertexInputState   ( &mShader->getVertexInputState() )
			.setPInputAssemblyState ( &mInputAssemblyState )
			.setPTessellationState  ( &mTessellationState )
			.setPViewportState      ( &mViewportState )
			.setPRasterizationState ( &mRasterizationState )
			.setPMultisampleState   ( &mMultisampleState )
			.setPDepthStencilState  ( &mDepthStencilState )
			.setPColorBlendState    ( &mColorBlendState )
			.setPDynamicState       ( &mDynamicState )
			.setLayout              ( *mShader->getPipelineLayout() )
			.setRenderPass          ( mRenderPass )
			.setSubpass             ( mSubpass )
			.setBasePipelineHandle  ( basePipelineHandle_ )
			.setBasePipelineIndex   ( mBasePipelineIndex )
			;

		pipeline = device.createGraphicsPipeline( pipelineCache, pipelineCreateInfo );

		mDirty = false;
		
		return pipeline;
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
