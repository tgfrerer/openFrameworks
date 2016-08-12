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

	// central store of uniforms - indexed by uniform hash
	std::map<uint64_t, std::shared_ptr<of::vk::Shader::UniformMeta>>   mUniformStore;
	
	// central store of set layouts - indexed by set layout hash
	// set layouts are ordered sequences of uniforms
	std::map<uint64_t, std::shared_ptr<of::vk::Shader::SetLayoutMeta>> mSetLayoutStore;

public:

	// TODO: these methods - as they may modify the contents of ShaderManager
	// may need to be mutexed to be safe across threads.
	std::shared_ptr<of::vk::Shader::UniformMeta>& borrowUniformMeta( uint64_t hash ){
		return mUniformStore[hash];
	}

	const std::shared_ptr<of::vk::Shader::UniformMeta>& getUniformMeta( uint64_t hash ) const{
		static std::shared_ptr<of::vk::Shader::UniformMeta> failUniformMeta;
		const auto &it = mUniformStore.find( hash );
		if ( it != mUniformStore.end() ){
			return it->second;
		} else{
			ofLogError() << "Could not find uniform with id: " << std::hex << hash;
		}
		return failUniformMeta;
	}

	std::shared_ptr<of::vk::Shader::SetLayoutMeta>& borrowSetLayoutMeta( uint64_t hash ){
		return mSetLayoutStore[hash];
	}

	const std::map<uint32_t, std::shared_ptr<of::vk::Shader::UniformMeta>>& getBindings( uint64_t setLayoutHash ) const;
	
	const std::map<uint64_t, std::shared_ptr<of::vk::Shader::UniformMeta>>& getUniformMeta() const{
		return mUniformStore;
	};


private:

	// central store of VkDescriptorSetLayouts, indexed by corresponding SetLayoutMeta hash
	std::map<uint64_t, std::shared_ptr<VkDescriptorSetLayout>> mDescriptorSetLayoutStore;

	// central store of bindings per descriptor set layout, indexed by descriptor set layout hash
	std::map<uint64_t, std::map<uint32_t, std::shared_ptr<of::vk::Shader::UniformMeta>>> mBindingsPerSetStore;
public:

	// create VkDescriptorSetLayouts for all descriptors currently held in mSetLayoutStore
	bool createVkDescriptorSetLayouts();

	const VkDescriptorSetLayout& getVkDescriptorSetLayout( uint64_t descriptorSetLayoutKey );

	// get number of descriptors of each type needed to fill all distinct DescriptorSetLayouts
	std::vector<VkDescriptorPoolSize> getVkDescriptorPoolSizes();

	// get number of descriptor sets
	uint32_t getNumDescriptorSets();

};	/* class ShaderManager */








}  /* namesapce vk */
}  /* namesapce of */