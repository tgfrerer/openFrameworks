#include "vk/DrawCommand.h"
#include "vk/RenderBatch.h"

// setup all non-transient state for this draw object

// current ubo values are stored with draw command

// think about it as immutable DATA versus STATE - we want immutable DATA
// not state. DATA is Plain Old Data - and this is how the draw command 
// must store itself.

// ----------------------------------------------------------------------

of::DrawCommand::DrawCommand( const DrawCommandInfo & dcs )
	:mDrawCommandInfo( dcs ){

	// Initialise Ubo blobs with default values, based on 
	// default values received from Shader. 
	//
	// Shader should provide us with values to initialise, because
	// these values depend on the shader, and the shader knows the
	// uniform variable types.

	const auto & descriptorSetsInfo = mDrawCommandInfo.getPipeline().getShader()->getDescriptorSetsInfo();

	mDescriptorSetData.reserve( descriptorSetsInfo.size() );
	
	for ( auto&di : descriptorSetsInfo ){
		DescriptorSetData_t descriptorSetData;
		
		auto & bindingsVec = descriptorSetData.descriptorBindings;

		bindingsVec.reserve(di.bindings.size());
		
		size_t numDynamicUbos = 0;

		for ( auto & binding : di.bindings ){
			if ( binding.descriptorType == ::vk::DescriptorType::eUniformBufferDynamic ){
				++numDynamicUbos;
			}
			for ( uint32_t aI = 0; aI != binding.descriptorCount; ++aI ){
				DescriptorSetData_t::DescriptorData_t bindingData;
				bindingData.arrayIndex = aI;
				bindingData.type = binding.descriptorType;
				bindingsVec.emplace_back( std::move( bindingData ) );
			}
		}

		descriptorSetData.dynamicBindingOffsets.resize( numDynamicUbos, 0 );
		mDescriptorSetData.emplace_back( std::move( descriptorSetData ) );
	}


}
