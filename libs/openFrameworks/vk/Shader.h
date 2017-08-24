#pragma once
#include <string>
#include <map>
#include "vulkan/vulkan.hpp"
#include "vk/spirv-cross/include/spirv_cross.hpp"
#include "vk/HelperTypes.h"

//   The smallest unit we may bind in Vulkan are DescriptorSets.
//   
//   Each set has its own namespace for bindings. Bindings may be sparse.
//   
//   If no set is specified explicitly in the shader, set 0 is used.
//   
//   A DescriptorSet is described by a DescriptorSetLayout.
//   
//   A DescriptorSetLayout is a table of DescriptorSetLayoutBindings
//   
//   A DescriptorSetLayoutBinding describes the type of descriptor and its count-
//   if the count is >1, this DSLB represents an
//   array of descriptors of the same type.
//   
//   An ordered array of DescriptorSetLayout is used to describe a PipelineLayout,
//   it shows which DescriptorSets in sequence describe the inputs for a pipeline.
//   
//   set 0
//   |- binding 0 - descriptor 0
//   |- binding 1 - decriptors[] 1,2,3
//   |- <empty>
//   |- binding 3 - descriptor 4
//   
//   set 1
//   |- binding 0
//   |- binding 1
//   
//   There does not seem to be a maximum number of descriptors that we can allocate,
//   it is limited by GPU memory. On AMD/Radeon, a descriptor consumes at most 32 bytes,
//   so you can have 32k descriptors per MB of GPU memory. On AMD, there is a privileged
//   256 MB of memory that is directly accessible through the CPU - on NVIDIA, this
//   chunk is possibly larger. This is where Descriptors are stored.
//   
//   Descriptors are allocated from DescriptorPools. If you use
//   VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, this means the pool uses a dynamic
//   allocator, which brings the risk of fragmentation. If you set up the pool so that it
//   only supports RESET, then this pool will have a linear allocator, memory will be pre-
//   allocated in a big chunk, and allocating and resetting is effectively updating an
//   offset, similar to what of::vk::Allocator does.
//   
//   Also, vkWriteDescriptorSet writes directly to GPU memory.
//   
//   There is a maximum number of DescriptorSets and Descriptors that can be bound
//   at the same time.
//   
//   Here is some more information about the Vulkan binding model:
//   https://developer.nvidia.com/vulkan-shader-resource-binding
//   
//   Some information on Descriptor Sets and fast paths
//   http://gpuopen.com/wp-content/uploads/2016/03/VulkanFastPaths.pdf
//   
//   
//   
//   
//	 Here is a diagram on how vertex attribute binding works, based on vkspec 1.0, pp.389
//
//
//     SHADER                                                       | Pipeline setup                               PIPELINE              |  COMMAND BUFFER   | BUFFERS (GPU MEMORY)
//   ---------------------------------------------------------------|--------------------------------------------|-----------------------|-------------------|---------------------
//     shader variable  => associated with => input attribute number| association on per-pipeline basis          | vertex input bindings |  association on a per-draw basis
//                                            in GLSL, set through  | when defining the pipeline with            |                       |  when binding buffers using
//                                           "location" qualifier   | "VkPipelineVertexInputStateCreateInfo"     | "binding number"      |  "vkCmdBindVertexBuffers"
//                                                                  |                                            |                       |                   |
//                                                                  | attributeDescription -> bindingDescription |                       |                   |
//                                                                  |    "location"        -> "binding"          |                       |                   |
//    vec3  inPos                   =>        (location = 0)        | --------------------.  .------------------ | [0] ----------------- |  ------.  .------ |   buffer A
//                                                                  |                      \/                    |                       |         \/        |
//                                                                  |                      /\                    |                       |         /\        |
//    vec3  inNormal                =>        (location = 1)        | --------------------'  '------------------ | [1] ----------------- |  ------'  '------ |   buffer B
//


namespace of{
namespace vk{



// ----------


class Shader
{
public:

	struct DescriptorSetLayoutInfo
	{
		uint64_t hash;	// hash for this descriptor set layout.
		std::vector<::vk::DescriptorSetLayoutBinding> bindings;
	};

	struct VertexInfo
	{
		std::vector<std::string>                           attributeNames;		  // attribute names sorted by location
		std::vector<::vk::VertexInputBindingDescription>   bindingDescription;	  // describes data input parameters for pipeline slots
		std::vector<::vk::VertexInputAttributeDescription> attribute;	          // mapping of attribute locations to pipeline slots
		::vk::PipelineVertexInputStateCreateInfo vi;
	};

