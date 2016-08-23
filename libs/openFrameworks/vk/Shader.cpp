#include "vk/Shader.h"
#include "vk/ShaderManager.h"
#include "ofLog.h"
#include "ofAppRunner.h"
#include "ofFileUtils.h"
#include "spooky/SpookyV2.h"
#include "shaderc/shaderc.hpp"
#include <algorithm>

// ----------------------------------------------------------------------

namespace of{ 
namespace utils{

// static utility method : no-op on non-WIN32 system. 
void setConsoleColor( uint32_t colour = 12 ){
#ifdef WIN32
	static HANDLE hConsole = GetStdHandle( STD_OUTPUT_HANDLE );
	SetConsoleTextAttribute( hConsole, colour + 0 * 16 );
#endif 
}

// reset console color
// static utility method : no-op on non-WIN32 system. 
void resetConsoleColor(){
#ifdef WIN32
	static HANDLE hConsole = GetStdHandle( STD_OUTPUT_HANDLE );
	SetConsoleTextAttribute( hConsole, 7 + 0 * 16 );
#endif 
}

} /*namespace vk*/ 
} /*namespace of*/


// ----------------------------------------------------------------------

of::vk::Shader::Shader( const Settings & settings_ )
	: mSettings( settings_ )
{
	//if ( mShaderManager == nullptr ){
	//	auto & renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );
	//	const_cast<std::shared_ptr<ShaderManager>&>(mShaderManager) = renderer->getShaderManager();
	//}
	compile();
}

// ----------------------------------------------------------------------

const uint64_t of::vk::Shader::getShaderCodeHash(){
	if ( mShaderHashDirty ){
		std::vector<uint64_t> spirvHashes;
		spirvHashes.reserve( mSpvHash.size() );
		for ( const auto&k : mSpvHash ){
			spirvHashes.push_back( k.second );
		}
		mShaderHash = SpookyHash::Hash64( spirvHashes.data(), spirvHashes.size() * sizeof( uint64_t ), 0 );
		mShaderHashDirty = false;
	}
	return mShaderHash;
}

// ----------------------------------------------------------------------

void of::vk::Shader::compile(){
	bool shaderDirty = false;
	
	for ( auto & source : mSettings.sources ){

		const auto & shaderType = source.first;
		const auto & filename = source.second;

		if ( !ofFile( filename ).exists() ){
			ofLogFatalError() << "Shader file not found: " << source.second;
			ofExit(1);
			return;
		}

		std::vector<uint32_t> spirCode;
		bool success = getSpirV( shaderType, filename, spirCode );	/* load or compiles code into spirCode */

		if ( !success){
			if (!mShaderStages.empty()){
				ofLogError() << "Aborting shader compile. Using previous version of shader instead";
				return;
			} else{
				// !TODO: should we use a default shader, then?
				ofLogFatalError() << "Shader did not compile: " << filename;
				ofExit( 1 );
			}
		} 

		uint64_t spirvHash = SpookyHash::Hash64( reinterpret_cast<char*>( spirCode.data() ), spirCode.size() * sizeof( uint32_t ), 0 );

		bool spirCodeDirty = isSpirCodeDirty( shaderType, spirvHash );

		if ( spirCodeDirty ){
			ofLog() << "Building shader module: " << filename;
			createVkShaderModule( shaderType, spirCode );
			// store hash in map so it does not appear dirty
			mSpvHash[shaderType] = spirvHash;
			// move the ir code buffer into the shader compiler
			mSpvCrossCompilers[shaderType] = make_shared<spirv_cross::Compiler>( std::move( spirCode ) );
		}

		shaderDirty |= spirCodeDirty;
		mShaderHashDirty |= spirCodeDirty;
	}

	if ( shaderDirty ){
		reflect( mSpvCrossCompilers, mVertexInfo );
		createSetLayouts();
	}
	
}

// ----------------------------------------------------------------------

bool of::vk::Shader::isSpirCodeDirty( const VkShaderStageFlagBits shaderStage, uint64_t spirvHash ){

	if ( mSpvHash.find( shaderStage ) == mSpvHash.end() ){
		// hash not found so must be dirty
		return true;
	} else{
		return ( mSpvHash[shaderStage] != spirvHash );
	}
	
	return false;
}


