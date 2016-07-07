#pragma once
#include <string>
#include <map>
#include "vulkan/vulkan.h"
#include "vk/spirv-cross/include/spirv_cross.hpp"


/*

Thoughts : 

	+ a shader should not own descriptorSets nor descriptorSetLayouts,
	  as these may be shared over multiple pipelines
	+ a shader can own uniform tables (complete with set and binding 
	  numbers) - these should be publicly readable 

*/




//	 Here is a diagram on how vertex attribute binding works, based on vkspec 1.0, pp.389
//
//
//     SHADER                                                       | Pipeline setup                               PIPELINE              |  COMMAND BUFFER   | BUFFERS (GPU MEMORY)
//   ---------------------------------------------------------------|--------------------------------------------|-----------------------|-------------------|---------------------
//     shader variable  => associated with => input attribute number| association on per-pipeline basis          | vertex input bindings |  association on a per-draw basis
//                                            in GLSL, set through  | when defining the pipeline with            |                       |  when binding buffers using
//                                           "location" qualifier   | "VkPipelineVertexInputStateCreateInfo"     | "binding number"      |  "vkCmdBindVertexBuffers"
//                                                                  |                                            |                       |                   |
//                                                                  | attributeDescription -> bindingDescription |                       |                   |
//                                                                  |    "location"        -> "binding"          |                       |                   |
//    vec3  inPos                   =>        (location = 0)        | --------------------.  .------------------ | [0] ----------------- |  ------.  .------ |   buffer A
//                                                                  |                      \/                    |                       |         \/        |
//                                                                  |                      /\                    |                       |         /\        |
//    vec3  inNormal                =>        (location = 1)        | --------------------'  '------------------ | [1] ----------------- |  ------'  '------ |   buffer B
//


namespace of{
namespace vk{

class Shader
{
public:
	
	//struct DescriptorSet
	//{
	//	// map from descriptor names to descriptor binding info
	//	// this maps closest to a shader uniform as it combines
	//	// binding number, name, descriptor type, and array size
	//	std::map < std::string, VkDescriptorSetLayoutBinding > uniforms;

	//	// layout is generated from a flattened list of all uniforms
	//	VkDescriptorSetLayout layout = nullptr;
	//	// set is storage for the descriptors described in layout as collection of uniforms
	//	VkDescriptorSet set = nullptr;
	//};

	//// map from "desciptor set" number to descriptorSet,
	//// we use a map instead of vector, since this collection may be sparse
	//// and idx indices ommitted. 
	//std::map<uint32_t, DescriptorSet> mDescriptorSets;
	
	struct UniformInfo
	{
		uint32_t set;							   // Descriptor set this binding belongs to
		VkDescriptorSetLayoutBinding binding;	   // Describes mapping of binding number to descriptors, 
		                                           // number and type of descriptors under a binding.
		uint32_t size;						       // size in bytes
	};

private:
	std::map<VkShaderStageFlagBits, VkShaderModule>         mModules;
	std::vector<VkPipelineShaderStageCreateInfo>	        mStages;

	//std::map<std::string, Binding> mBindings; // map from ubo block name to binding
	std::map<VkShaderStageFlagBits, std::shared_ptr<spirv_cross::Compiler>> mCompilers;
	
	// map from uniform name to uniform set and binding info 
	// when we say "uniform" this may be any of VkDescriptorType, so: UBOs, samplers ...
	std::map<std::string, UniformInfo> mUniforms;

	// ----------------------------------------------------------------------
	// Derive bindings from shader reflection using SPIR-V Cross.
	// we want to extract as much information out of the shader metadata as possible
	// all this data helps us to create descriptors, and also to create layouts fit
	// for our pipelines.
	void reflectShaderResources();

	std::vector<uint64_t> uniformSetIds;

public:

	const struct Settings
	{
		VkDevice device;
		std::map<VkShaderStageFlagBits, std::string> sources;
	} mSettings;

	struct VertexInfo
	{
		std::vector<VkVertexInputBindingDescription>   bindingDescription;	  // describes data input parameters for pipeline slots
		std::vector<VkVertexInputAttributeDescription> attribute;	          // mapping of attribute locations to pipeline slots
		VkPipelineVertexInputStateCreateInfo vi;
	} mVertexInfo;

	// ----------------------------------------------------------------------

	// shader object needs to be initialised based on spir-v sources to be useful
	Shader( const Settings& settings_ );

	// ----------------------------------------------------------------------

	~Shader()
	{
		mCompilers.clear();
		// reset shader object
		for ( auto &s : mModules ){
			if ( s.second != nullptr ){
				vkDestroyShaderModule( mSettings.device, s.second, nullptr );
				s.second = nullptr;
			}
		}
		mModules.clear();
		mStages.clear();
	}

	// ----------------------------------------------------------------------

	const std::map <std::string, UniformInfo>& getUniforms() const{
		return mUniforms;
	}

	// ----------------------------------------------------------------------

	// returns an ordered list of descriptor set names 
	// order is based on uniform set index, that is the order of elements
	// in this vector defines the layout of descriptorSets for this shader/pipeline
	// empty elements mean empty slots
	const std::vector<std::string> getDescriptorSetLayoutNames(){
	
	}

	// ----------------------------------------------------------------------

	// return vector create info for all shader modules which compiled successfully
	const std::vector<VkPipelineShaderStageCreateInfo>& getShaderStageCreateInfo(){
		return mStages;
	}

	// ----------------------------------------------------------------------
	// return vertex input state create info
	// this hold the layout and wiring of attribute inputs to vertex bindings
	const VkPipelineVertexInputStateCreateInfo& getVertexInputState(){
		return mVertexInfo.vi;
	}
	
};

} // namespace vk
} // namespace of