	struct UboMemberSubrange
	{
		uint32_t setNumber;
		uint32_t bindingNumber;
		uint32_t offset;
		uint32_t range;

		friend
			inline bool operator < ( UboMemberSubrange const & lhs, UboMemberSubrange const & rhs ){
			return lhs.offset < rhs.offset;
		}
	};

	struct UboRange
	{
		uint32_t storageSize = 0;
		std::map<std::string, UboMemberSubrange> subranges;
	};

	struct Uniform_t
	{
		uint32_t                         setNumber = 0;
		::vk::DescriptorSetLayoutBinding layoutBinding;	/* this contains binding number */

		std::string                      name;
		UboRange                         uboRange;
	};

	class Source
	{
		// Shader source may be one of:
		// + Vector of uint32_t binary spir-v code
		// + A filesystem path from where to load glsl/spv source
		// + A string of glsl code

		std::vector<uint32_t>         spirvCode;
		std::filesystem::path         filePath;
		std::string                   glslSourceInline;

		std::map<std::string, string> defines; // passed to GLSL compiler like: "-DNAME=VALUE"

		enum class Type : uint8_t
		{
			eUndefined        = 0,
			eCode             = 1,
			eFilePath         = 2,
			eGLSLSourceInline = 3,  // inline GLSL
		} mType = Type::eUndefined;

	public:

		Source( std::vector<uint32_t> code_ )
			: spirvCode( code_ )
			, mType( Type::eCode ){};

		Source( std::filesystem::path path_ )
			: filePath( path_ )
			, mType( Type::eFilePath ){};

		Source( const char* filePath )
			: filePath( filePath )
			, mType( Type::eFilePath ){};

		Source( const std::string& glslSourceInline )
			: glslSourceInline( glslSourceInline )
			, mType( Type::eGLSLSourceInline ){};

		const std::string getName() const{
			switch ( mType ){
			case Source::Type::eCode:
				return "Inline SPIRV";
			case Source::Type::eFilePath:
				return filePath.string();
			case Source::Type::eGLSLSourceInline:
				return "Inline GLSL";
			default:
				return "";
			}
		};

		// Default vertex shader forwards normals, and position in eye space,
		// outputs transformed geometry to clip space
		static const std::vector<uint32_t> defaultShaderVert;
		
		// Draw geometry using globalColor
		static const std::vector<uint32_t> defaultShaderFragGlobalColor;  

		// Draw geometry in color based on normals
		static const std::vector<uint32_t> defaultShaderFragNormalColor; 

		friend class Shader;
	};

	struct Settings
	{
		bool                                        printDebugInfo = false;
		::vk::Device                                device;
		std::shared_ptr<VertexInfo>                 vertexInfo;              // Set this if you want to override vertex info generated through shader reflection
		mutable std::map<::vk::ShaderStageFlagBits, Source> sources;         // specify source object for each shader stage - if source is of type eFilePath, file extension is '.spv' no compilation occurs, otherwise file is loaded and compiled using shaderc
		std::string                                 name;                    // Debug name for shader - by default and if possible this is set to name of vertex shader file, less extension
		
		Settings& setPrintDebugInfo( bool shouldPrint_ ){
			printDebugInfo = shouldPrint_;
			return *this;
		}
		Settings& setDevice( const ::vk::Device& device_ ){
			device = device_;
			return *this;
		}
		
		Settings& setSource( ::vk::ShaderStageFlagBits shaderType_, const Source& src_ ){
			// We first erase at key position in case this source was set before
			// in case there was no entry with this key, this will do nothing
			sources.erase( shaderType_ );
			// We use emplace, as this uses a templated constructor, which will 
			// construct the element in place, allowing Shader::Source
			// to find the most matching specialised constructor.
			sources.emplace( shaderType_, src_ );
			return *this;
		}
		Settings& setVertexInfo( std::shared_ptr<VertexInfo> info_ ){
			vertexInfo = info_;
			return *this;
		}
		Settings& setName( const std::string& name_ ){
			name = name_;
			return *this;
		}
		Settings& clearSources(){
			sources.clear();
			return *this;
		}
	};

private:

	const Settings mSettings;

	// default template for descriptor set data
	std::vector<DescriptorSetData_t>   mDescriptorSetData;
	// default keys into descriptor set data.
	std::map<std::string, UniformId_t> mUniformDictionary;

	// keys for resources based on set-, binding number
	std::vector<std::vector<UniformId_t>> mUniformBindings;

