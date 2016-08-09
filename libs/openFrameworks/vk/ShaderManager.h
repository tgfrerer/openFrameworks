#pragma once

#include "vulkan/vulkan.h"
#include "vk/Shader.h"
#include "ofLog.h"

namespace of{
namespace vk{

/*

   Shader manager keeps track of all descriptorsetlayouts and pipelinelayouts

   + it is intended to be used across threads, by multiple contexts.
   + it is only supposed to contain metadata - so layout and binding descriptions,
   not the actual DescriptorSets (which holds actual descriptors)

*/

class ShaderManager
{
public:
	struct Settings
	{
		VkDevice device = nullptr;
	} const mSettings;

	ShaderManager(Settings settings)
	: mSettings(settings){
	};
	

private:
	struct DescriptorSetLayoutInfo
	{
		of::vk::Shader::SetLayout setLayout;
		VkDescriptorSetLayout     vkDescriptorSetLayout;
	};
	// map of descriptorsetlayoutKey to descriptorsetlayout
	// this map is the central registry of DescriptorSetLayouts for all Shaders
	// used with(in) this Context.
	std::map<uint64_t, std::shared_ptr<DescriptorSetLayoutInfo>> mDescriptorSetLayouts;

	friend class Shader;

	void storeDescriptorSetLayout( Shader::SetLayout && setLayout_ ){

		// 1. check if layout already exists
		// 2. if no, store layout in our map.

		auto & dsl = mDescriptorSetLayouts[setLayout_.key];

		if ( dsl.get() == nullptr ){

			// if no descriptor set was found, this means that 
			// no element with such hash exists yet in the registry.

			// create & store descriptorSetLayout based on bindings for this set
			// This means first to extract just the bindings from the data structure
			// so we can feed it to the vulkan api.
			std::vector<VkDescriptorSetLayoutBinding> bindings;

			bindings.reserve( setLayout_.bindings.size() );

			for ( const auto & b : setLayout_.bindings ){
				bindings.push_back( b.binding );
			}

			VkDescriptorSetLayoutCreateInfo createInfo{
				VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,    // VkStructureType                        sType;
				nullptr,                                                // const void*                            pNext;
				0,                                                      // VkDescriptorSetLayoutCreateFlags       flags;
				uint32_t( bindings.size() ),                            // uint32_t                               bindingCount;
				bindings.data()                                         // const VkDescriptorSetLayoutBinding*    pBindings;
			};

			dsl = std::shared_ptr<DescriptorSetLayoutInfo>( new DescriptorSetLayoutInfo{ std::move( setLayout_ ), nullptr },
				[device = mSettings.device]( DescriptorSetLayoutInfo* lhs ){
				vkDestroyDescriptorSetLayout( device, lhs->vkDescriptorSetLayout, nullptr );
				delete lhs;
			} );

			vkCreateDescriptorSetLayout( mSettings.device, &createInfo, nullptr, &dsl->vkDescriptorSetLayout );
		}

		ofLog() << "DescriptorSetLayout " << std::hex << setLayout_.key << " | Use Count: " << dsl.use_count();
	}

public:

	const std::vector<of::vk::Shader::BindingInfo>& getBindings( uint64_t descriptorSetLayoutKey ){
		static std::vector<of::vk::Shader::BindingInfo> failedBinding;
		
		const auto & it = mDescriptorSetLayouts.find( descriptorSetLayoutKey );
		if ( it != mDescriptorSetLayouts.end() ){
			return it->second->setLayout.bindings;
		} else{
			ofLogError() << "No binding for DescriptorSetLayout key: " << std::hex << descriptorSetLayoutKey;
		}
		
		return failedBinding;
	};

	const VkDescriptorSetLayout getDescriptorSetLayout( uint64_t descriptorSetLayoutKey ){
		static VkDescriptorSetLayout failedLayout = nullptr;

		const auto & it = mDescriptorSetLayouts.find( descriptorSetLayoutKey );
		if ( it != mDescriptorSetLayouts.end() ){
			return it->second->vkDescriptorSetLayout;
		} else{
			ofLogError() << "No binding for DescriptorSetLayout key: " << std::hex << descriptorSetLayoutKey;
		}

		return failedLayout;
	}

	const std::map<uint64_t, std::shared_ptr<DescriptorSetLayoutInfo>>& getDescriptorSetLayouts(){
		return mDescriptorSetLayouts;
	};

	

};	/* class ShaderManager */








}  /* namesapce vk */
}  /* namesapce of */