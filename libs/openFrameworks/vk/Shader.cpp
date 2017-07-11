#include "vk/Shader.h"
#include "ofLog.h"
#include "ofAppRunner.h"
#include "ofFileUtils.h"
#include "spooky/SpookyV2.h"
#include "shaderc/shaderc.hpp"
#include <algorithm>

// ----------------------------------------------------------------------

namespace of{ 
namespace utils{

enum class ConsoleColor : uint32_t {
	eDefault       = 39,
	eBrightRed     = 91,
	eBrightYellow  = 93,
	eBrightCyan    = 96,
	eRed    = 31,
	eYellow = 33,
	eCyan   = 36,
};

// set console colour
std::string setConsoleColor( of::utils::ConsoleColor colour ){
#if defined( TARGET_WIN32 )
	// On Windows, we need to enable processing of ANSI color sequences.
	// We only need to do this the very first time, as the setting
	// should stick until the console is closed.
	//
	static bool needsConsoleModeSetup = true;
	if ( needsConsoleModeSetup ){
		HANDLE hConsole = GetStdHandle( STD_OUTPUT_HANDLE );
		// 0x0004 == ENABLE_VIRTUAL_TERMINAL_PROCESSING, see: 
		// https://msdn.microsoft.com/en-us/library/windows/desktop/ms686033(v=vs.85).aspx
		DWORD consoleFlags;
		GetConsoleMode( hConsole, &consoleFlags );
		consoleFlags |= 0x0004;
		SetConsoleMode( hConsole, consoleFlags );
		needsConsoleModeSetup = false;
	}
#endif
#if defined (TARGET_LINUX) || defined (TARGET_WIN32)
	 std::ostringstream tmp;
	 tmp << "\033[" << reinterpret_cast<uint32_t&>(colour) << "m";
	 return tmp.str();
#else
	return "";
#endif
}

// reset console colour
std::string resetConsoleColor(){
	return setConsoleColor(of::utils::ConsoleColor::eDefault);
}

} /*namespace utils*/ 
} /*namespace of*/


// ----------------------------------------------------------------------

// Responder for file includes through shaderc
class FileIncluder : public shaderc::CompileOptions::IncluderInterface
{
	std::unordered_set<std::string> mIncludedFiles; /// full set of included files

	struct FileInfo
	{
		const std::string      pathAsString;
		std::filesystem::path  path; /// path to file
		std::vector<char>  contents; /// contents of file
	};
public:
	// constructor
	explicit FileIncluder(){};

	// Handles shaderc_include_resolver_fn callbacks.
	shaderc_include_result* GetInclude( const char* requested_source, shaderc_include_type type, const char* requesting_source, size_t include_depth ) override;

	// Handles shaderc_include_result_release_fn callbacks.
	void ReleaseInclude( shaderc_include_result* data ) override;

};

// ----------------------------------------------------------------------

// helper method to return error via shaderc
shaderc_include_result* shadercMakeErrorIncludeResult( const char* message ){
	return new shaderc_include_result{ "", 0, message, strlen( message ) };
}

// ----------------------------------------------------------------------

shaderc_include_result * FileIncluder::GetInclude( const char * requested_source, shaderc_include_type type, const char * requesting_source, size_t include_depth ){

	std::filesystem::path path = ofToDataPath( requested_source, type == shaderc_include_type::shaderc_include_type_standard ? true : false );

	FileInfo * newFileInfo = new FileInfo{ path.string(), std::move( path ), std::vector<char>() };

	if ( false == std::filesystem::exists( newFileInfo->path ) ){
		return shadercMakeErrorIncludeResult( "<include file not found>" );
	}

	auto includeFileBuf = ofBufferFromFile( newFileInfo->path, true );

	std::vector<char> data;
	data.resize( includeFileBuf.size() );
	data.assign( includeFileBuf.begin(), includeFileBuf.end() );

	newFileInfo->contents = std::move( data );

	return new shaderc_include_result{
		newFileInfo->pathAsString.data(), newFileInfo->pathAsString.length(),
		newFileInfo->contents.data(), newFileInfo->contents.size(),
		newFileInfo };
}

// ----------------------------------------------------------------------

void FileIncluder::ReleaseInclude( shaderc_include_result * include_result ){
	auto fileInfo = reinterpret_cast<FileInfo*>( include_result->user_data );
	delete fileInfo;
	delete include_result;
}

// ----------------------------------------------------------------------
// Helper mapping vk shader stage to shaderc shader kind
static shaderc_shader_kind getShaderCKind( const vk::ShaderStageFlagBits &shaderStage ){
	shaderc_shader_kind shaderKind = shaderc_shader_kind::shaderc_glsl_infer_from_source;
	switch ( shaderStage ){
	case ::vk::ShaderStageFlagBits::eVertex:
		shaderKind = shaderc_shader_kind::shaderc_glsl_default_vertex_shader;
		break;
	case ::vk::ShaderStageFlagBits::eTessellationControl:
		shaderKind = shaderc_shader_kind::shaderc_glsl_default_tess_control_shader;
		break;
	case ::vk::ShaderStageFlagBits::eTessellationEvaluation:
		shaderKind = shaderc_shader_kind::shaderc_glsl_default_tess_evaluation_shader;
		break;
	case ::vk::ShaderStageFlagBits::eFragment:
		shaderKind = shaderc_shader_kind::shaderc_glsl_default_fragment_shader;
		break;
	case ::vk::ShaderStageFlagBits::eCompute:
		shaderKind = shaderc_shader_kind::shaderc_glsl_default_compute_shader;
		break;
	case ::vk::ShaderStageFlagBits::eGeometry:
		shaderKind = shaderc_shader_kind::shaderc_glsl_default_geometry_shader;
		break;
	default:
		break;
	}
	return shaderKind;
}
// ----------------------------------------------------------------------

