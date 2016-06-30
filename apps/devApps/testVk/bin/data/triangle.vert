#version 420 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// uniforms (resources)
layout (set = 0, binding = 0) uniform DefaultMatrices 
{
	mat4 projectionMatrix;
	mat4 modelMatrix;
	mat4 viewMatrix;
} ubo;

// inputs (vertex attributes)
layout (set = 0, location = 0) in vec3 inPos;
layout (set = 0, location = 1) in vec4 inColor;
layout (set = 0, location = 2) in vec3 inNormal;

// outputs 
layout (location = 0) out vec4 outColor;
layout (location = 1) out vec3 outNormal;

void main() 
{
	outNormal   = (inverse(transpose( ubo.viewMatrix * ubo.modelMatrix)) * vec4(inNormal, 0.0)).xyz;
	outColor    = inColor;
	gl_Position = ubo.projectionMatrix * ubo.viewMatrix * ubo.modelMatrix * vec4(inPos.xyz, 1.0);
}