// ----------------------------------------------------------------------

bool of::vk::Shader::getSpirV( const VkShaderStageFlagBits shaderStage, const std::string & fileName, std::vector<uint32_t> &spirCode ){
	
	auto f = ofFile( fileName );
	auto fExt = f.getExtension();

	if ( fExt == "spv" ){
		ofBuffer fileBuf = ofBufferFromFile( fileName, true );
		ofLogNotice() << "Loading SPIR-V shader module: " << fileName;
		auto a = fileBuf.getData();
		spirCode.assign(
			reinterpret_cast<uint32_t*>( fileBuf.getData() ),
			reinterpret_cast<uint32_t*>( fileBuf.getData() ) + fileBuf.size() / sizeof( uint32_t )
		);
		return true;
	} else {
		shaderc_shader_kind shaderType = shaderc_shader_kind::shaderc_glsl_infer_from_source;

		switch ( shaderStage ){
		case VK_SHADER_STAGE_VERTEX_BIT:
			shaderType = shaderc_shader_kind::shaderc_glsl_default_vertex_shader;
			break;
		case VK_SHADER_STAGE_FRAGMENT_BIT:
			shaderType = shaderc_shader_kind::shaderc_glsl_default_fragment_shader;
			break;
		default:
			break;
		}

		bool success = true;

		ofBuffer fileBuf = ofBufferFromFile( fileName, true );
		shaderc::Compiler compiler;
		shaderc::CompileOptions options;

		// Like -DMY_DEFINE=1
		// options.AddMacroDefinition( "MY_DEFINE", "1" );

		shaderc::SpvCompilationResult module = compiler.CompileGlslToSpv(
			fileBuf.getData(), fileBuf.size(), shaderType, fileName.c_str(), options );

		if ( module.GetCompilationStatus() != shaderc_compilation_status_success ){
			std::string errorMessage = module.GetErrorMessage();
			ofLogError() << "Shader compile failed for: " << fileName;

			of::utils::setConsoleColor( 12 /* red */ );
			ofLogError() << std::endl << errorMessage;
			of::utils::resetConsoleColor();
			 
			// Error string will have the form:  "triangle.frag:28: error: '' :  syntax error"

			ostringstream scanString; /* create a scan string with length of first element known: "%*23s : %d :" */
			scanString << "%*" << fileName.size() << "s : %d :"; 
			
			uint32_t lineNumber = 0; /* <- Will contain error line number after successful parse */
			auto scanResult = sscanf( errorMessage.c_str(), scanString.str().c_str(), &lineNumber );

			if ( scanResult != std::char_traits<wchar_t>::eof() ){
				auto & lineIt = fileBuf.getLines().begin();
				size_t currentLine = 1; /* Line numbers start counting at 1 */

				while (lineIt != fileBuf.getLines().end()){

					if ( currentLine >= lineNumber - 3 ){
						ostringstream sourceContext;
						const auto shaderSourceCodeLine = lineIt.asString();
						sourceContext << std::right << std::setw(4) << currentLine << " | " << shaderSourceCodeLine;
						
						if ( currentLine == lineNumber ) of::utils::setConsoleColor( 11 );
						ofLogError() << sourceContext.str();
						if ( currentLine == lineNumber ) of::utils::resetConsoleColor();
					}

					if ( currentLine >= lineNumber + 2 ){
						ofLogError(); // add empty for better readability
						break;
					}

					++lineIt;
					++currentLine;
				}
			}

			return false;
		} else{
			spirCode.clear();
			spirCode.assign( module.cbegin(), module.cend() );
			// ofLogNotice() << "OK \tShader compile: " << fileName;
			return true;
		}
		
		assert( success );
	}
}

// ----------------------------------------------------------------------