of::vk::Shader::Shader( const of::vk::Shader::Settings& settings_ )
	: mSettings( settings_ )
{
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

bool of::vk::Shader::compile(){
	bool shaderDirty = false;
	
	for ( auto & source : mSettings.sources ){

		auto & shaderStage  = source.first;
		auto & shaderSource = source.second;


		bool success = getSpirV( shaderStage, shaderSource);	/* load or compiles into spirCode */

		if ( !success){
			if (!mShaderStages.empty()){
				ofLogError() << "Aborting shader compile. Using previous version of shader instead";
				return false;
			} else{
				// We must exit - there is no predictable way to recover from this.
				//
				// Using a default fail shader would be not without peril: 
				// Inputs and outputs will most certainly not match whatever 
				// the user specified for their original shader.
				ofLogFatalError() << "Shader did not compile: " << getName() << " : " << shaderSource.getName();
				ofExit( 1 );
				return false;
			}
		}

		uint64_t spirvHash = SpookyHash::Hash64( reinterpret_cast<char*>( shaderSource.spirvCode.data() ), shaderSource.spirvCode.size() * sizeof( uint32_t ), 0 );

		bool spirCodeDirty = isSpirCodeDirty( shaderStage, spirvHash );

		if ( spirCodeDirty ){
			createVkShaderModule( shaderStage, shaderSource.spirvCode);
			// store hash in map so it does not appear dirty
			mSpvHash[shaderStage] = spirvHash;
			// copy the ir code buffer into the shader compiler
			mSpvCrossCompilers[shaderStage] = make_shared<spirv_cross::Compiler>( shaderSource.spirvCode );
		}

		shaderDirty |= spirCodeDirty;
		mShaderHashDirty |= spirCodeDirty;
	}

	if ( shaderDirty ){
		reflect( mSpvCrossCompilers, mVertexInfo );
		createSetLayouts();
		createVkPipelineLayout();
		shaderDirty = false;
		return true;
	} 
	
	return false;
}

// ----------------------------------------------------------------------

bool of::vk::Shader::isSpirCodeDirty( const ::vk::ShaderStageFlagBits shaderStage, uint64_t spirvHash ){

	if ( mSpvHash.find( shaderStage ) == mSpvHash.end() ){
		// hash not found so must be dirty
		return true;
	} else{
		return ( mSpvHash[shaderStage] != spirvHash );
	}
	
	return false;
}

// ----------------------------------------------------------------------
inline bool of::vk::Shader::checkForLineNumberModifier( const std::string& line, uint32_t& lineNumber, std::string& currentFilename, std::string& lastFilename ) 
{

	if ( line.find( "#line", 0 ) != 0 )
		return false;

	// --------| invariant: current line is a line number marker

	istringstream is( line );

	// ignore until first whitespace, then parse linenumber, then parse filename
	std::string quotedFileName;
	is.ignore( numeric_limits<streamsize>::max(), ' ' ) >> lineNumber >> quotedFileName;
	// decrease line number by one, as marker line is not counted
	--lineNumber;
	// store last filename when change occurs
	std::swap( lastFilename, currentFilename );
	// remove double quotes around filename, if any
	currentFilename.assign( quotedFileName.begin() + quotedFileName.find_first_not_of( '"' ), quotedFileName.begin() + quotedFileName.find_last_not_of( '"' ) + 1 );
	return true;
}

// ----------------------------------------------------------------------

inline void of::vk::Shader::printError( const std::string& fileName, std::string& errorMessage, std::vector<char>& sourceCode ) {

	ofLogError() << "ERR \tShader compile: " << fileName;

	ofLogError() << of::utils::setConsoleColor( of::utils::ConsoleColor::eBrightRed )
		<< errorMessage
		<< of::utils::resetConsoleColor();

	std::string errorFileName( 255, '\0' );  // Will contain the name of the file which contains the error
	uint32_t    lineNumber = 0;              // Will contain error line number after successful parse

	// Error string will has the form:  "triangle.frag:28: error: '' :  syntax error"
	auto scanResult = sscanf( errorMessage.c_str(), "%[^:]:%d:", errorFileName.data(), &lineNumber );
	errorFileName.shrink_to_fit();

	ofBuffer::Lines lines( sourceCode.begin(), sourceCode.end() );

	if ( scanResult != std::char_traits<wchar_t>::eof() ){
		auto lineIt = lines.begin();

		uint32_t    currentLine = 1; /* Line numbers start counting at 1 */
		std::string currentFilename = fileName;
		std::string lastFilename = fileName;

		while ( lineIt != lines.end() ){

			// Check for lines inserted by the preprocessor which hold line numbers for included files
			// Such lines have the pattern: '#line 21 "path/to/include.frag"' (without single quotation marks)
			auto wasLineMarker = checkForLineNumberModifier( cref( lineIt.asString() ), ref( currentLine ), ref( currentFilename ), ref( lastFilename ) );

			if ( 0 == strcmp( errorFileName.c_str(), currentFilename.c_str() ) ){
				if ( currentLine >= lineNumber - 3 ){
					ostringstream sourceContext;
					const auto shaderSourceCodeLine = wasLineMarker ? "#include \"" + lastFilename + "\"" : lineIt.asString();

					if ( currentLine == lineNumber ){
						sourceContext << of::utils::setConsoleColor( of::utils::ConsoleColor::eBrightCyan );
					}

					sourceContext << std::right << std::setw( 4 ) << currentLine << " | " << shaderSourceCodeLine;

					if ( currentLine == lineNumber ){
						sourceContext << of::utils::resetConsoleColor();
					}

					ofLogError() << sourceContext.str();
				}

				if ( currentLine >= lineNumber + 2 ){
					ofLogError(); // add empty for better readability
					break;
				}
			}
			++lineIt;
			++currentLine;
		}
	}
};

// ----------------------------------------------------------------------

inline bool of::vk::Shader::compileGLSLtoSpirV( 
	const::vk::ShaderStageFlagBits shaderStage, 
	std::string & sourceText, 
	std::string fileName, 
	std::vector<uint32_t>& spirCode, 
	const std::map<std::string, string>& defines_
){

	shaderc_shader_kind shaderType = getShaderCKind( shaderStage );

	shaderc::Compiler compiler;
	shaderc::CompileOptions options;

	// Set any #defines requested 
	for ( auto& d : defines_ ){
		options.AddMacroDefinition( d.first, d.second ); // Like -DMY_DEFINE=1
	}

	// Create a temporary callback object which deals with include preprocessor directives
	options.SetIncluder( std::make_unique<FileIncluder>() );

	auto preprocessorResult = compiler.PreprocessGlsl( sourceText, shaderType, fileName.c_str(), options );

	std::vector<char> sourceCode( preprocessorResult.cbegin(), preprocessorResult.cend() );

	if ( preprocessorResult.GetCompilationStatus() != shaderc_compilation_status_success ){
		auto msg = preprocessorResult.GetErrorMessage();
		printError( fileName, msg , sourceCode );
		return  false;
	}

	auto module = compiler.CompileGlslToSpv( sourceCode.data(), sourceCode.size(), shaderType, fileName.c_str(), "main", options );

	if ( module.GetCompilationStatus() != shaderc_compilation_status_success ){
		auto msg = module.GetErrorMessage();
		printError( fileName, msg, sourceCode );
		return false;
	} else {
		spirCode.clear();
		spirCode.assign( module.cbegin(), module.cend() );
		return true;
	}
}

// ----------------------------------------------------------------------

const std::string& of::vk::Shader::getName(){
	return mSettings.name;
}

// ----------------------------------------------------------------------

bool of::vk::Shader::getSpirV( const ::vk::ShaderStageFlagBits shaderStage, Source& shaderSource ){
	
	bool success = true;

	switch ( shaderSource.mType ){
	case Source::Type::eCode:
		// nothing to do 
		break;
	case Source::Type::eFilePath:
	{
		auto f = ofFile( shaderSource.filePath );

		if ( !f.exists() ){
			ofLogFatalError() << "Shader file not found: " << shaderSource.filePath;
			success = false;
			break;
		} 

		// ---------| invariant: File exists.

		if ( mSettings.name == "" && shaderStage == ::vk::ShaderStageFlagBits::eVertex ){
			// if name has not been set explicitly, infer shader name from 
			// baseName of vertex shader file.
			const_cast<std::string&>(mSettings.name) = f.getBaseName();
		}

		auto fExt = f.getExtension();
		ofBuffer fileBuf = ofBufferFromFile( shaderSource.filePath, true );
		
		if ( fExt == "spv" ){
			// File is precompiled SPIR-V file
			ofLogNotice() << "Loading SPIR-V shader code: " << shaderSource.filePath;
			auto a = fileBuf.getData();
			shaderSource.spirvCode.assign(
				reinterpret_cast<uint32_t*>( fileBuf.getData() ),
				reinterpret_cast<uint32_t*>( fileBuf.getData() ) + fileBuf.size() / sizeof( uint32_t )
			);
			success = true;
			break;
		}

		// ----------| invariant: File does not have ".spv" extension

		success = compileGLSLtoSpirV( shaderStage, fileBuf.getText(), shaderSource.filePath.string(), shaderSource.spirvCode, shaderSource.defines);
		if ( success && mSettings.printDebugInfo ){
			ofLogNotice() << "OK \tShader compile: " << shaderSource.filePath.string();
		}
		break;
	}
	case Source::Type::eGLSLSourceInline:
	{
		std::string sourceText = shaderSource.glslSourceInline;
		success = compileGLSLtoSpirV( shaderStage, sourceText, getName() + " (Inline GLSL)", shaderSource.spirvCode, shaderSource.defines);
		if ( success && mSettings.printDebugInfo ){
			ofLogNotice() << "OK \tShader compile: [" << to_string(shaderStage) << "] " << getName() + " (Inline GLSL)";
		}
		break;
	}
	default:
		break;
	}

	return success; 
}

// ----------------------------------------------------------------------

void of::vk::Shader::createVkShaderModule( const ::vk::ShaderStageFlagBits shaderType, const std::vector<uint32_t> &spirCode ){

	::vk::ShaderModuleCreateInfo shaderModuleCreateInfo; 
	shaderModuleCreateInfo
		.setFlags( ::vk::ShaderModuleCreateFlagBits() )
		.setCodeSize( spirCode.size() * sizeof(uint32_t))
		.setPCode( spirCode.data() )
		;

	::vk::ShaderModule module = mSettings.device.createShaderModule( shaderModuleCreateInfo );

	auto tmpShaderStage = std::shared_ptr<ShaderStage>( new ShaderStage, [d = mSettings.device](ShaderStage* lhs){
		d.destroyShaderModule( lhs->module );
		delete lhs;
	} );

	tmpShaderStage->module = module;

	tmpShaderStage->createInfo = ::vk::PipelineShaderStageCreateInfo();
	tmpShaderStage->createInfo
		.setStage( shaderType )
		.setModule( tmpShaderStage->module )
		.setPName( "main" )
		.setPSpecializationInfo( nullptr )
		;

	mShaderStages[shaderType] = std::move( tmpShaderStage );
}

// ----------------------------------------------------------------------

void of::vk::Shader::reflect(
	const std::map<::vk::ShaderStageFlagBits, std::shared_ptr<spirv_cross::Compiler>>& compilers, 
	VertexInfo& vertexInfo
){
	// storage for reflected information about UBOs

	mUniforms.clear();
	mUboMembers.clear();

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

		reflectStorageBuffers(compiler, shaderStage);

		// --- vertex inputs ---
		if ( shaderStage == ::vk::ShaderStageFlagBits::eVertex ){
			
			if ( mSettings.vertexInfo.get() == nullptr ){
				// we only reflect vertex inputs if they haven't been set externally.
				reflectVertexInputs( compiler, vertexInfo );
			} else{
				vertexInfo = *mSettings.vertexInfo;
			}

			::vk::PipelineVertexInputStateCreateInfo vertexInputStateCreateInfo = ::vk::PipelineVertexInputStateCreateInfo();
			vertexInputStateCreateInfo
				.setVertexBindingDescriptionCount( vertexInfo.bindingDescription.size() )
				.setPVertexBindingDescriptions( vertexInfo.bindingDescription.data() )
				.setVertexAttributeDescriptionCount( vertexInfo.attribute.size() )
				.setPVertexAttributeDescriptions( vertexInfo.attribute.data() )
				;

			vertexInfo.vi = std::move( vertexInputStateCreateInfo );
		} 
		
	}  

	mAttributeBindingNumbers.clear();
	// Create lookup table attribute name -> attibute binding number
	// Note that multiple locations may share the same binding.
	// Attribute binding number specifies which bound buffer to read data from for this particular attribute
	for ( size_t i = 0; i != mVertexInfo.attributeNames.size(); ++i ){
		// assume attributeNames are sorted by location
		mAttributeBindingNumbers[mVertexInfo.attributeNames[i]] = mVertexInfo.attribute[i].binding;
	}


	// reserve storage for dynamic uniform data for each uniform entry
	// over all sets - then build up a list of ubos.
	for ( const auto & uniformPair : mUniforms ){
		const auto & uniform = uniformPair.second;

		for ( const auto & uniformMemberPair : uniform.uboRange.subranges ){
			// add with combined name - this should always work
			mUboMembers.insert( { uniformPair.first + "." + uniformMemberPair.first ,uniformMemberPair.second } );
			// add only with member name - this might work, but if members share the same name, we're in trouble.
			mUboMembers.insert( { uniformMemberPair.first ,uniformMemberPair.second } );
		}
	}

}

