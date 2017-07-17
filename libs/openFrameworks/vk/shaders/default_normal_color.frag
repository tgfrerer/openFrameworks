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

// inputs
layout ( location = 0 ) in vec4 inPos;
layout ( location = 1 ) in vec3 inNormal;
layout ( location = 2 ) in vec2 inTexCoord;

// outputs
layout ( location = 0 ) out vec4 outFragColor;

void main() 
{
	vec4 normalColor = vec4(0.5 * (inNormal + vec3(1)),1.);
	outFragColor = normalColor;
}