void of::vk::Shader::createVkShaderModule( const VkShaderStageFlagBits shaderType, const std::vector<uint32_t> &spirCode ){

	VkShaderModuleCreateInfo info{
		VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,	            // VkStructureType              sType;
		nullptr,                                                    // const void*                  pNext;
		0,	                                                        // VkShaderModuleCreateFlags    flags;
		spirCode.size() * sizeof( uint32_t ),                       // size_t                       codeSize;
		spirCode.data()                                             // const uint32_t*              pCode;
	};

	auto & deviceHandle = mShaderManager->mSettings.device;

	VkShaderModule module;
	auto err = vkCreateShaderModule( deviceHandle, &info, nullptr, &module );
	assert( !err );

	auto tmpShaderStage = std::shared_ptr<ShaderStage>( new ShaderStage, [device = deviceHandle](ShaderStage* lhs){
		vkDestroyShaderModule( device, lhs->module, nullptr );
		delete lhs;
	} );

	tmpShaderStage->module = module;
	tmpShaderStage->createInfo = {
		VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,    // VkStructureType                     sType;
		nullptr,                                                // const void*                         pNext;
		0,	                                                    // VkPipelineShaderStageCreateFlags    flags;
		shaderType,                                             // VkShaderStageFlagBits               stage;
		tmpShaderStage->module,                                 // VkShaderModule                      module;
		"main",                                                 // const char*                         pName;
		nullptr                                                 // const VkSpecializationInfo*         pSpecializationInfo;
	};
	mShaderStages[shaderType] = std::move( tmpShaderStage );
}

// ----------------------------------------------------------------------

void of::vk::Shader::reflect( 
	const std::map<VkShaderStageFlagBits, std::shared_ptr<spirv_cross::Compiler>>& compilers, 
	VertexInfo& vertexInfo
){
	// storage for reflected information about UBOs

	// for all shader stages
	for ( auto &c : compilers ){

		auto & compiler    = *c.second;
		auto & shaderStage = c.first;

		// ! TODO: process texture samplers
		// This: http://gpuopen.com/wp-content/uploads/2016/03/VulkanFastPaths.pdf
		// suggests one fast path is to bind all (!) textures into ONE DescriptorSet / binding 
		// as an array of textures, and then use pushConstants to fetch the index 
		// into the array for the texture we want for this particular draw. 
		// This would mean to create one descriptor per texture and to bind all these 
		// texture descriptors to one binding - and to one descriptorset.

		// --- uniform buffers ---
		reflectUBOs( compiler, shaderStage );
		
		// --- samplers
		reflectSamplers( compiler, shaderStage );

		// --- vertex inputs ---
		if ( shaderStage & VK_SHADER_STAGE_VERTEX_BIT ){
			reflectVertexInputs(compiler, vertexInfo );
		} 
		
	}  

	// print binding information to the console

	struct BindingForLog
	{
		uint32_t setNumber;
		uint32_t bindingNumber;
		uint64_t uniformHash;
	};

	std::vector<BindingForLog> bindingForLog;
	bindingForLog.reserve( mBindingsTable.size() );

	for ( const auto& b : mBindingsTable ){
		const auto & hash = b.first;
		const auto & bindingTable = b.second;
		bindingForLog.push_back( { bindingTable.setNumber, bindingTable.bindingNumber, hash } );
	}

	std::sort( bindingForLog.begin(), bindingForLog.end(), [](const BindingForLog & lhs, const BindingForLog & rhs)->bool{
		return ( lhs.setNumber < rhs.setNumber || ( lhs.setNumber == rhs.setNumber && lhs.bindingNumber < rhs.bindingNumber ) );
	} );

	ofLog() << "Uniform Bindings:";
	uint32_t setNumber = -1;
	for ( const auto &b : bindingForLog ){
		if ( setNumber != b.setNumber ){
			ofLog() << "Set [" << std::setw( 2 ) << b.setNumber << "]";
			setNumber = b.setNumber;
		}
		ofLog() << " " << char( 195 ) 
			<< std::setw( 2 ) << b.bindingNumber << " : " 
			<< std::hex << b.uniformHash 
			<< " '" << mShaderManager->getDescriptorInfo( b.uniformHash )->name << "'";
	}

}

// ----------------------------------------------------------------------

