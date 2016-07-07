#include "vk/Shader.h"
#include "ofLog.h"
#include "ofFileUtils.h"

void of::vk::Shader::reflectShaderResources()
{

	/*
	
	when we analyse shader uniform resouces, we first need to group bindings by set.

	The smallest unit we bind in Vulkan are sets - and for each set we will have to specify offsets for each descriptor.
	
	set - binding 0 - descriptor 0
	    - binding 1 - decriptors[] 1,2,3
	    - <empty>
	    - binding 3 - descriptor 4
	
	there does not seem to be a maximum number of descriptor that we can allocate - as long as we don't use them all at the same time

	is there a maximum number of descriptorsets that we can allocate?
	
	*/





	// for all shader stages
	for ( auto &c : mCompilers ){

		auto & compiler = *c.second;
		auto & shaderStage = c.first;

		if ( shaderStage & VK_SHADER_STAGE_VERTEX_BIT ){
			ofLog() << std::endl << "Vertex Stage" << endl << string( 70, '-' );
		} else if ( shaderStage & VK_SHADER_STAGE_FRAGMENT_BIT ){
			ofLog() << std::endl << "Fragment Stage" << endl << string( 70, '-' );
		}

		auto shaderResources = compiler.get_shader_resources();

		shaderResources.uniform_buffers.size();

		// --- uniform buffers ---

		// we need to build a unique list of uniforms
		// and make sure that uniforms with the same name 
		// refer to the same binding number and set index.
		//
		// also if a uniform is referred to by more than one
		// shader stages this needs to be updated in the uniform's 
		// accessibility stage flags.

		for ( auto & ubo : shaderResources.uniform_buffers ){
			ostringstream os;

			uint32_t descriptor_set = 0;
			uint32_t bindingNumber = 0;

			// get a bitmask representing uniform decorations 
			uint64_t decorationMask = compiler.get_decoration_mask( ubo.id );
			
			// get the storage type for this ubo
			auto storageType = compiler.get_type( ubo.type_id );
			// get the storageSize (in bytes) for this ubo
			uint32_t storageSize = compiler.get_declared_struct_size( storageType );

			if ( ( 1ull << spv::DecorationDescriptorSet ) & decorationMask ){
				descriptor_set = compiler.get_decoration( ubo.id, spv::DecorationDescriptorSet );
				os << ", set = " << descriptor_set;
			} else{
				ofLogWarning() << "shader uniform " << ubo.name << "does not specify set id";
				// If undefined, set descriptor set id to 0. This is conformant with:
				// https://www.khronos.org/registry/vulkan/specs/misc/GL_KHR_vulkan_glsl.txt
				descriptor_set = 0;
			}

			if ( ( 1ull << spv::DecorationBinding ) & decorationMask ){
				bindingNumber = compiler.get_decoration( ubo.id, spv::DecorationBinding );
				os << ", binding = " << bindingNumber;
			} else{
				ofLogWarning() << "shader uniform" << ubo.name << "does not specify binding number.";
			}

			ofLog() << "Uniform Block: '" << ubo.name << "'" << os.str();

			auto type = compiler.get_type( ubo.type_id );
				   
			// type for ubo descriptors is struct
			// such structs will have member types, that is, they have elements within.
			for ( uint32_t tI = 0; tI != type.member_types.size(); ++tI ){
				auto mn = compiler.get_member_name( ubo.type_id, tI );
				ofLog() << "\\-" << "[" << tI << "] : " << mn;
			}

			{
				// let's look up if the current block name already exists in the 
				// table of bindings for this shader, and if necessary update
				// the shader stage flags to permit access to all stages that need it:

				// shaderStage defines from which shader stages this layout is accessible
				VkShaderStageFlags layoutAccessibleFromStages = shaderStage;

				VkDescriptorSetLayoutBinding  newBinding{
					bindingNumber,                                        // uint32_t              binding;
					VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,            // VkDescriptorType      descriptorType;
					1,                  // <- check:array?                // uint32_t              descriptorCount;
					layoutAccessibleFromStages,                           // VkShaderStageFlags    stageFlags;
					nullptr,                                              // const VkSampler*      pImmutableSamplers;
				};

				if ( mUniforms.find( ubo.name ) != mUniforms.end() ){
					// we have found a binding with the same name in another shader stage.
					// therefore we: 
					// 1.) need to update the binding accessiblity flag
					// 2.) do some error checking to make sure the binding is the same.

					auto& existingBinding = mUniforms[ubo.name];
					if ( existingBinding.set != descriptor_set
						|| existingBinding.binding.binding != bindingNumber ){
						ofLogError() << "Incompatible bindings between shader stages: " << ubo.name;
					} else{
						// all good, make sure the binding is also accessible in the 
						// current stage.
						newBinding.stageFlags |= existingBinding.binding.stageFlags;
					}
				}

				mUniforms[ubo.name] = { descriptor_set, newBinding, storageSize };
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
			mVertexInfo.bindingDescription.resize( shaderResources.stage_inputs.size() );

			for ( uint32_t i = 0; i != shaderResources.stage_inputs.size(); ++i ){

				auto & attributeInput = shaderResources.stage_inputs[i];
				auto attributeType = compiler.get_type( attributeInput.type_id );

				uint32_t location = i; // shader location qualifier mapped to binding number

				if ( ( 1ull << spv::DecorationLocation ) & compiler.get_decoration_mask( attributeInput.id ) ){
					location = compiler.get_decoration( attributeInput.id, spv::DecorationLocation );
				}

				ofLog() << "Vertex Attribute loc=[" << location << "] : " << attributeInput.name;

				// Binding Description: Describe how to read data from buffer based on binding number
				mVertexInfo.bindingDescription[i].binding = location;  // which binding number we are describing
				mVertexInfo.bindingDescription[i].stride = ( attributeType.width / 8 ) * attributeType.vecsize * attributeType.columns;
				mVertexInfo.bindingDescription[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

				// Attribute description: Map shader location to pipeline binding number
				mVertexInfo.attribute[i].location = location;   // .location == which shader attribute location
				mVertexInfo.attribute[i].binding = location;    // .binding  == pipeline binding number == where attribute takes data from

				switch ( attributeType.vecsize ){
				case 3:
					mVertexInfo.attribute[i].format = VK_FORMAT_R32G32B32_SFLOAT;	     // 3-part float
					break;
				case 4:
					mVertexInfo.attribute[i].format = VK_FORMAT_R32G32B32A32_SFLOAT;	 // 4-part float
					break;
				default:
					break;
				}
			}

			VkPipelineVertexInputStateCreateInfo vi{
				VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, // VkStructureType                             sType;
				nullptr,                                                   // const void*                                 pNext;
				0,                                                         // VkPipelineVertexInputStateCreateFlags       flags;
				mVertexInfo.bindingDescription.size(),                     // uint32_t                                    vertexBindingDescriptionCount;
				mVertexInfo.bindingDescription.data(),                     // const VkVertexInputBindingDescription*      pVertexBindingDescriptions;
				mVertexInfo.attribute.size(),                              // uint32_t                                    vertexAttributeDescriptionCount;
				mVertexInfo.attribute.data()                               // const VkVertexInputAttributeDescription*    pVertexAttributeDescriptions;
			};

			mVertexInfo.vi = std::move( vi );

		} // end shaderStage & VK_SHADER_STAGE_VERTEX_BIT
	}  // end for : mCompilers
};


// ----------------------------------------------------------------------

of::vk::Shader::Shader( const Settings & settings_ )
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
			spirCode.size() * sizeof( uint32_t ),	                    // size_t                       codeSize;
			spirCode.data()                                             // const uint32_t*              pCode;
		};

		auto err = vkCreateShaderModule( mSettings.device, &info, nullptr, &module );

		if ( err == VK_SUCCESS ){

			mModules[s.first] = module;

			VkPipelineShaderStageCreateInfo shaderStage{
				VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,    // VkStructureType                     sType;
				nullptr,	                                            // const void*                         pNext;
				0,	                                                    // VkPipelineShaderStageCreateFlags    flags;
				s.first,	                                            // VkShaderStageFlagBits               stage;
				module,	                                                // VkShaderModule                      module;
				"main",	                                                // const char*                         pName;
				nullptr	                                                // const VkSpecializationInfo*         pSpecializationInfo;
			};

			mStages.push_back( std::move( shaderStage ) );
			// move the ir code buffer into the shader compiler
			mCompilers[s.first] = make_shared<spirv_cross::Compiler>( std::move( spirCode ) );
		} else{
			ofLog() << "Error creating shader module: " << s.second;
		}
	}  // for : mSettings.sources
	reflectShaderResources();
	//mPipelineLayout = createPipelineLayout();
}
