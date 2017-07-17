#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// uniforms (resources)
layout (set = 0, binding = 0) uniform DefaultMatrices 
{
	mat4 projectionMatrix;
	mat4 modelMatrix;
	mat4 viewMatrix;
}; 

layout (set = 0, binding = 1) uniform Style
{
	vec4 globalColor;
} style;

// inputs (vertex attributes)
layout ( location = 0 ) in vec3 inPos;
layout ( location = 1 ) in vec3 inNormal;
layout ( location = 2 ) in vec2 inTexCoord;

// output (to next shader stage)
layout ( location = 0 ) out vec4 outPos;
layout ( location = 1 ) out vec3 outNormal;
layout ( location = 2 ) out vec2 outTexCoord;

// we override the built-in fixed function outputs
// to have more control over the SPIR-V code created.
out gl_PerVertex
{
    vec4 gl_Position;
};

void main() 
{
	// Use globalColor so compiler does not remove it automatically.
	vec4 colorSink       = style.globalColor * 1; 
	mat4 modelViewMatrix = viewMatrix * modelMatrix;
	
	outTexCoord = inTexCoord;
	outNormal   = (transpose(inverse(modelViewMatrix)) * vec4(inNormal,1.f)).xyz;
	outPos      = modelViewMatrix * vec4(inPos.xyz, 1.0);
	
	gl_Position = projectionMatrix * outPos;
}