bool of::vk::Shader::reflectUBOs( const spirv_cross::Compiler & compiler, const VkShaderStageFlagBits & shaderStage ){

	auto uniformBuffers = compiler.get_shader_resources().uniform_buffers;

	for ( const auto & ubo : uniformBuffers ){

		auto tmpUniform         = std::make_shared<DescriptorInfo>();
		tmpUniform->count       = 1; // must be 1 for UBOs (arrays of UBOs are forbidden by the spec.)
		tmpUniform->name        = ubo.name;
		tmpUniform->storageSize = compiler.get_declared_struct_size( compiler.get_type( ubo.type_id ) );
		tmpUniform->type        = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; /* All our uniform buffer are dynamic */
		tmpUniform->stageFlags  = shaderStage;

		auto bufferRanges = compiler.get_active_buffer_ranges( ubo.id );

		for ( const auto &r : bufferRanges ){
			// Note that SpirV-Cross will only tell us the ranges of *actually used* members within an UBO. 
			// By merging the ranges later, we effectively also create aliases for member names which are 
			// not consistently named the same.
			auto memberName = compiler.get_member_name( ubo.type_id, r.index );
			tmpUniform->memberRanges[memberName] = { r.offset, r.range };
		}

		tmpUniform->calculateHash();

		// Let's see if an uniform buffer with this fingerprint has already been seen.
		// If yes, it would already be in uniformStore.

		auto & uniform = mShaderManager->borrowDescriptorInfo( tmpUniform->hash );

		if ( uniform == nullptr ){
			// Write uniform into store
			uniform = std::move( tmpUniform );
		} else{
			// Uniform with this key already exists.
			if ( uniform->storageSize != tmpUniform->storageSize ){
				ofLogError() << "Ubo: '" << uniform->name << "' re-defined with incompatible storage size.";
				// !TODO: try to recover.
				return false;
			} else{
				// Merge stage flags
				uniform->stageFlags |= tmpUniform->stageFlags;
				// Merge memberRanges
				ostringstream overlapMsg;
				if ( uniform->checkMemberRangesOverlap( uniform->memberRanges, tmpUniform->memberRanges, overlapMsg ) ){
					// member ranges overlap: print diagnostic message
					ofLogWarning() << "Inconsistency found parsing UBO: '" << ubo.name << "': " << std::endl << overlapMsg.str();
				}
				uniform->memberRanges.insert( tmpUniform->memberRanges.begin(), tmpUniform->memberRanges.end() );
			}
		}

		if ( false == addResourceToBindingsTable( compiler, ubo, uniform ) ){
			ofLogError() << "Could not add uniform to bindings table.";
			return false;
		}

	} // end: for all uniform buffers

	return true;
}

// ----------------------------------------------------------------------

bool of::vk::Shader::reflectSamplers( const spirv_cross::Compiler & compiler, const VkShaderStageFlagBits & shaderStage ){

	auto sampledImages = compiler.get_shader_resources().sampled_images;

	for ( const auto & sampledImage : sampledImages){

		auto tmpUniform = std::make_shared<DescriptorInfo>();
		tmpUniform->count = 1; //!TODO: find out how to query array size
		tmpUniform->name = sampledImage.name;
		tmpUniform->storageSize = 0; // sampled image is an opaque type and has no size
		tmpUniform->type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		tmpUniform->stageFlags = shaderStage;


		tmpUniform->calculateHash();

		// Let's see if an uniform buffer with this fingerprint has already been seen.
		// If yes, it would already be in uniformStore.

		auto & uniform = mShaderManager->borrowDescriptorInfo( tmpUniform->hash );

		if ( uniform == nullptr ){
			// Write uniform into store
			uniform = std::move( tmpUniform );
		} else{
			// Uniform with this key already exists.
		}

		if ( false == addResourceToBindingsTable( compiler, sampledImage, uniform ) ){
			ofLogError() << "Could not add uniform to bindings table.";
			return false;
		}

	} // end: for all uniform buffers

	return true;
}

// ----------------------------------------------------------------------