// ----------------------------------------------------------------------

size_t calcMaxRange(){
	of::vk::UniformId_t uniformT;
	uniformT.dataRange = ~( 0ULL );
	return uniformT.dataRange;
}

// ----------------------------------------------------------------------

bool of::vk::Shader::reflectUBOs( const spirv_cross::Compiler & compiler, const ::vk::ShaderStageFlagBits & shaderStage ){

	static const size_t maxRange = calcMaxRange();

	auto uniformBuffers = compiler.get_shader_resources().uniform_buffers;

	for ( const auto & ubo : uniformBuffers ){

		Uniform_t tmpUniform;
		
		tmpUniform.name = ubo.name;

		tmpUniform.uboRange.storageSize = compiler.get_declared_struct_size( compiler.get_type( ubo.type_id ) );

		if ( tmpUniform.uboRange.storageSize > maxRange ){
			;
			ofLogWarning() << of::utils::setConsoleColor( of::utils::ConsoleColor::eBrightYellow )
			               << "Ubo '" << ubo.name << "' is too large. Consider splitting it up. Size: " << tmpUniform.uboRange.storageSize
			               << of::utils::resetConsoleColor();
		}


		tmpUniform.layoutBinding
			.setDescriptorCount( 1 )                                            /* Must be 1 for ubo bindings, as arrays of ubos are not allowed */
			.setDescriptorType( ::vk::DescriptorType::eUniformBufferDynamic )   /* All our uniform buffer are dynamic */
			.setStageFlags( shaderStage )
			;

		getSetAndBindingNumber( compiler, ubo, tmpUniform.setNumber, tmpUniform.layoutBinding.binding);

		auto bufferRanges = compiler.get_active_buffer_ranges( ubo.id );

		for ( const auto &r : bufferRanges ){
			// Note that SpirV-Cross will only tell us the ranges of *actually used* members within an UBO. 
			// By merging the ranges later, we effectively also create aliases for member names which are 
			// not consistently named the same.
			auto memberName = compiler.get_member_name( ubo.base_type_id, r.index );
			tmpUniform.uboRange.subranges[memberName] = { tmpUniform.setNumber, tmpUniform.layoutBinding.binding, (uint32_t)r.offset, (uint32_t)r.range };
		}

		// Let's see if an uniform buffer with this fingerprint has already been seen.
		// If yes, it would already be in uniformStore.

		auto insertion = mUniforms.insert( { ubo.name, tmpUniform } );

		if (insertion.second == false ){
			// Uniform with this key already existed, nothing was inserted.

			auto & storedUniform = insertion.first->second;

			if ( storedUniform.uboRange.storageSize != tmpUniform.uboRange.storageSize ){

				ofLogWarning()
				        << of::utils::setConsoleColor( of::utils::ConsoleColor::eBrightRed )
				        << "Ubo: '" << ubo.name << "' re-defined with incompatible storage size."
				        << of::utils::resetConsoleColor();

				// !TODO: try to recover.
				return false;
			} else if ( storedUniform.setNumber != tmpUniform.setNumber
				|| storedUniform.layoutBinding.binding != tmpUniform.layoutBinding.binding ){

				ofLogWarning()
				        << of::utils::setConsoleColor( of::utils::ConsoleColor::eBrightYellow )
				        << "Ubo: '" << ubo.name << "' re-defined with inconsistent set/binding numbers."
				        << of::utils::resetConsoleColor();
			} else {
				// Merge stage flags
				storedUniform.layoutBinding.stageFlags |= tmpUniform.layoutBinding.stageFlags;
				// Merge memberRanges
				ostringstream overlapMsg;
				if ( checkMemberRangesOverlap( storedUniform.uboRange.subranges, tmpUniform.uboRange.subranges, overlapMsg ) ){

					// member ranges overlap: print diagnostic message
					ofLogWarning()
					        << of::utils::setConsoleColor( of::utils::ConsoleColor::eBrightYellow)
					        << "Inconsistency found parsing UBO: '" << ubo.name << "': " << std::endl << overlapMsg.str()
					        << of::utils::resetConsoleColor();
				}
				// insert any new subranges if necesary.
				storedUniform.uboRange.subranges.insert( tmpUniform.uboRange.subranges.begin(), tmpUniform.uboRange.subranges.end() );
			}
		}

	} // end: for all uniform buffers


	return true;
}