	VertexInfo mVertexInfo;

	// map from uniform name to uniform data
	std::map<std::string, Uniform_t> mUniforms;

	// lookup table from uniform name to storage info for dynamic ubos
	std::map<std::string, Shader::UboMemberSubrange> mUboMembers;

	// vector of descriptor set binding information (index is descriptor set number)
	std::vector<DescriptorSetLayoutInfo> mDescriptorSetsInfo;
	
	// attribute binding numbers, indexed by attribute name
	std::map<std::string, size_t> mAttributeBindingNumbers;

	// vector of just the descriptor set layout keys - this needs to be kept in sync 
	// with mDescriptorSetsInfo
	std::vector<uint64_t>               mDescriptorSetLayoutKeys;

	std::vector<std::shared_ptr<::vk::DescriptorSetLayout>> mDescriptorSetLayouts;

	std::shared_ptr<::vk::PipelineLayout> mPipelineLayout;

	uint64_t mShaderHash = 0;
	bool     mShaderHashDirty = true;

	struct ShaderStage{
		::vk::ShaderModule module;
		::vk::PipelineShaderStageCreateInfo createInfo;
	};

	std::map<::vk::ShaderStageFlagBits, std::shared_ptr<ShaderStage>> mShaderStages;
	std::map<::vk::ShaderStageFlagBits, std::shared_ptr<spirv_cross::Compiler>> mSpvCrossCompilers;
	
	// hashes for pre-compiled spirv
	// we use this to find out if shader code has changed.
	std::map<::vk::ShaderStageFlagBits, uint64_t> mSpvHash;

	// ----------------------------------------------------------------------
	// Derive bindings from shader reflection using SPIR-V Cross.
	// we want to extract as much information out of the shader metadata as possible
	// all this data helps us to create descriptors, and also to create layouts fit
	// for our pipelines.
	void reflect( const std::map<::vk::ShaderStageFlagBits, std::shared_ptr<spirv_cross::Compiler>>& compilers, VertexInfo& vertexInfo );
	
	static void reflectVertexInputs( const spirv_cross::Compiler & compiler, of::vk::Shader::VertexInfo& vertexInfo );

	bool reflectUBOs( const spirv_cross::Compiler & compiler, const ::vk::ShaderStageFlagBits & shaderStage );
	bool reflectSamplers( const spirv_cross::Compiler & compiler, const ::vk::ShaderStageFlagBits & shaderStage);
	bool reflectStorageBuffers( const spirv_cross::Compiler & compiler, const ::vk::ShaderStageFlagBits & shaderStage );

	bool createSetLayouts();
	void createVkPipelineLayout();

	// based on shader source type, read /compile either spirv or glsl file and fill vector of spirV words
	bool getSpirV( const ::vk::ShaderStageFlagBits shaderType, of::vk::Shader::Source& shaderSource );
	
	// find out if module is dirty
	bool isSpirCodeDirty( const ::vk::ShaderStageFlagBits shaderStage, uint64_t spirvHash );

	// create vkShader module from binary spirv code
	void createVkShaderModule( const ::vk::ShaderStageFlagBits shaderType, const std::vector<uint32_t> &spirCode);

	static bool checkMemberRangesOverlap(
		const std::map<std::string, of::vk::Shader::UboMemberSubrange>& lhs,
		const std::map<std::string, of::vk::Shader::UboMemberSubrange>& rhs,
		std::ostringstream & errorMsg );

	static void getSetAndBindingNumber( const spirv_cross::Compiler & compiler, const spirv_cross::Resource & resource, uint32_t &descriptor_set, uint32_t &bindingNumber );


	// Utility method for finding line number modifiers when compiling using shaderc 
	// line number modifiers are lines which the preprocessor inserts to mark line 
	// number offsets for included files.
	static bool checkForLineNumberModifier( const std::string& line, uint32_t& lineNumber, std::string& currentFilename, std::string& lastFilename );
	
	// Utility method for printing errors when compiling using shaderc
	static void printError( const std::string& fileName, std::string& errorMessage, std::vector<char>& sourceCode );

public:

	// ----------------------------------------------------------------------

	// shader object needs to be initialised based on spir-v sources to be useful
	Shader( const of::vk::Shader::Settings& settings_ );

	// ----------------------------------------------------------------------

	~Shader()
	{
		mSpvCrossCompilers.clear();
		mShaderStages.clear();
	}

	// re-compile shader - this invalidates hashes only when shader has changed.
	// returns true if new shader compiled successfully, otherwise false
	bool compile();

