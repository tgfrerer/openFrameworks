# Precompiled default shaders for openFrameworks VK Renderer

The shaders in this folder are used internally by the VK renderer. 
We provide shaders as GLSL source - and SPIRV, and source and 
SPIRV must be in sync.

To compile shaders we use glslc, which is the front-end for 
standalone shaderc:

	glslc default.vert default.frag -c -mfmt=num

Note the '-mfmt=num' parameter - this stores spirv in text form 
as a list uint32_t literals.

We compile shaders into spir-v because it accelerates load times, 
and also allows us to inline shader code as binary blobs into 
applications:
			 
	// Include shader source into binary 
	static const std::vector<uin32_t> myShader {
		#include "myshadersource.spv"
	}
