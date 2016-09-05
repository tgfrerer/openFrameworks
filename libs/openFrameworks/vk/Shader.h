#pragma once
#include <string>
#include <map>
#include "vulkan/vulkan.hpp"
#include "vk/spirv-cross/include/spirv_cross.hpp"

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

class ShaderManager;

class Shader
{
public:

	const struct Settings
	{
		std::shared_ptr<of::vk::ShaderManager> shaderManager;
		std::map<::vk::ShaderStageFlagBits, std::string> sources;
	} mSettings;

	// we need a table of uniforms
	struct UboMemberRange
	{
		size_t offset;
		size_t range;
	};

	// Metadata for descriptor 
	// - descriptor can be of type dynamic_uniform or combined_image_sampler
	struct DescriptorInfo
	{
		using MemberMap = std::map< std::string, of::vk::Shader::UboMemberRange>;
		
		::vk::DescriptorType    type;                                /* used for hash */
		uint32_t                count = 0;                           /* used for hash */
		size_t                  storageSize = 0;                     /* used for hash */
		std::string             name;                                /* used for hash */
		
		::vk::ShaderStageFlags  stageFlags;
		MemberMap               memberRanges; // optional: memory offsets for members within UBO, indexed by name
		
		uint64_t                hash = 0;                            /* hash of type, count, name, storageSize */
		
		void calculateHash();
		
		bool checkMemberRangesOverlap( const MemberMap& lhs, const MemberMap & rhs, std::ostringstream & errorMsg) const;
	};

	struct UniformBindingInfo
	{
		uint32_t setNumber     = 0;
		uint32_t bindingNumber = 0;
	};

	// (set id and binding number of uniform) indexed by DescriptorInfo hash
	std::map<uint64_t, UniformBindingInfo> mBindingsTable;

	struct SetLayoutInfo
	{
		// binding number to DescriptorInfo hash - binding numbers may be sparse.
		std::map<uint32_t, uint64_t> bindingTable;

		uint64_t hash = 0;
		void calculateHash();
	};


private:
	
	// table of SetLayoutInfo pointers to keep track of use for Set Layout
	std::vector<std::shared_ptr<SetLayoutInfo>> mPipelineLayoutPtrsMeta;
	
	// table of setlayouts describing pipeline layout
	std::vector<uint64_t> mPipelineLayoutMeta;

	// alias into mSettings
	const std::shared_ptr<ShaderManager>& mShaderManager = mSettings.shaderManager;


public: 

	struct VertexInfo
	{
		std::vector<::vk::VertexInputBindingDescription>   bindingDescription;	  // describes data input parameters for pipeline slots
		std::vector<::vk::VertexInputAttributeDescription> attribute;	          // mapping of attribute locations to pipeline slots
		::vk::PipelineVertexInputStateCreateInfo vi;
	} mVertexInfo;

	

private:

	mutable std::shared_ptr<::vk::PipelineLayout> mPipelineLayout;
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

	// Sequence of hashes of SetLayouts - which reference vkDescriptorSetLayouts in Context.
	// This describes the sequence for the pipeline layout for this shader.
	// std::vector<uint64_t>  mDescriptorSetLayoutKeys;  

	// ----------------------------------------------------------------------
	// Derive bindings from shader reflection using SPIR-V Cross.
	// we want to extract as much information out of the shader metadata as possible
	// all this data helps us to create descriptors, and also to create layouts fit
	// for our pipelines.
	void reflect( const std::map<::vk::ShaderStageFlagBits, std::shared_ptr<spirv_cross::Compiler>>& compilers, VertexInfo& vertexInfo );
	//static void reflectUniformBuffers( const spirv_cross::Compiler & compiler, const VkShaderStageFlagBits & shaderStage, std::map<std::string, BindingInfo>& uniformInfo );
	
	bool reflectUBOs( const spirv_cross::Compiler & compiler, const ::vk::ShaderStageFlagBits & shaderStage );
	bool reflectSamplers( const spirv_cross::Compiler & compiler, const ::vk::ShaderStageFlagBits & shaderStage);
	bool addResourceToBindingsTable( const spirv_cross::Compiler & compiler, const spirv_cross::Resource & ubo, std::shared_ptr<of::vk::Shader::DescriptorInfo> & uniform );
	bool createSetLayouts();
	void createVkPipelineLayout();

	static void getSetAndBindingNumber( const spirv_cross::Compiler & compiler, const spirv_cross::Resource & ubo, uint32_t &descriptor_set, uint32_t &bindingNumber );
	static void reflectVertexInputs(const spirv_cross::Compiler & compiler, VertexInfo& vertexInfo );


	// based on file name ending, read either spirv or glsl file and fill vector of spirV words
	bool getSpirV( const ::vk::ShaderStageFlagBits shaderType, const std::string & fileName, std::vector<uint32_t> &spirCode );
	
	// find out if module is dirty
	bool isSpirCodeDirty( const ::vk::ShaderStageFlagBits shaderStage, uint64_t spirvHash );

	// create vkShader module from binary spirv code
	void createVkShaderModule( const ::vk::ShaderStageFlagBits shaderType, const std::vector<uint32_t> &spirCode);


public:


	// ----------------------------------------------------------------------

	// shader object needs to be initialised based on spir-v sources to be useful
	Shader( const Settings& settings_ );

	// ----------------------------------------------------------------------

	~Shader()
	{
		mSpvCrossCompilers.clear();
		mShaderStages.clear();
	}

	// re-compile shader - this invalidates hashes only when shader has changed.
	void compile();


	// ----------------------------------------------------------------------

	const std::vector<uint64_t>& getSetLayoutKeys() const{
		return mPipelineLayoutMeta;
	}

	// ----------------------------------------------------------------------

	// return vector create info for all shader modules which compiled successfully
	const std::vector<::vk::PipelineShaderStageCreateInfo> getShaderStageCreateInfo(){
		
		std::vector<::vk::PipelineShaderStageCreateInfo> stageInfo;
		stageInfo.reserve( mShaderStages.size() );
		
		for ( const auto& s : mShaderStages ){
			stageInfo.push_back( s.second->createInfo );
		}

		return stageInfo;
	}

	// ----------------------------------------------------------------------
	// return vertex input state create info
	// this hold the layout and wiring of attribute inputs to vertex bindings
	const ::vk::PipelineVertexInputStateCreateInfo& getVertexInputState(){
		return mVertexInfo.vi;
	}
	
	// ----------------------------------------------------------------------
	const std::shared_ptr<::vk::PipelineLayout>& getPipelineLayout() {
		if ( mPipelineLayout.get() == nullptr )
			createVkPipelineLayout();
		return mPipelineLayout;
	}

	// ----------------------------------------------------------------------
	// returns hash of spirv code over all shader shader stages
	const uint64_t getShaderCodeHash();
};

} // namespace vk
} // namespace of