bool of::vk::Shader::addResourceToBindingsTable( const spirv_cross::Compiler & compiler, const spirv_cross::Resource & ubo, std::shared_ptr<of::vk::Shader::DescriptorInfo> & uniform ){
	
	// uniform holds our current uniform

	UniformBindingInfo bindingInfo;

	getSetAndBindingNumber( compiler, ubo, bindingInfo.setNumber, bindingInfo.bindingNumber );

	// Now store the set and binding information for this shader
	// we use this to check for consistent set and binding decorations for uniforms,
	// and later, to create our descriptorSetLayout

	auto localBindingIt = mBindingsTable.find( uniform->hash );
	if ( localBindingIt == mBindingsTable.end() ){
		// not yet seen this binding - store it
		mBindingsTable[uniform->hash] = bindingInfo;
	} else{
		bool success = true;
		if ( localBindingIt->second.setNumber != bindingInfo.setNumber ){
			ofLogError() << "Ubo: '" << uniform->name << "' set number mismatch: " << bindingInfo.setNumber;
			success = false;
		} else if ( localBindingIt->second.bindingNumber != bindingInfo.bindingNumber ){
			ofLogError() << "Ubo: '" << uniform->name << "' binding number mismatch: " << bindingInfo.bindingNumber;
			success = false;
		}
		if ( !success ){
			ofLogError() << "Binding and set number must match for Ubo member over all stages within the same shader";
			ofLogError() << "Check if your set ids and binding numbers are consistent for: " << ubo.name;
			return false;
		}
	}
	return true;
}

// ----------------------------------------------------------------------

bool of::vk::Shader::createSetLayouts(){
	
	// Create set layouts based on shader local 
	// bindings table, mBindingsTable.

	// First translate mBindingsTable into a map of SetLayoutMeta,
	// indexed by set number.

	// SetLayoutMeta (= set layout binding tables) indexed by set id
	map<uint32_t, std::shared_ptr<SetLayoutMeta>> descriptorSetLayoutsOderedBySetNumber;

	for ( const auto & b : mBindingsTable ){
		const auto & bindingHash   = b.first;
		const auto & setNumber     = b.second.setNumber;
		const auto & bindingNumber = b.second.bindingNumber;
		
		auto & setLayoutMeta = descriptorSetLayoutsOderedBySetNumber[setNumber];
		
		if ( setLayoutMeta == nullptr ){
			setLayoutMeta = std::make_shared<SetLayoutMeta>();
		}
		setLayoutMeta->bindingTable.insert( { bindingNumber, bindingHash } );
	}

	
	// Also build up mPipelineLayoutMeta, a vector which describes
	// sequence of DescriptorSetMeta keys used to create 
	// Pipeline Layout for this shader.

	mPipelineLayoutMeta.resize( descriptorSetLayoutsOderedBySetNumber.size() );
	mPipelineLayoutPtrsMeta.resize( descriptorSetLayoutsOderedBySetNumber.size() );

	for ( auto&s: descriptorSetLayoutsOderedBySetNumber ){
		const auto & setNumber     = s.first;
		auto       & createdLayout = s.second;
		createdLayout->calculateHash();

		// Store newly created SetLayoutMeta in ShaderManager 
		// (if it donesn't already exist there)
		auto & storedLayout = mShaderManager->borrowSetLayoutMeta(createdLayout->hash);
		
		if ( storedLayout == nullptr ){
			storedLayout = std::move( createdLayout );
			ofLogNotice() << "Created new Descriptor Set Layout";
		}
		
		mPipelineLayoutPtrsMeta[setNumber] = storedLayout ;
		mPipelineLayoutMeta[setNumber]     =  storedLayout->hash ;
		ofLogNotice() << "DescriptorSetLayout: " << std::hex << storedLayout->hash << " use count: " << storedLayout.use_count() -1; /*subtract 1 for shadermanager using it too.*/

	}

	return true;
}
// ----------------------------------------------------------------------

void of::vk::Shader::getSetAndBindingNumber( const spirv_cross::Compiler & compiler, const spirv_cross::Resource & resource, uint32_t &descriptor_set, uint32_t &bindingNumber ){
	// see what kind of decorations this resource has
	uint64_t decorationMask = compiler.get_decoration_mask( resource.id );
	
	if ( ( 1ull << spv::DecorationDescriptorSet ) & decorationMask ){
		descriptor_set = compiler.get_decoration( resource.id, spv::DecorationDescriptorSet );
	} else{
		// If undefined, set descriptor set id to 0. This is conformant with:
		// https://www.khronos.org/registry/vulkan/specs/misc/GL_KHR_vulkan_glsl.txt
		descriptor_set = 0;
		ofLogWarning() 
			<< "Warning: Shader uniform " << resource.name << "does not specify set id, and will " << endl
			<< "therefore be mapped to set 0 - this could have unintended consequences.";
	}

	if ( ( 1ull << spv::DecorationBinding ) & decorationMask ){
		bindingNumber = compiler.get_decoration( resource.id, spv::DecorationBinding );
	} else{
		ofLogWarning() << "Shader uniform" << resource.name << "does not specify binding number.";
	}
}

