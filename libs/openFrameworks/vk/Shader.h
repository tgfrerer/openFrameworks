#pragma once
#include <string>
#include <map>
#include "vulkan/vulkan.h"

#include "ofFileUtils.h"
#include "spirv_cross.hpp"


namespace of{
namespace vk{

class Shader
{
	std::map<VkShaderStageFlagBits, VkShaderModule>         mModules;
	std::vector<VkPipelineShaderStageCreateInfo>	        mStages;

	struct Binding
	{	
		uint32_t                      set;
		VkDescriptorSetLayoutBinding  layout;
		//VkDescriptorBufferInfo        buffer; // we may need to store the buffer with the bindings so that we can use this to fill in the writeDescriptorSet.
	};

	std::map<std::string, Binding> mBindings; // map from block name to binding
	std::map<VkShaderStageFlagBits, std::shared_ptr<spirv_cross::Compiler>> mCompilers;

	// contains bindings programmed from a flattened list of descriptorSetLayouts
	// "represents a sequence of descriptor sets with each having a specific layout"
	//
	// The pipeline layout describes which descriptor sets you are using as well as push 
	// constants. This serves as the "function prototype" for your shader.
	// see: https://community.arm.com/groups/arm-mali-graphics/blog/2016/04/18/spirv-cross
	//std::shared_ptr<VkPipelineLayout> mPipelineLayout;
	

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
	// derive bindings from shader reflection using SPIR-V Cross
	// we want to extract as much information out of the shader metadata as possible
	// all this data helps us to create descriptors, and also to create layouts fit
	// for our pipelines.
	void reflectShaderResources(){

		// go through all shader elements
		for ( auto &c : mCompilers ){

			auto & compiler = *c.second;
			auto & shaderStage = c.first;

			auto shaderResources = compiler.get_shader_resources();

			shaderResources.uniform_buffers.size();

			// --- uniform buffers ---
			// DESCRIPTORS

			for ( auto & ubo : shaderResources.uniform_buffers ){
				ostringstream os;
				
				uint32_t descriptor_set     = 0;
				uint32_t binding            = 0;

				// returns a bitmask 
				uint64_t decorationMask = compiler.get_decoration_mask( ubo.id );
				

				if ( ( 1ull << spv::DecorationDescriptorSet ) & decorationMask ){
					descriptor_set = compiler.get_decoration( ubo.id, spv::DecorationDescriptorSet );
					os << ", set = " << descriptor_set;
				}

				if ( ( 1ull << spv::DecorationBinding ) & decorationMask ){
					binding = compiler.get_decoration( ubo.id, spv::DecorationBinding );
					os << ", binding = " << binding;
				}

				ofLog() << "Uniform Block: '" << ubo.name << "'" << os.str();

				auto type = compiler.get_type( ubo.type_id );

				// type for ubo descriptors is struct
				// such structs will have member types, that is, they have elements within.
				for ( uint32_t tI = 0; tI != type.member_types.size(); ++tI ){
					auto mn = compiler.get_member_name(ubo.type_id, tI );
					auto mt = compiler.get_type( type.member_types[tI] );
					ofLog() << "Member Name: " << ubo.name << "[" << tI << "] : " << mn;
				}

				{
					// let's look up if the current block name already exists in the 
					// table of bindings for this shader, and if necessary update
					// the shader stage flags to permit access to all stages that need it:

					// shaderStage defines from which shader stages this layout is accessible
					VkShaderStageFlags layoutAccessibleFromStages = shaderStage;

					VkDescriptorSetLayoutBinding  layoutBinding{
						binding,                                              // uint32_t              binding;
						VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,            // VkDescriptorType      descriptorType;
						1,                  // <- check:array?                // uint32_t              descriptorCount;
						layoutAccessibleFromStages,                           // VkShaderStageFlags    stageFlags;
						nullptr,                                              // const VkSampler*      pImmutableSamplers;
					};

					if ( mBindings.find( ubo.name ) != mBindings.end() ){
						// we have found a binding with the same name in another shader stage.
						// therefore we: 
						// 1.) need to update the binding accessiblity flag
						// 2.) do some error checking to make sure the binding is the same.

						auto& existingBinding = mBindings[ubo.name];
						if ( existingBinding.set != descriptor_set
							|| existingBinding.layout.binding != binding ){
							ofLogError() << "Incompatible bindings between shader stages: " << ubo.name;
						}
						else{
							// all good, make sure the binding is also accessible in the 
							// current stage.
							layoutBinding.stageFlags |= existingBinding.layout.stageFlags;
						}
					}

					mBindings[ubo.name] = { descriptor_set, layoutBinding };
				}
				// TODO: check under which circumstances descriptorCount needs to be other
				// than 1.
				
				
			} // end for : shaderResources.uniform_buffers

			// --- vertex inputs ---
			// VERTEX ATTRIBUTES
			
			if ( shaderStage & VK_SHADER_STAGE_VERTEX_BIT ){
				// this populate vertex info 

				ofLog() << "Vertex Attribute locations";

				mVertexInfo.attribute.resize( shaderResources.stage_inputs.size() );
				mVertexInfo.binding.resize( shaderResources.stage_inputs.size() );

				for ( uint32_t i = 0; i != shaderResources.stage_inputs.size(); ++i ){
					auto & input = shaderResources.stage_inputs[i];

					ofLog() << "Vertex Attribute: [" << i << "] : " << input.name;

					auto type = compiler.get_type( input.type_id );

					// binding description: how memory is mapped to the input assembly
					mVertexInfo.binding[i].binding = i;
					mVertexInfo.binding[i].stride = (type.width/8) * type.vecsize * type.columns;
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
		//mPipelineLayout = createPipelineLayout();
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

	const std::map < std::string , Binding > & getBindings() const{
		return mBindings;
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
		    [device = mSettings.device]( VkDescriptorSetLayout * dsl )
		{
			vkDestroyDescriptorSetLayout( device, *dsl, nullptr );
			delete dsl;
		} );

		vector<VkDescriptorSetLayoutBinding> tmpFlattenedBindings( mBindings.size() );
		for ( auto b:mBindings ){
			tmpFlattenedBindings[b.second.set] = b.second.layout;
		}

		VkDescriptorSetLayoutCreateInfo ci{
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, // VkStructureType                        sType;
			nullptr,                                             // const void*                            pNext;
			0,                                                   // VkDescriptorSetLayoutCreateFlags       flags;
			static_cast<uint32_t>(tmpFlattenedBindings.size()),  // uint32_t                               bindingCount;
			tmpFlattenedBindings.data()                          // const VkDescriptorSetLayoutBinding*    pBindings;
		};

		vkCreateDescriptorSetLayout( mSettings.device, &ci, nullptr, dsl.get());

		return ( dsl );
	}

};

} // namespace vk
} // namespace of