// ----------------------------------------------------------------------

bool of::vk::Shader::reflectStorageBuffers( const spirv_cross::Compiler & compiler, const ::vk::ShaderStageFlagBits & shaderStage ){

	static const size_t maxRange = calcMaxRange();

	auto storageBuffers = compiler.get_shader_resources().storage_buffers;

	for ( const auto & buffer : storageBuffers){

		Uniform_t tmpUniform;

		tmpUniform.name = buffer.name;
		tmpUniform.uboRange.storageSize = compiler.get_declared_struct_size( compiler.get_type( buffer.type_id ) );

		if ( tmpUniform.uboRange.storageSize > maxRange ){

			ofLogWarning() << of::utils::setConsoleColor( of::utils::ConsoleColor::eBrightYellow )
			               << "Ubo '" << buffer.name << "' is too large. Consider splitting it up. Size: " << tmpUniform.uboRange.storageSize
			               << of::utils::resetConsoleColor();
		}

		tmpUniform.layoutBinding
			.setDescriptorCount( 1 )                                            /* Must be 1 for ubo bindings, as arrays of ubos are not allowed */
			.setDescriptorType( ::vk::DescriptorType::eStorageBufferDynamic )   /* All our storage buffers are dynamic */
			.setStageFlags( shaderStage )
			;

		getSetAndBindingNumber( compiler, buffer, tmpUniform.setNumber, tmpUniform.layoutBinding.binding );

		auto bufferRanges = compiler.get_active_buffer_ranges( buffer.id );

		for ( const auto &r : bufferRanges ){
			auto memberName = compiler.get_member_name( buffer.base_type_id, r.index );
			tmpUniform.uboRange.subranges[memberName] = { tmpUniform.setNumber, tmpUniform.layoutBinding.binding, (uint32_t)r.offset, (uint32_t)r.range };
		}

		// Let's see if an uniform buffer with this fingerprint has already been seen.
		// If yes, it would already be in uniformStore.

		auto insertion = mUniforms.insert( { buffer.name, tmpUniform } );
		if ( insertion.second == false ){

			auto & storedUniform = insertion.first->second;
			// Uniform with this key already existed, nothing was inserted.
			if ( storedUniform.setNumber != tmpUniform.setNumber
				|| storedUniform.layoutBinding.binding != tmpUniform.layoutBinding.binding ){

				ofLogWarning()
				        << of::utils::setConsoleColor( of::utils::ConsoleColor::eBrightYellow )
				        << "Buffer: '" << buffer.name << "' re-defined with inconsistent set/binding numbers."
				        << of::utils::resetConsoleColor();
			} else{
				// Merge stage flags
				storedUniform.layoutBinding.stageFlags |= tmpUniform.layoutBinding.stageFlags;
				// insert any new subranges if necesary.
				storedUniform.uboRange.subranges.insert( tmpUniform.uboRange.subranges.begin(), tmpUniform.uboRange.subranges.end() );
			}
		}

	} // end: for all uniform buffers


	return true;
}
// ----------------------------------------------------------------------

