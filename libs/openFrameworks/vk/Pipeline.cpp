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
	inputAssemblyState = ::vk::PipelineInputAssemblyStateCreateInfo();
	inputAssemblyState
		.setTopology( ::vk::PrimitiveTopology::eTriangleList )
		.setPrimitiveRestartEnable( VK_FALSE )
		;

	tessellationState = ::vk::PipelineTessellationStateCreateInfo();
	tessellationState
		.setPatchControlPoints( 0 )
		;

	// viewport and scissor are tracked as dynamic states, so this object
	// will not get used.
	viewportState = ::vk::PipelineViewportStateCreateInfo();
	viewportState
		.setViewportCount( 1 )
		.setPViewports( nullptr )
		.setScissorCount( 1 )
		.setPScissors( nullptr )
		;

	rasterizationState = ::vk::PipelineRasterizationStateCreateInfo();
	rasterizationState
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

	multisampleState = ::vk::PipelineMultisampleStateCreateInfo();
	multisampleState
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
		.setCompareOp( ::vk::CompareOp::eNever )
		.setCompareMask( 0 )
		.setWriteMask( 0 )
		.setReference( 0 )
		;

	depthStencilState = ::vk::PipelineDepthStencilStateCreateInfo();
	depthStencilState
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

	blendAttachmentStates.fill( ::vk::PipelineColorBlendAttachmentState() );

	blendAttachmentStates[0]
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

	colorBlendState = ::vk::PipelineColorBlendStateCreateInfo();
	colorBlendState
		.setLogicOpEnable( VK_FALSE )
		.setLogicOp( ::vk::LogicOp::eClear )
		.setAttachmentCount( 1 )
		.setPAttachments   ( nullptr )
		.setBlendConstants( {0.f,0.f,0.f,0.f} )
		;

	dynamicStates = {
		::vk::DynamicState::eScissor,
		::vk::DynamicState::eViewport,
	};

	dynamicState = ::vk::PipelineDynamicStateCreateInfo();
	dynamicState
		.setDynamicStateCount( dynamicStates.size() )
		.setPDynamicStates( nullptr )
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


		// make sure pointers to internal vectors and arrays are valid:

		colorBlendState
			.setPAttachments        ( blendAttachmentStates.data() )
			;

		dynamicState
			.setDynamicStateCount(dynamicStates.size())
			.setPDynamicStates      ( dynamicStates.data() )
			;

		// create pipeline info object based on current pipeline object state

		::vk::GraphicsPipelineCreateInfo pipelineCreateInfo;
		
		pipelineCreateInfo
			.setFlags               ( createFlags )
			.setStageCount          ( stageCreateInfo.size() )
			.setPStages             ( stageCreateInfo.data() )
			.setPVertexInputState   ( &mShader->getVertexInputState() )
			.setPInputAssemblyState ( &inputAssemblyState )
			.setPTessellationState  ( &tessellationState )
			.setPViewportState      ( &viewportState )
			.setPRasterizationState ( &rasterizationState )
			.setPMultisampleState   ( &multisampleState )
			.setPDepthStencilState  ( &depthStencilState )
			.setPColorBlendState    ( &colorBlendState )
			.setPDynamicState       ( &dynamicState )
			.setLayout              ( *mShader->getPipelineLayout() )
			.setRenderPass          ( mRenderPass )
			.setSubpass             ( mSubpass )
			.setBasePipelineHandle  ( basePipelineHandle_ )
			.setBasePipelineIndex   ( mBasePipelineIndex )
			;

		pipeline = device.createGraphicsPipeline( pipelineCache, pipelineCreateInfo );

		// reset internal pointers, so hashing works again

		colorBlendState
			.setPAttachments( nullptr )
			;

		dynamicState
			.setPDynamicStates( nullptr )
			;

		mDirty = false;
		
		return pipeline;
}

// ----------------------------------------------------------------------

bool of::vk::GraphicsPipelineState::operator==( GraphicsPipelineState const & rhs ){
	return mRenderPass == rhs.mRenderPass
		&& mSubpass == rhs.mSubpass
		&& mShader->getShaderCodeHash() == rhs.mShader->getShaderCodeHash()
		&& inputAssemblyState == rhs.inputAssemblyState
		&& tessellationState == rhs.tessellationState
		&& viewportState == rhs.viewportState
		&& rasterizationState == rhs.rasterizationState
		&& multisampleState == rhs.multisampleState
		&& depthStencilState == rhs.depthStencilState
		&& colorBlendState == rhs.colorBlendState
		&& dynamicState == rhs.dynamicState
		;
}
	   	   
// ----------------------------------------------------------------------

uint64_t of::vk::GraphicsPipelineState::calculateHash() const {

	// TODO: this method is broken -- the reason for this is that 
	// some vk::.. objects are not laid out tightly in memory - this
	// means we cannot look at pipeline state as a POD, but must 
	// find a way to quickly hash all member objects separately, 
	// if necessary even their member elements. 
	//
	// You need to be super careful about this if you are on 
	// an architecture that is 8 byte aligned, as pointers are 8 byte, 
	// but types like VKBool32 are 4 bytes.

	std::vector<uint64_t> setLayoutKeys = mShader->getDescriptorSetLayoutKeys();

	uint64_t hash = mShader->getShaderCodeHash();

	hash = SpookyHash::Hash64( setLayoutKeys.data(), sizeof( uint64_t ) * setLayoutKeys.size(), hash );

	hash = SpookyHash::Hash64( (void*) &inputAssemblyState,    sizeof( inputAssemblyState    ), hash );
	hash = SpookyHash::Hash64( (void*) &tessellationState,     sizeof( tessellationState     ), hash );
	hash = SpookyHash::Hash64( (void*) &viewportState,         sizeof( viewportState         ), hash );
	hash = SpookyHash::Hash64( (void*) &rasterizationState,    sizeof( rasterizationState    ), hash );
	hash = SpookyHash::Hash64( (void*) &multisampleState,      sizeof( multisampleState      ), hash );
	hash = SpookyHash::Hash64( (void*) &depthStencilState,     sizeof( depthStencilState     ), hash );
	hash = SpookyHash::Hash64( (void*) &dynamicStates,         sizeof( dynamicStates         ), hash );
	hash = SpookyHash::Hash64( (void*) &blendAttachmentStates, sizeof( blendAttachmentStates ), hash );
	hash = SpookyHash::Hash64( (void*) &colorBlendState,       sizeof( colorBlendState       ), hash );
	hash = SpookyHash::Hash64( (void*) &dynamicState,          sizeof( dynamicState          ), hash );
	hash = SpookyHash::Hash64( (void*) &mRenderPass,           sizeof( mRenderPass           ), hash );
	hash = SpookyHash::Hash64( (void*) &mSubpass,              sizeof( mSubpass              ), hash );
	
	ofLog() << "pipeline hash:" << std::hex << hash;
	return hash;
}

// ----------------------------------------------------------------------
