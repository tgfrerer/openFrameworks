#include "vk/ComputeCommand.h"
#include "vk/BufferAllocator.h"

using namespace of::vk;
// setup all non-transient state for this draw object

// current ubo values are stored with draw command

// think about it as immutable DATA versus STATE - we want immutable DATA
// not state. DATA is Plain Old Data - and this is how the draw command 
// must store itself.

// ----------------------------------------------------------------------


void ComputeCommand::setup( const ComputePipelineState& pipelineState ){

	mPipelineState = pipelineState;

	mDescriptorSetData = mPipelineState.getShader()->getDescriptorSetData();
	mUniformDictionary = mPipelineState.getShader()->getUniformDictionary();

}

// ------------------------------------------------------------

void ComputeCommand::commitUniforms( const std::unique_ptr<BufferAllocator>& alloc ){

	for ( auto & descriptorSetData : mDescriptorSetData ){

		auto imgInfoIt        = descriptorSetData.imageAttachment.begin();
		auto bufferInfoIt     = descriptorSetData.bufferAttachment.begin();
		auto dynamicOffsetsIt = descriptorSetData.dynamicBindingOffsets.begin();
		auto dataIt           = descriptorSetData.dynamicUboData.begin();

		for ( auto & descriptor : descriptorSetData.descriptors ){

			switch ( descriptor.type ){
			case ::vk::DescriptorType::eSampler:
				break;
			case ::vk::DescriptorType::eCombinedImageSampler:
			{
				descriptor.imageView   = imgInfoIt->imageView;
				descriptor.sampler     = imgInfoIt->sampler;
				descriptor.imageLayout = imgInfoIt->imageLayout;
				imgInfoIt++;
			}
			break;
			case ::vk::DescriptorType::eSampledImage:
				break;
			case ::vk::DescriptorType::eStorageImage:
				break;
			case ::vk::DescriptorType::eUniformTexelBuffer:
				break;
			case ::vk::DescriptorType::eStorageTexelBuffer:
				break;
			case ::vk::DescriptorType::eUniformBuffer:
				break;
			case ::vk::DescriptorType::eUniformBufferDynamic:
			{
				descriptor.buffer = alloc->getBuffer();
				::vk::DeviceSize offset;
				void * dataP = nullptr;

				const auto & dataVec = *dataIt;
				const auto & dataRange = dataVec.size();

				// allocate data on gpu
				if ( alloc->allocate( dataRange, offset ) && alloc->map( dataP ) ){

					// copy data to gpu
					memcpy( dataP, dataVec.data(), dataRange );

					// update dynamic binding offsets for this binding
					*dynamicOffsetsIt = offset;

				} else{
					ofLogError() << "commitUniforms: could not allocate transient memory.";
				}
				dataIt++;
				dynamicOffsetsIt++;
				descriptor.range = dataRange;
			}
			break;
			case ::vk::DescriptorType::eStorageBuffer:
				break;
			case ::vk::DescriptorType::eStorageBufferDynamic:
				descriptor.buffer = bufferInfoIt->buffer;
				descriptor.range  = bufferInfoIt->range;
				*dynamicOffsetsIt = bufferInfoIt->offset;

				bufferInfoIt++;
				dynamicOffsetsIt++;
				break;
			case ::vk::DescriptorType::eInputAttachment:
				break;
			default:
				break;
			} // end switch

		} // end for each descriptor
	} // end foreach mDescriptorSetData
}