bool of::vk::Shader::reflectSamplers( const spirv_cross::Compiler & compiler, const ::vk::ShaderStageFlagBits & shaderStage ){

	auto sampledImages = compiler.get_shader_resources().sampled_images;

	for ( const auto & sampledImage : sampledImages){

		Uniform_t tmpUniform;
		tmpUniform.name = sampledImage.name;
		tmpUniform.layoutBinding
			.setDescriptorCount( 1 ) //!TODO: find out how to query array size
			.setDescriptorType( ::vk::DescriptorType::eCombinedImageSampler )
			.setStageFlags( shaderStage )
			;

		getSetAndBindingNumber( compiler, sampledImage, tmpUniform.setNumber, tmpUniform.layoutBinding.binding );

		// Let's see if an uniform buffer with this fingerprint has already been seen.
		// If yes, it would already be in uniformStore.

		auto insertion = mUniforms.insert( { sampledImage.name, tmpUniform } );

		if ( insertion.second == false ){
			// uniform with this key already exists: check set and binding numbers are identical
			auto & storedUniform = insertion.first->second;
			// otherwise print a warning and return false.
			if ( storedUniform.layoutBinding.binding != tmpUniform.layoutBinding.binding
				|| storedUniform.setNumber != tmpUniform.setNumber ){

				ofLogWarning() << of::utils::setConsoleColor( of::utils::ConsoleColor::eBrightYellow )
				               << "Combined image sampler: '" << sampledImage.name << "' is declared multiple times, but with inconsistent binding/set number."
				               << of::utils::resetConsoleColor();
				return false;
			} else{
				// Merge stage flags
				storedUniform.layoutBinding.stageFlags |= tmpUniform.layoutBinding.stageFlags;
				// insert any new subranges if necesary.
				storedUniform.uboRange.subranges.insert( tmpUniform.uboRange.subranges.begin(), tmpUniform.uboRange.subranges.end() );
			}
		}

	} // end: for all uniform buffers

	return true;
}

// ----------------------------------------------------------------------

