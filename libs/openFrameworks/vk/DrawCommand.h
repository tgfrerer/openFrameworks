#pragma once
#include "vulkan/vulkan.hpp"
#include "vk/Shader.h"
#include "vk/Pipeline.h"



namespace of {

class BufferObject;		 // ffdecl.
class IndexBufferObject; // ffdecl.
class DrawCommand;
class RenderBatch;

// ----------------------------------------------------------------------

class DrawCommandInfo {
	
	friend class DrawCommand;
							                        
	std::map<std::string, std::vector<uint8_t>>       uboData;
	std::map<std::string, std::vector<::vk::Sampler>> samplerData;

	std::map<std::string, BufferObject * >          attributeData;
	IndexBufferObject *                             indexData        = nullptr;

	vk::GraphicsPipelineState                       pipeline;

public:

	vk::GraphicsPipelineState& getPipeline(){
		pipeline.mDirty = true; // invalidate hash
		return pipeline;
	}

	const vk::GraphicsPipelineState& getPipelineC(){
		return pipeline;
	}

};

// ----------------------------------------------------------------------

class DrawCommand
{
	// a draw command has everything needed to draw an object
	const DrawCommandInfo mDrawCommandInfo;

	DrawCommand() = delete;
	
	uint64_t mPipelineHash;

public:

	const DrawCommandInfo& getInfo(){
		return mDrawCommandInfo;
	}

	// setup all non-transient state for this draw object
	DrawCommand( const DrawCommandInfo& dcs );

	// set data for upload to ubo
	void setUboData( const std::string uboName, const std::vector<uint8_t> data ){};

};




} // namespace of