// ----------------------------------------------------------------------

void of::vk::Shader::reflectVertexInputs(const spirv_cross::Compiler & compiler, VertexInfo& vertexInfo ){
	ofLog() << "Vertex Attribute locations";
	const auto shaderResources = compiler.get_shader_resources();

	vertexInfo.attribute.resize( shaderResources.stage_inputs.size() );
	vertexInfo.bindingDescription.resize( shaderResources.stage_inputs.size() );

	for ( uint32_t i = 0; i != shaderResources.stage_inputs.size(); ++i ){

		auto & attributeInput = shaderResources.stage_inputs[i];
		auto attributeType = compiler.get_type( attributeInput.type_id );

		uint32_t location = i; // shader location qualifier mapped to binding number

		if ( ( 1ull << spv::DecorationLocation ) & compiler.get_decoration_mask( attributeInput.id ) ){
			location = compiler.get_decoration( attributeInput.id, spv::DecorationLocation );
		}

		ofLog() << " " << ( i + 1 == shaderResources.stage_inputs.size() ? char( 192 ) : char( 195 ) ) << std::setw( 2 ) << location << " : " << attributeInput.name;


		// Binding Description: Describe how to read data from buffer based on binding number
		vertexInfo.bindingDescription[i].binding = location;  // which binding number we are describing
		vertexInfo.bindingDescription[i].stride = ( attributeType.width / 8 ) * attributeType.vecsize * attributeType.columns;
		vertexInfo.bindingDescription[i].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		// Attribute description: Map shader location to pipeline binding number
		vertexInfo.attribute[i].location = location;   // .location == which shader attribute location
		vertexInfo.attribute[i].binding = location;    // .binding  == pipeline binding number == where attribute takes data from

		switch ( attributeType.vecsize ){
		case 3:
			vertexInfo.attribute[i].format = VK_FORMAT_R32G32B32_SFLOAT;	     // 3-part float
			break;
		case 4:
			vertexInfo.attribute[i].format = VK_FORMAT_R32G32B32A32_SFLOAT;	 // 4-part float
			break;
		default:
			break;
		}
	}

	VkPipelineVertexInputStateCreateInfo vi{
		VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO, // VkStructureType                             sType;
		nullptr,                                                   // const void*                                 pNext;
		0,                                                         // VkPipelineVertexInputStateCreateFlags       flags;
		uint32_t( vertexInfo.bindingDescription.size() ),          // uint32_t                                    vertexBindingDescriptionCount;
		vertexInfo.bindingDescription.data(),                      // const VkVertexInputBindingDescription*      pVertexBindingDescriptions;
		uint32_t( vertexInfo.attribute.size() ),                   // uint32_t                                    vertexAttributeDescriptionCount;
		vertexInfo.attribute.data()                                // const VkVertexInputAttributeDescription*    pVertexAttributeDescriptions;
	};

	vertexInfo.vi = std::move( vi );
}

// ----------------------------------------------------------------------

void of::vk::Shader::createVkPipelineLayout() {
	
	std::vector<VkDescriptorSetLayout> vkLayouts;
	vkLayouts.reserve( mPipelineLayoutMeta.size() );

	for ( const auto &k : mPipelineLayoutMeta ){
		vkLayouts.push_back( mShaderManager->getVkDescriptorSetLayout( k ) );
	}
	
	VkPipelineLayoutCreateInfo pipelineInfo{
		VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,    // VkStructureType                 sType;
		nullptr,                                          // const void*                     pNext;
		0,                                                // VkPipelineLayoutCreateFlags     flags;
		uint32_t( vkLayouts.size() ),                     // uint32_t                        setLayoutCount;
		vkLayouts.data(),                                 // const VkDescriptorSetLayout*    pSetLayouts;
		0,                                                // uint32_t                        pushConstantRangeCount;
		nullptr                                           // const VkPushConstantRange*      pPushConstantRanges;
	};
	
	auto & deviceHandle = mShaderManager->mSettings.device;

	mPipelineLayout = std::shared_ptr<VkPipelineLayout>( new VkPipelineLayout, 
		[device = deviceHandle]( VkPipelineLayout* lhs ){
		vkDestroyPipelineLayout( device, *lhs, nullptr );
		delete lhs;
	} );

	vkCreatePipelineLayout( deviceHandle, &pipelineInfo, nullptr, mPipelineLayout.get() );
}