bool of::vk::Shader::createSetLayouts(){
	
	// Consolidate uniforms into descriptor sets



	if ( mUniforms.empty() ){
		// nothing to do.
		return true;
	}

	// map from descriptorSet to map of (sparse) bindings
	map<uint32_t, map<uint32_t, Uniform_t>> uniformSetLayouts;

	// --------| invariant: there are uniforms to assign to descriptorsets.
	
	for ( const auto & uniform : mUniforms ){

		const std::pair<uint32_t, Uniform_t> uniformBinding = {
			uniform.second.layoutBinding.binding, 
			uniform.second,
		};

		// attempt to insert a fresh set
		auto setInsertion = uniformSetLayouts.insert( { uniform.second.setNumber, { uniformBinding } } );

		if ( setInsertion.second == false ){
		// if there was already a set at this position, append to this set
			auto bindingInsertion = setInsertion.first->second.insert( uniformBinding );
			if ( bindingInsertion.second == false ){
				ofLogError()
					<< of::utils::setConsoleColor( of::utils::ConsoleColor::eBrightRed )
					<< "Could not insert binding - it appears that there is already a binding a this position, set: " << uniform.second.setNumber
					<< ", binding number: " << uniform.second.layoutBinding.binding
					<< of::utils::setConsoleColor( of::utils::ConsoleColor::eDefault );
				return false;
			}
		}
	}

	
	// assert set numbers are not sparse.
	if ( uniformSetLayouts.size() != ( uniformSetLayouts.rbegin()->first + 1 ) ){
		ofLogError() << of::utils::setConsoleColor( of::utils::ConsoleColor::eBrightRed ) 
			<< "Descriptor sets may not be sparse"
			<< of::utils::setConsoleColor( of::utils::ConsoleColor::eDefault );
		return  false;
	}

	
	// make sure that for each set, bindings are not sparse by adding placeholder uniforms 
	// into empty binding slots.
	{
		Uniform_t placeHolderUniform;
		placeHolderUniform.layoutBinding.descriptorCount = 0; // a count of 0 marks this descriptor as being a placeholder.

		for ( auto & uniformSetLayout : uniformSetLayouts ){
			
			const auto & setNumber = uniformSetLayout.first;
			auto & bindings        = uniformSetLayout.second;

			placeHolderUniform.setNumber = setNumber;

			if ( bindings.empty() ){
				continue;
			}

			// Attempt to insert placeholder descriptors for each binding.
			uint32_t last_binding_number = bindings.crbegin()->first;
			placeHolderUniform.layoutBinding.stageFlags = bindings[last_binding_number].layoutBinding.stageFlags;
			uint32_t bindingCount = last_binding_number + 1;

			for ( uint32_t i = 0; i != bindingCount; ++i ){
				placeHolderUniform.layoutBinding.binding = i;
				auto insertionResult = bindings.insert( { i, placeHolderUniform } );
				if ( insertionResult.second == true ){

					ofLogWarning() << of::utils::setConsoleColor( of::utils::ConsoleColor::eBrightYellow )
					               << "Detected sparse bindings: gap at set: " << setNumber << ", binding: " << i << ". This could slow the GPU down."
					               << of::utils::resetConsoleColor();
				} 
			}
		}
	}
	
	// ---------| invariant: we should have a map of sets and each set should have bindings
	//            and both in ascending order.
	
	{
	
		// create temporary data storage object for uniforms
		// and uniform dictionary 
		
		mDescriptorSetData.clear();
		mUniformDictionary.clear();

		size_t setLayoutIndex = 0;

		for ( auto & setLayoutBindingsMapPair : uniformSetLayouts ){
			
			const auto & setNumber            = setLayoutBindingsMapPair.first;
			const auto & setLayoutBindingsMap = setLayoutBindingsMapPair.second;

			DescriptorSetData_t tmpDescriptorSetData;
			UniformId_t uniformId;

			auto & descriptors = tmpDescriptorSetData.descriptors;

			uniformId.setIndex        = setLayoutIndex;
			uniformId.descriptorIndex = 0;
			uniformId.auxDataIndex    = -1;

			for ( auto & bindingsPair : setLayoutBindingsMap ){

				const auto & bindingNumber = bindingsPair.first;
				const auto & uniform       = bindingsPair.second;
				const auto & layoutBinding = uniform.layoutBinding;

				uniformId.dataOffset = 0;
				uniformId.dataRange  = 0;

				if ( layoutBinding.descriptorType == ::vk::DescriptorType::eUniformBufferDynamic ){
					
					tmpDescriptorSetData.dynamicBindingOffsets.push_back( 0 );
					tmpDescriptorSetData.dynamicUboData.push_back( {} );

					uniformId.auxDataIndex = tmpDescriptorSetData.dynamicUboData.size() - 1;

					// go through member ranges if any.
					for ( const auto &subRangePair : uniform.uboRange.subranges ){
						
						auto uboMemberUniformId = uniformId;

						const auto & memberName  = subRangePair.first;
						const auto & memberRange = subRangePair.second;
						
						uboMemberUniformId.dataOffset = memberRange.offset;
						uboMemberUniformId.dataRange  = memberRange.range;
						
						mUniformDictionary.insert( { uniform.name + '.' + memberName, uboMemberUniformId } );

						auto insertionResult = mUniformDictionary.insert( { memberName, uboMemberUniformId } );

						if ( insertionResult.second == false ){
							ofLogWarning()
							        << of::utils::setConsoleColor( of::utils::ConsoleColor::eBrightYellow )
							        << "Uniform Ubo member name not uniqe: '" << memberName << "'."
							        << of::utils::resetConsoleColor();
						}
						
					}

					uniformId.dataOffset = 0;
					uniformId.dataRange  = uniform.uboRange.storageSize;

					// make space for data storage
					tmpDescriptorSetData.dynamicUboData.back().resize( uniformId.dataRange );
				}

				if ( layoutBinding.descriptorType == ::vk::DescriptorType::eStorageBufferDynamic ){
					tmpDescriptorSetData.dynamicBindingOffsets.push_back( 0 );
					tmpDescriptorSetData.bufferAttachment.push_back( {} );
					uniformId.auxDataIndex = tmpDescriptorSetData.bufferAttachment.size() - 1;
					
					uniformId.dataRange = uniform.uboRange.storageSize;
				}

				for ( uint32_t arrayIndex = 0; arrayIndex != layoutBinding.descriptorCount; ++arrayIndex ){

					DescriptorSetData_t::DescriptorData_t descriptorData;
					descriptorData.bindingNumber = layoutBinding.binding;
					descriptorData.arrayIndex = arrayIndex;
					descriptorData.type = layoutBinding.descriptorType;

					if ( layoutBinding.descriptorType == ::vk::DescriptorType::eCombinedImageSampler ){
						// store image attachment 
						tmpDescriptorSetData.imageAttachment.push_back( {} );
						uniformId.auxDataIndex = tmpDescriptorSetData.imageAttachment.size() - 1;
					}

					descriptors.emplace_back( std::move( descriptorData ) );

					mUniformDictionary.insert( { uniform.name, uniformId } );

					uniformId.descriptorIndex++;
				}
			}

			mDescriptorSetData.emplace_back( std::move( tmpDescriptorSetData ) );
			++setLayoutIndex;
		}


	}
	
	// ---------

	// print out shader binding log, but only when compiled in debug mode.
	if ( mSettings.printDebugInfo ){

		std::ostringstream log;
		log << "Shader Uniform Bindings: " << endl;

		for ( const auto & descriptorSetPair : uniformSetLayouts ){
			const auto & setId = descriptorSetPair.first;
			const size_t indentLevel = 2;

			log << std::string( indentLevel, ' ' )
				<< " Set " << std::setw( 2 ) << setId << ": " << std::endl;

			for ( const auto & bindingPair : descriptorSetPair.second ){
				const size_t indentLevel = 6;

				const auto & bindingNumber = bindingPair.first;
				const auto & binding = bindingPair.second;

				log << std::string( indentLevel, ' ' )
					<< std::setw( 2 ) << std::right << binding.layoutBinding.binding;

				if ( binding.layoutBinding.descriptorCount == 0 ){
					log << " - UNUSED - " << std::endl;
				} else{
					log << "[" << std::setw( 3 ) << std::right << binding.layoutBinding.descriptorCount << "] : '" << binding.name << "'\t";
				}

				switch ( binding.layoutBinding.descriptorType ){
					case ::vk::DescriptorType::eUniformBufferDynamic:
						log << "Dynamic ";
						// fall through to uniformBuffer, as these two are similar.
					case ::vk::DescriptorType::eUniformBuffer:
					{
						log << "UniformBuffer - ";
						log << " Total Size : " << std::right << std::setw( 4 ) << binding.uboRange.storageSize << "B";
						log << std::endl;

						std::map< of::vk::Shader::UboMemberSubrange, std::string> reverseSubrangeMap;

						for ( const auto & subrangePair : binding.uboRange.subranges ){
							reverseSubrangeMap.insert( { subrangePair.second, subrangePair.first } );
						};

						for ( const auto & subrangePair : reverseSubrangeMap ){
							const size_t indentLevel = 12;

							const auto & name = subrangePair.second;
							const auto & subrange = subrangePair.first;

							log << std::string( indentLevel, ' ' )
								<< "> " << std::setw( 40 ) << "'" + subrangePair.second + "'"
								<< ", offset: " << std::setw( 5 ) << std::right << subrange.offset << "B"
								<< ", size  : " << std::setw( 5 ) << std::right << subrange.range << "B"
								<< std::endl;
						}
					}
					break;
					case ::vk::DescriptorType::eStorageBufferDynamic:
						log << "Dynamic Storage Buffer - ";
						log << " Total Size : " << std::right << std::setw( 4 ) << binding.uboRange.storageSize << "B";
						log << std::endl;

						break;
					case ::vk::DescriptorType::eCombinedImageSampler:
						log << "Combined Image Sampler";
						break;
					default:
						break;
				} // end switch binding.layoutBinding.descriptorType

				log << std::endl;
			}
		}

		// Print Attribute Inputs
		{
			size_t numAttributes = mVertexInfo.attribute.size();
			
			log << std::endl << "Attribute Inputs:" << std::endl;

			for ( size_t i = 0; i != numAttributes; ++i ){
				log  
					<< "\t(location = " << std::setw(2) << mVertexInfo.attribute[i].location << ") : "
					<< "binding : "   << std::setw( 2 ) << mVertexInfo.attribute[i].binding << " : "
					<< "offset : "    << std::setw( 4 ) << mVertexInfo.attribute[i].offset << "B : "
					<< std::right << std::setw( 15 ) << ::vk::to_string(mVertexInfo.attribute[i].format) << " : "
					<< mVertexInfo.attributeNames[i] 
					<< std::endl;
			}

		}

		ofLogNotice() << log.str();
	}

	// --------- build VkDescriptorSetLayouts
	
	mDescriptorSetsInfo.clear();
	mDescriptorSetsInfo.reserve( uniformSetLayouts.size() );

	mDescriptorSetLayoutKeys.clear();
	mDescriptorSetLayoutKeys.reserve( uniformSetLayouts.size() );

	for (const auto & descriptorSet : uniformSetLayouts ){
		DescriptorSetLayoutInfo layoutInfo;
		layoutInfo.bindings.reserve( descriptorSet.second.size() );
		
		for ( const auto & binding : descriptorSet.second ){
			layoutInfo.bindings.emplace_back( std::move(binding.second.layoutBinding) );
		}

		static_assert(
			+sizeof( ::vk::DescriptorSetLayoutBinding::binding )
			+ sizeof( ::vk::DescriptorSetLayoutBinding::descriptorType )
			+ sizeof( ::vk::DescriptorSetLayoutBinding::descriptorCount )
			+ sizeof( ::vk::DescriptorSetLayoutBinding::stageFlags )
			+ sizeof( ::vk::DescriptorSetLayoutBinding::pImmutableSamplers )
			== sizeof( ::vk::DescriptorSetLayoutBinding )
			, "DescriptorSetLayoutBindings is not tightly packed." );

		layoutInfo.hash = SpookyHash::Hash64(layoutInfo.bindings.data(),layoutInfo.bindings.size() * sizeof( ::vk::DescriptorSetLayoutBinding ), 0);
		
		mDescriptorSetLayoutKeys.push_back( layoutInfo.hash );
		mDescriptorSetsInfo.emplace_back( std::move( layoutInfo ) );
	}

	// -------| invariant: mDescriptorSetInfo contains information for each descriptorset.

	mDescriptorSetLayouts.clear();
	mDescriptorSetLayouts.reserve( mDescriptorSetsInfo.size() );

	for ( auto & descriptorSetInfo : mDescriptorSetsInfo ){
		::vk::DescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo;
		descriptorSetLayoutCreateInfo
			.setBindingCount( descriptorSetInfo.bindings.size() )
			.setPBindings( descriptorSetInfo.bindings.data() )
			;

		// Create the auto-deleter
		std::shared_ptr<::vk::DescriptorSetLayout> vkDescriptorSetLayout =
			std::shared_ptr<::vk::DescriptorSetLayout>( new ::vk::DescriptorSetLayout, [d = mSettings.device]( ::vk::DescriptorSetLayout* lhs ){
			if ( *lhs ){
				d.destroyDescriptorSetLayout( *lhs );
			}
			delete lhs;
		} );
		
		// create new descriptorSetLayout
		*vkDescriptorSetLayout = mSettings.device.createDescriptorSetLayout( descriptorSetLayoutCreateInfo );
		
		// store new descriptorSetLayout
		mDescriptorSetLayouts.emplace_back( std::move( vkDescriptorSetLayout ) );
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

void of::vk::Shader::reflectVertexInputs(const spirv_cross::Compiler & compiler, of::vk::Shader::VertexInfo& vertexInfo ){
	const auto shaderResources = compiler.get_shader_resources();

	vertexInfo.attribute.resize( shaderResources.stage_inputs.size() );
	vertexInfo.bindingDescription.resize( shaderResources.stage_inputs.size() );
	vertexInfo.attributeNames.resize( shaderResources.stage_inputs.size() );

	for ( uint32_t i = 0; i != shaderResources.stage_inputs.size(); ++i ){

		auto & attributeInput = shaderResources.stage_inputs[i];
		auto attributeType = compiler.get_type( attributeInput.type_id );

		uint32_t location = i; // shader location qualifier mapped to binding number

		if ( ( 1ull << spv::DecorationLocation ) & compiler.get_decoration_mask( attributeInput.id ) ){
			location = compiler.get_decoration( attributeInput.id, spv::DecorationLocation );
		}

		// TODO: figure out what to do if attribute bindings are sparse, i.e. the number of locations
		// is less than the highest location reflected from DecorationLocation

		vertexInfo.attributeNames[location] = attributeInput.name;

		// Binding Description: Describe how to read data from buffer based on binding number
		vertexInfo.bindingDescription[location].binding = location;  // which binding number we are describing
		vertexInfo.bindingDescription[location].stride = ( attributeType.width / 8 ) * attributeType.vecsize * attributeType.columns;
		vertexInfo.bindingDescription[location].inputRate = ::vk::VertexInputRate::eVertex;
		

		// Attribute description: Map shader location to pipeline binding number
		vertexInfo.attribute[location].location = location;   // .location == which shader attribute location
		vertexInfo.attribute[location].binding = location;    // .binding  == pipeline binding number == which index of bound buffer where data is read from
		vertexInfo.attribute[location].offset = 0;            //TODO: allow interleaved vertex inputs

		switch ( attributeType.vecsize ){
		case 2:
			vertexInfo.attribute[location].format = ::vk::Format::eR32G32Sfloat;        // 2-part float
			break;
		case 3:
			vertexInfo.attribute[location].format = ::vk::Format::eR32G32B32Sfloat;     // 3-part float
			break;
		case 4:
			vertexInfo.attribute[location].format = ::vk::Format::eR32G32B32A32Sfloat;	 // 4-part float
			break;
		default:
			ofLogWarning()
			        << of::utils::setConsoleColor( of::utils::ConsoleColor::eBrightYellow )
			        << "Could not determine vertex attribute type for: " << attributeInput.name
			        << of::utils::resetConsoleColor();
			break;
		}
	}


}

// ----------------------------------------------------------------------

void of::vk::Shader::createVkPipelineLayout() {
	
	std::vector<::vk::DescriptorSetLayout> vkLayouts;
	vkLayouts.reserve( mDescriptorSetLayouts.size() );

	for ( const auto &layout : mDescriptorSetLayouts ){
		vkLayouts.push_back(*layout);
	}
	
	auto pipelineInfo = ::vk::PipelineLayoutCreateInfo();
	pipelineInfo
		.setSetLayoutCount( vkLayouts.size())
		.setPSetLayouts( vkLayouts.data() )
		.setPushConstantRangeCount( 0 )
		.setPPushConstantRanges( nullptr )
		;


	mPipelineLayout = std::shared_ptr<::vk::PipelineLayout>( new ::vk::PipelineLayout,
		[device = mSettings.device]( ::vk::PipelineLayout* lhs ){
		if ( lhs ){
			device.destroyPipelineLayout( *lhs );
		}
		delete lhs;
	} );

	*mPipelineLayout = mSettings.device.createPipelineLayout( pipelineInfo );

}


// ----------------------------------------------------------------------
// Check whether member ranges within an UBO overlap
// Should this be the case, there is a good chance that the 
// Ubo layout was inconsistently defined across shaders or 
// shader stages, or that there was a typo in an UBO declaration.
bool of::vk::Shader::checkMemberRangesOverlap(
	const std::map<std::string, of::vk::Shader::UboMemberSubrange>& lhs,
	const std::map<std::string, of::vk::Shader::UboMemberSubrange>& rhs,
	std::ostringstream & errorMsg ) {

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

	std::vector<std::pair<std::string, of::vk::Shader::UboMemberSubrange>> ranges;
	ranges.insert( ranges.begin(), lhs.begin(), lhs.end() );
	ranges.insert( ranges.begin(), rhs.begin(), rhs.end() );

	std::sort( ranges.begin(), ranges.end(), []( const std::pair<std::string, of::vk::Shader::UboMemberSubrange> & lhs,
		std::pair<std::string, of::vk::Shader::UboMemberSubrange>&rhs )->bool{
		return lhs.second.offset < rhs.second.offset;
	} );

	auto lastRangeIt = ranges.begin();
	for ( auto rangeIt = ++ranges.begin(); rangeIt != ranges.end(); lastRangeIt = rangeIt++ ){

		if ( rangeIt->first == lastRangeIt->first
			&& rangeIt->second.offset == lastRangeIt->second.offset
			&& rangeIt->second.range == lastRangeIt->second.range
			){
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

