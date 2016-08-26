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

	// central store of descriptor infos - indexed by descriptor info hash
	std::map<uint64_t, std::shared_ptr<of::vk::Shader::DescriptorInfo>>   mDescriptorInfoStore;
	
	// central store of set layouts - indexed by set layout hash
	// set layouts are ordered sequences of uniforms
	std::map<uint64_t, std::shared_ptr<of::vk::Shader::SetLayoutInfo>> mSetLayoutStore;

public:

	// TODO: these methods - as they may modify the contents of ShaderManager
	// may need to be mutexed to be safe across threads.
	std::shared_ptr<of::vk::Shader::DescriptorInfo>& borrowDescriptorInfo( uint64_t hash ){
		return mDescriptorInfoStore[hash];
	}

	const std::shared_ptr<of::vk::Shader::DescriptorInfo>& getDescriptorInfo( uint64_t hash ) const{
		static std::shared_ptr<of::vk::Shader::DescriptorInfo> failDescriptorInfo;
		const auto &it = mDescriptorInfoStore.find( hash );
		if ( it != mDescriptorInfoStore.end() ){
			return it->second;
		} else{
			ofLogError() << "Could not find uniform with id: " << std::hex << hash;
		}
		return failDescriptorInfo;
	}

	std::shared_ptr<of::vk::Shader::SetLayoutInfo>& borrowSetLayoutMeta( uint64_t hash ){
		return mSetLayoutStore[hash];
	}

	const std::map<uint32_t, std::shared_ptr<of::vk::Shader::DescriptorInfo>>& getBindings( uint64_t setLayoutHash ) const;
	
	const std::map<uint64_t, std::shared_ptr<of::vk::Shader::DescriptorInfo>>& getDescriptorInfos() const{
		return mDescriptorInfoStore;
	};


private:

	// central store of VkDescriptorSetLayouts, indexed by corresponding SetLayoutInfo hash
	std::map<uint64_t, std::shared_ptr<VkDescriptorSetLayout>> mDescriptorSetLayoutStore;

	// central store of bindings per descriptor set layout, indexed by descriptor set layout hash
	std::map<uint64_t, std::map<uint32_t, std::shared_ptr<of::vk::Shader::DescriptorInfo>>> mBindingsPerSetStore;
public:

	// create VkDescriptorSetLayouts for all descriptors currently held in mSetLayoutStore
	bool createVkDescriptorSetLayouts();

	const VkDescriptorSetLayout& getVkDescriptorSetLayout( uint64_t descriptorSetLayoutKey );

	// get minimum number of descriptors of each type needed to fill all distinct DescriptorSetLayouts
	std::vector<VkDescriptorPoolSize> getVkDescriptorPoolSizes();

	// get number of descriptor sets
	uint32_t getNumDescriptorSets();

};	/* class ShaderManager */








}  /* namesapce vk */
}  /* namesapce of */