	// return shader stage information for pipeline creation
	const std::vector<::vk::PipelineShaderStageCreateInfo> getShaderStageCreateInfo();

	const std::vector<DescriptorSetLayoutInfo> & getDescriptorSetsInfo();

	// return vertex input state create info
	// this hold the layout and wiring of attribute inputs to vertex bindings
	const ::vk::PipelineVertexInputStateCreateInfo& getVertexInputState();
	
	// return pipeline layout reflected from this shader
	const std::shared_ptr<::vk::PipelineLayout>& getPipelineLayout();

	const std::vector<uint64_t> & getDescriptorSetLayoutKeys() const;

	// get setLayout at set index
	const std::shared_ptr<::vk::DescriptorSetLayout>& getDescriptorSetLayout( size_t setId ) const;

	// get all set layouts 
	const std::vector<std::shared_ptr<::vk::DescriptorSetLayout>>& getDescriptorSetLayouts() const;
	
	// returns hash of spirv code over all shader shader stages
	const uint64_t getShaderCodeHash();

	//const std::map<std::string, Uniform_t>& getUniforms();

	const std::vector<std::string> & getAttributeNames();
	
	bool getAttributeBinding( const std::string& name, size_t& index ) const;

	const VertexInfo& getVertexInfo();

	const std::vector<DescriptorSetData_t> & getDescriptorSetData() const{
		return mDescriptorSetData;
	};

	const std::map<std::string, UniformId_t>& getUniformDictionary() const{
		return mUniformDictionary;
	}

	const std::vector<std::vector<UniformId_t>>& getUniformBindings() const {
		return mUniformBindings;
	};

	// Compile source text and store result in vector of SPIR-V words 
	static bool compileGLSLtoSpirV( const ::vk::ShaderStageFlagBits shaderStage, const std::string & sourceText, std::string fileName, std::vector<uint32_t>& spirCode, const std::map<std::string, std::string>& defines_ = {});

	// shader name is debug name for shader
	const std::string& getName();
};

// ----------------------------------------------------------------------

inline const std::vector<::vk::PipelineShaderStageCreateInfo> of::vk::Shader::getShaderStageCreateInfo(){

	std::vector<::vk::PipelineShaderStageCreateInfo> stageInfo;
	stageInfo.reserve( mShaderStages.size() );

	for ( const auto& s : mShaderStages ){
		stageInfo.push_back( s.second->createInfo );
	}

	return stageInfo;
}

// ----------------------------------------------------------------------

inline const std::vector<of::vk::Shader::DescriptorSetLayoutInfo>& of::vk::Shader::getDescriptorSetsInfo(){
	return mDescriptorSetsInfo;
}

// ----------------------------------------------------------------------

inline const ::vk::PipelineVertexInputStateCreateInfo & of::vk::Shader::getVertexInputState(){
	return mVertexInfo.vi;
}

// ----------------------------------------------------------------------

inline const std::vector<std::string> & of::vk::Shader::getAttributeNames(){
	return mVertexInfo.attributeNames;
}

// ----------------------------------------------------------------------

inline bool of::vk::Shader::getAttributeBinding( const std::string &name, size_t &index ) const{
	auto result = mAttributeBindingNumbers.find( name );
	
	if ( result == mAttributeBindingNumbers.end() ){
		return false;
	} else{
		index = result->second;
		return true;
	};
}

// ----------------------------------------------------------------------

inline const of::vk::Shader::VertexInfo & of::vk::Shader::getVertexInfo(){
	return mVertexInfo;
}

// ----------------------------------------------------------------------

inline const std::shared_ptr<::vk::PipelineLayout>& Shader::getPipelineLayout() {
	if ( mPipelineLayout.get() == nullptr )
		createVkPipelineLayout();
	return mPipelineLayout;
}

// ----------------------------------------------------------------------

inline const std::vector<uint64_t>& Shader::getDescriptorSetLayoutKeys() const{
	return mDescriptorSetLayoutKeys;
}

// ----------------------------------------------------------------------

inline const std::vector<std::shared_ptr<::vk::DescriptorSetLayout>>& of::vk::Shader::getDescriptorSetLayouts() const{
	return mDescriptorSetLayouts;
}

// ----------------------------------------------------------------------

inline const std::shared_ptr<::vk::DescriptorSetLayout>& of::vk::Shader::getDescriptorSetLayout( size_t setId ) const{
	return mDescriptorSetLayouts.at( setId );
}

// ----------------------------------------------------------------------

} // namespace vk
} // namespace of
