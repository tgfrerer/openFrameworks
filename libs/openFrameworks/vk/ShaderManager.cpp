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

		std::vector<::vk::DescriptorSetLayoutBinding> bindings;
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

			// Store / add this binding to central descriptorSet->bindings store.
			//
			// As this references an object held in mDescriptorInfoStore, the object's
			// use_count will increase with each reference, and tell how many 
			// copies of this uniform are needed.
			mBindingsPerSetStore[setLayoutHash].insert( { bindingNumber, uniformMeta } );

			// If uniform references a combined image sampler, we want to add a reference for this
			// sampler to the central registry.

			if ( uniformMeta->type == ::vk::DescriptorType::eCombinedImageSampler ){
				mTextureUsage[uniformMeta->name].push_back( setLayoutHash );
			}

			bindings.push_back( {                                // VkDescriptorSetLayoutBinding: 
				bindingNumber,                                   // uint32_t                               binding;
				uniformMeta->type,                               // VkDescriptorType                       descriptorType;
				uniformMeta->count,                              // uint32_t                               count;
				uniformMeta->stageFlags,                         // VkShaderStageFlags                     stageFlags;
				nullptr,                                         // const VkSampler*                       pImmutableSamplers;
			} );
		}

		auto descriptorSetLayoutInfo = ::vk::DescriptorSetLayoutCreateInfo();
		descriptorSetLayoutInfo
			.setBindingCount( bindings.size() )
			.setPBindings( bindings.data())
			;

		auto descriptorSetLayout = std::shared_ptr<::vk::DescriptorSetLayout>( new ::vk::DescriptorSetLayout, 
			[device=mSettings.device](::vk::DescriptorSetLayout * lhs){
			if ( *lhs ){
				device.destroyDescriptorSetLayout( *lhs );
			}
			delete lhs;
		} );

		*descriptorSetLayout = mSettings.device.createDescriptorSetLayout( descriptorSetLayoutInfo );

		mDescriptorSetLayoutStore[setLayoutHash] = std::move( descriptorSetLayout );
	}

	return true;
}

// ----------------------------------------------------------------------

const ::vk::DescriptorSetLayout& of::vk::ShaderManager::getVkDescriptorSetLayout( uint64_t descriptorSetLayoutKey ){

	//! TODO: create descriptorSetLayout if possible.

	static ::vk::DescriptorSetLayout failedLayout;

	const auto & foundLayout = mDescriptorSetLayoutStore.find( descriptorSetLayoutKey );
	if ( foundLayout != mDescriptorSetLayoutStore.end() ){
		return *foundLayout->second;
	} else{
		ofLogError() << "No binding for DescriptorSetLayout key: " << std::hex << descriptorSetLayoutKey;
	}

	return failedLayout;
}

// ----------------------------------------------------------------------

std::vector<::vk::DescriptorPoolSize> of::vk::ShaderManager::getVkDescriptorPoolSizes(){

	updatePoolSizesPerDescriptorSetCache();
	
	std::vector<::vk::DescriptorPoolSize> poolSizes;

	for ( const auto & sizePair : mPoolSizesPerDescriptorSetCache ){
		const auto & key = sizePair.first;
		const auto & poolSizeVec = sizePair.second;
		poolSizes.insert( poolSizes.end(), poolSizeVec.cbegin(), poolSizeVec.cend() );
	}

	return poolSizes;
}

// ----------------------------------------------------------------------

void of::vk::ShaderManager::updatePoolSizesPerDescriptorSetCache(){
	
	// To know how many descriptors of each type to allocate, 
	// we group descriptors over all layouts by type and count each group.

	for ( const auto& s : mSetLayoutStore ){
		const auto & hash = s.first;
		const auto & setLayoutMeta = s.second;

		std::map<::vk::DescriptorType, uint32_t> poolSizeMap;

		for ( const auto & b : setLayoutMeta->bindingTable ){
			const auto & bindingNumber = b.first;
			const auto & bindingHash = b.second;

			auto & descriptorInfo = getDescriptorInfo( bindingHash );

			if ( descriptorInfo ){
				if ( poolSizeMap.end() == poolSizeMap.find( descriptorInfo->type ) ){
					poolSizeMap.insert( { descriptorInfo->type, descriptorInfo->count } );
				} else{
					poolSizeMap[descriptorInfo->type] += descriptorInfo->count;
				}
			}
		}
		
		// flatten map into vector of VkDescriptorPoolSize

		std::vector<::vk::DescriptorPoolSize> poolSizes;
		poolSizes.reserve( poolSizeMap.size() );

		std::transform( poolSizeMap.cbegin(), poolSizeMap.cend(), std::back_inserter( poolSizes ),
			[]( const std::pair<::vk::DescriptorType, uint32_t>& lhs ) -> ::vk::DescriptorPoolSize {
			return{ lhs.first,lhs.second };
		} );

		mPoolSizesPerDescriptorSetCache[hash] = poolSizes;
	}


}

// ----------------------------------------------------------------------

uint32_t of::vk::ShaderManager::getNumDescriptorSets(){
	return static_cast<uint32_t>(mDescriptorSetLayoutStore.size());
}

// ----------------------------------------------------------------------
