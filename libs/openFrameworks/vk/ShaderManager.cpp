#include <vk/ShaderManager.h>
#include "ofLog.h"

// ----------------------------------------------------------------------
const std::map<uint32_t, std::shared_ptr<of::vk::Shader::DescriptorInfo>>& of::vk::ShaderManager::getBindings( uint64_t setLayoutHash )const{
	static std::map<uint32_t, std::shared_ptr<of::vk::Shader::DescriptorInfo>> failedBinding;
	const auto it = mBindingsPerSetStore.find( setLayoutHash );
	if ( mBindingsPerSetStore.end() != it ){
		return it->second;
	} else{
		ofLogError() << "Could not get binding for SetLayout hash: " << std::hex << setLayoutHash;
		return failedBinding;
	}
}

// ----------------------------------------------------------------------

bool of::vk::ShaderManager::createVkDescriptorSetLayouts(){

	for ( const auto& s:  mSetLayoutStore ){
		const auto & setLayoutHash = s.first;
		const auto & setLayoutMeta = *s.second;

		// create vkbinding description for each binding

		std::vector<VkDescriptorSetLayoutBinding> bindings;
		bindings.reserve( setLayoutMeta.bindingTable.size() );

		for ( const auto& bindingInfo : setLayoutMeta.bindingTable ){
			const auto& bindingNumber = bindingInfo.first;
			const auto& uniformHash   = bindingInfo.second;
			const auto& uniformMeta   = mDescriptorInfoStore[uniformHash];

			if ( uniformMeta == nullptr ){
				ofLogError() << "Cannot find uniform binding with id: " << std::hex << uniformHash << ".";
				return false;
			} 
			
			//----------| invariant: uniformMeta for this setLayoutHash was valid

			// Store / add this binding to central descriptorSet->bindings store
			// as this references an object held in mDescriptorInfoStore, the object's
			// use_count will increase with each reference, and tell how many 
			// copies of this uniform are needed.
			mBindingsPerSetStore[setLayoutHash].insert( { bindingNumber, uniformMeta } );

			bindings.push_back( {                                // VkDescriptorSetLayoutBinding: 
				bindingNumber,                                   // uint32_t                               binding;
				uniformMeta->type,                               // VkDescriptorType                       descriptorType;
				uniformMeta->count,                    // uint32_t                               count;
				uniformMeta->stageFlags,                         // VkShaderStageFlags                     stageFlags;
				nullptr,                                         // const VkSampler*                       pImmutableSamplers;
			} );
		}

		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{
			VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, // VkStructureType                        sType;
			nullptr,                                             // const void*                            pNext;
			0,                                                   // VkDescriptorSetLayoutCreateFlags       flags;
			static_cast<uint32_t>(bindings.size()),              // uint32_t                               bindingCount;
			bindings.data()                                      // const VkDescriptorSetLayoutBinding*    pBindings;
		};

		auto descriptorSetLayout = std::shared_ptr<VkDescriptorSetLayout>( new VkDescriptorSetLayout, 
			[device=mSettings.device](VkDescriptorSetLayout * lhs){
			if ( *lhs != nullptr ){
				vkDestroyDescriptorSetLayout( device, *lhs, nullptr );
			}
			delete lhs;
		} );

		auto err = vkCreateDescriptorSetLayout( mSettings.device, &descriptorSetLayoutInfo, nullptr, descriptorSetLayout.get() );
		assert( !err );
		mDescriptorSetLayoutStore[setLayoutHash] = std::move( descriptorSetLayout );
	}

	return true;
}

// ----------------------------------------------------------------------

const VkDescriptorSetLayout& of::vk::ShaderManager::getVkDescriptorSetLayout( uint64_t descriptorSetLayoutKey ){

	//! TODO: create descriptorSetLayout if possible.

	static VkDescriptorSetLayout failedLayout = nullptr;

	const auto & foundLayout = mDescriptorSetLayoutStore.find( descriptorSetLayoutKey );
	if ( foundLayout != mDescriptorSetLayoutStore.end() ){
		return *foundLayout->second;
	} else{
		ofLogError() << "No binding for DescriptorSetLayout key: " << std::hex << descriptorSetLayoutKey;
	}

	return failedLayout;
}

// ----------------------------------------------------------------------
std::vector<VkDescriptorPoolSize> of::vk::ShaderManager::getVkDescriptorPoolSizes(){

	// To know how many descriptors of each type to allocate, 
	// we group descriptors over all layouts by type and count each group.

	std::map<VkDescriptorType, uint32_t> poolSizeMap;

	for ( const auto& s : mSetLayoutStore ){
		const auto & hash          = s.first;
		const auto & setLayoutMeta = s.second;
		
		for ( const auto & b : setLayoutMeta->bindingTable ){
			const auto & bindingNumber = b.first;
			const auto & bindingHash   = b.second;

			auto & descriptorInfo = getDescriptorInfo( bindingHash );
			
			if ( descriptorInfo ){
				if ( poolSizeMap.end() == poolSizeMap.find( descriptorInfo->type ) ){
					poolSizeMap.insert( { descriptorInfo->type, descriptorInfo->count } );
				} else{
					poolSizeMap[descriptorInfo->type] += descriptorInfo->count;
				}
			}
		}
	}

	// flatten map into vector of VkDescriptorPoolSize

	std::vector<VkDescriptorPoolSize> poolSizes;
	poolSizes.reserve( poolSizeMap.size() );

	std::transform( poolSizeMap.cbegin(), poolSizeMap.cend(), std::back_inserter( poolSizes ), 
		[](const std::pair<VkDescriptorType,uint32_t>& lhs) -> VkDescriptorPoolSize {
		return{ lhs.first,lhs.second };
	} );

	return poolSizes;
}

// ----------------------------------------------------------------------

uint32_t of::vk::ShaderManager::getNumDescriptorSets(){
	return static_cast<uint32_t>(mDescriptorSetLayoutStore.size());
}

// ----------------------------------------------------------------------
