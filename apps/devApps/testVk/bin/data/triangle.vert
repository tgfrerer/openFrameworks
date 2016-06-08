#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs
layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inColor;

// outputs
layout (location = 0) flat out vec3 outColor;

// uniforms (resources)
layout (binding = 0) uniform DefaultMatrices 
{
	mat4 projectionMatrix;
	mat4 modelMatrix;
	mat4 viewMatrix;
} ubo;


void main() 
{
	outColor = ((inverse(transpose( ubo.viewMatrix * ubo.modelMatrix)) * vec4(inColor,0) ).xyz + vec3(1.0)) * vec3(0.5);
	gl_Position = ubo.projectionMatrix * ubo.viewMatrix * ubo.modelMatrix * vec4(inPos.xyz, 1.0);
}
