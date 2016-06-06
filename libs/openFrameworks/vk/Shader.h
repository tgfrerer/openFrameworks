#pragma once
#include <string>
#include <map>
#include "vulkan\vulkan.h"
#include "spirv_cross.hpp"

#include "ofFileUtils.h"


namespace of{
namespace vk{

class Shader
{
	std::map<VkShaderStageFlagBits, VkShaderModule>        mModules;
	std::vector<VkPipelineShaderStageCreateInfo>	       mStages;
	std::vector<VkDescriptorSetLayoutBinding>              mBindings;

	std::map<VkShaderStageFlagBits, std::shared_ptr<spirv_cross::Compiler>> mCompilers;

public:

	const struct Settings
	{
		VkDevice device;
		std::map<VkShaderStageFlagBits, std::string> sources;
	} mSettings;

	struct VertexInfo
	{
		std::vector<VkVertexInputBindingDescription>   binding;
		std::vector<VkVertexInputAttributeDescription> attribute;
		VkPipelineVertexInputStateCreateInfo vi;
	} mVertexInfo;

	// ----------------------------------------------------------------------
	// derive bindings from shader reflection
	void reflectShaderResources(){

		// go through all shader elements
		for ( auto &c : mCompilers ){

			auto & compiler = *c.second;
			auto & shaderStage = c.first;

			compiler.compile();

			auto shaderResources = compiler.get_shader_resources();

			shaderResources.uniform_buffers.size();

			// --- uniform buffers ---

			for ( auto & ubo : shaderResources.uniform_buffers ){
				ostringstream os;
				
				uint32_t set     = 0;
				uint32_t binding = 0;

				// returns a bitmask 
				uint64_t decorationMask = compiler.get_decoration_mask( ubo.id );

				if ( ( 1ull << spv::DecorationDescriptorSet ) & decorationMask ){
					set = compiler.get_decoration( ubo.id, spv::DecorationDescriptorSet );
					os << ", set = " << set;
				}

				if ( ( 1ull << spv::DecorationBinding ) & decorationMask ){
					binding = compiler.get_decoration( ubo.id, spv::DecorationBinding );
					os << ", binding = " << binding;
				}

				ofLog() << "Uniform Block: '" << ubo.name << "'" << os.str();
				
				// TODO: check under which circumstances descriptorCount needs to be other
				// than 1.

				VkDescriptorSetLayoutBinding  layoutBinding{
					binding,                                              // uint32_t              binding;
					VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,            // VkDescriptorType      descriptorType;
					1,                                                    // uint32_t              descriptorCount;
					shaderStage,                                          // VkShaderStageFlags    stageFlags;
					nullptr,                                              // const VkSampler*      pImmutableSamplers;
				};

				mBindings.emplace_back( std::move( layoutBinding ) );
			} // end for : shaderResources.uniform_buffers

			// --- vertex inputs ---
			
			if ( shaderStage & VK_SHADER_STAGE_VERTEX_BIT ){
				// this populate vertex info 

				ofLog() << "Vertex Attribute locations";

				mVertexInfo.attribute.resize( shaderResources.stage_inputs.size() );
				mVertexInfo.binding.resize( shaderResources.stage_inputs.size() );


				for ( uint32_t i = 0; i != shaderResources.stage_inputs.size(); ++i ){
					auto & input = shaderResources.stage_inputs[i];

					uint64_t decorationMask = compiler.get_decoration_mask( input.id );
					ofLog() << "Vertex Attribute: [" << i << "] : " << input.name;

					// binding description: how memory is mapped to the input assembly
					mVertexInfo.binding[i].binding = i;
					mVertexInfo.binding[i].stride = sizeof( ofVec3f ); // TODO: figure out how to calculate proper stride based on type
					mVertexInfo.binding[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

					// attribute decription: how memory is read from the input assembly
					mVertexInfo.attribute[i].binding = i;
					mVertexInfo.attribute[i].format = VK_FORMAT_R32G32B32_SFLOAT;	 // 3-part float
					mVertexInfo.attribute[i].location = i;
				}

				if ( !shaderResources.stage_inputs.empty() ){
					VkPipelineVertexInputStateCreateInfo vi{
						VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, // VkStructureType                             sType;
						nullptr,                                                   // const void*                                 pNext;
						0,                                                         // VkPipelineVertexInputStateCreateFlags       flags;
						mVertexInfo.binding.size(),                                // uint32_t                                    vertexBindingDescriptionCount;
						mVertexInfo.binding.data(),                                // const VkVertexInputBindingDescription*      pVertexBindingDescriptions;
						mVertexInfo.attribute.size(),                              // uint32_t                                    vertexAttributeDescriptionCount;
						mVertexInfo.attribute.data()                               // const VkVertexInputAttributeDescription*    pVertexAttributeDescriptions;
					};

					mVertexInfo.vi = std::move( vi );
				}
				else{
					// this is kind of weird, but not unheard of (full-screen triangles 
					// may want to do this for example):
					// no vertex attributes = inputs have been defined.
				}
			}
		}  // end for : mCompilers
	};

	// ----------------------------------------------------------------------

	// shader object needs to be initialised based on spir-v sources to be useful
	Shader( const Settings& settings_ )
		: mSettings( settings_ )
	{
		std::vector<uint32_t> spirCode; // tmp container for code loaded from shader file
		size_t                numStages = mSettings.sources.size(); // number of shader files to load

		// load each individual stage

		for ( auto & s : mSettings.sources ){

			VkShaderModule module;

			if ( !ofFile( s.second ).exists() ){
				ofLogError() << "Shader file not found: " << s.second;
				continue;
			}

			{
				ofBuffer fileBuf = ofBufferFromFile( s.second, true );
				spirCode.assign(
					reinterpret_cast<uint32_t*>( fileBuf.getData() ),
					reinterpret_cast<uint32_t*>( fileBuf.getData() ) + fileBuf.size() / sizeof( uint32_t )
				);
			}

			VkShaderModuleCreateInfo info{
				VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,	            // VkStructureType              sType;
				nullptr,	                                                // const void*                  pNext;
				0,	                                                        // VkShaderModuleCreateFlags    flags;
				spirCode.size() * sizeof(uint32_t),	                // size_t                       codeSize;
				spirCode.data()                                     // const uint32_t*              pCode;
			};

			auto err = vkCreateShaderModule( mSettings.device, &info, nullptr, &module );

			if ( err == VK_SUCCESS ){

				mModules[s.first] = module;

				VkPipelineShaderStageCreateInfo shaderStage{
					VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,	// VkStructureType                     sType;
					nullptr,	                                            // const void*                         pNext;
					0,	                                                    // VkPipelineShaderStageCreateFlags    flags;
					s.first,	                                            // VkShaderStageFlagBits               stage;
					module,	                                                // VkShaderModule                      module;
					"main",	                                                // const char*                         pName;
					nullptr	                                                // const VkSpecializationInfo*         pSpecializationInfo;
				};

				mStages.push_back( std::move( shaderStage ) );
				// move the ir code buffer into the shader compiler
				mCompilers[s.first] = make_shared<spirv_cross::Compiler>( std::move( spirCode) );
			}
			else { 
				ofLog() << "Error creating shader module: " << s.second;
			}
		}  // for : mSettings.sources
		reflectShaderResources();
	};

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
	
	// ----------------------------------------------------------------------

	// return  descriptorset layout derived from shader reflection
	// descriptor sets describe the interface for uniforms within the render pipeline
	// note: returns an auto-deleted shared pointer.
	std::shared_ptr<VkDescriptorSetLayout> createDescriptorSetLayout(){
		auto dsl = std::shared_ptr<VkDescriptorSetLayout>( new VkDescriptorSetLayout,
			[&device = mSettings.device]( VkDescriptorSetLayout * dsl ){
			vkDestroyDescriptorSetLayout( device, *dsl, nullptr );
			delete dsl;
		} );

		VkDescriptorSetLayoutCreateInfo ci{
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, // VkStructureType                        sType;
			nullptr,                                             // const void*                            pNext;
			0,                                                   // VkDescriptorSetLayoutCreateFlags       flags;
			mBindings.size(),                                    // uint32_t                               bindingCount;
			mBindings.data()                                     // const VkDescriptorSetLayoutBinding*    pBindings;
		};
		vkCreateDescriptorSetLayout( mSettings.device, &ci, nullptr, dsl.get());

		return ( dsl );
	}

	// return a layout create info derived from shader reflection
	VkPipelineLayoutCreateInfo getLayoutCreateInfo(){
		VkPipelineLayoutCreateInfo res;
		return res;
	}

	// ----------------------------------------------------------------------

};

} // namespace vk
} // namespace of