// ----------------------------------------------------------------------

inline void of::vk::Shader::SetLayoutMeta::calculateHash(){
	std::vector<uint64_t> flatBindings;
	flatBindings.reserve( bindingTable.size() );
	for ( const auto &b : bindingTable ){
		flatBindings.push_back( b.second );
	}
	hash = SpookyHash::Hash64( flatBindings.data(), flatBindings.size() * sizeof( uint64_t ), 0 );
}

// ----------------------------------------------------------------------

inline void of::vk::Shader::DescriptorInfo::calculateHash(){
	
	// hash of type, descriptorcount, storagesize
	hash = SpookyHash::Hash64( &this->type,
		sizeof( type ) +
		sizeof( count ) +
		sizeof( storageSize )
		, 0 );
	
	hash = SpookyHash::Hash64( name.data(), name.size(), hash );

}

// ----------------------------------------------------------------------
// Check whether member ranges within an UBO overlap
// Should this be the case, there is a good chance that the 
// Ubo layout was inconsistently defined across shaders or 
// shader stages, or that there was a typo in an UBO declaration.
bool of::vk::Shader::DescriptorInfo::checkMemberRangesOverlap( 
	const MemberMap& lhs,
	const MemberMap& rhs,
	std::ostringstream & errorMsg ) const{

	// Check whether member ranges overlap.
	// 
	// 0. combine member ranges eagerly
	// 1. sort member ranges by start
	// 2. for each sorted member range
	//    2.0 move to next if current member is exact duplicate of last member [perfect match, that's what we want.]
	//    2.1 check if current member offset ==  last member offset [overlap because start at same place]
	//    2.1 check if (last member offset + last member range) > current member offset [overlap because current starts inside last]
	
	bool overlap = false;
	
	if ( rhs.empty() ){
		// impossible that there might be a conflict if there is no second set to compare with. 
		return false;
	}

	std::vector<std::pair<std::string, UboMemberRange>> ranges;
	ranges.insert( ranges.begin(), lhs.begin(), lhs.end() );
	ranges.insert( ranges.begin(), rhs.begin(), rhs.end() );

	std::sort( ranges.begin(), ranges.end(), []( const std::pair<std::string, UboMemberRange> & lhs,
		std::pair<std::string, UboMemberRange>&rhs )->bool{
		return lhs.second.offset < rhs.second.offset;
	} );

	auto lastRangeIt = ranges.begin();
	for ( auto rangeIt = ++ranges.begin(); rangeIt != ranges.end(); lastRangeIt = rangeIt++ ){

		if ( rangeIt->first == lastRangeIt->first 
			&& rangeIt->second.offset == lastRangeIt->second.offset
			&& rangeIt->second.range == lastRangeIt->second.range
			)
		{
			continue;
		}

		bool overlapStart = false;
		bool overlapRange = false;
		
		if ( rangeIt->second.offset == lastRangeIt->second.offset ){
			overlap = overlapStart = true;
		}

		if ( ( lastRangeIt->second.offset + lastRangeIt->second.range ) > rangeIt->second.offset ){
			overlap = overlapRange = true;
		}

		if ( overlapStart || overlapRange ){
			errorMsg << "Range for UBO Member Names: '" << rangeIt->first << "' and '" << lastRangeIt->first << "' overlap.";
			if ( rangeIt->second.range == lastRangeIt->second.range ){
				errorMsg << "\nCheck for a possible typo in this UBO member name.";
			} else{
				errorMsg << "\nCheck whether the elements within this UBO are laid out consistently over all shaders that use it within this Context.";
			}
		}

	}

	return overlap;
}

