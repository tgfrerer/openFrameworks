#version 420 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (set = 0, location = 0) in vec4 inColor;
layout (set = 0, location = 1) in vec3 inNormal;

// uniforms (resources)
layout (binding = 0) uniform DefaultMatrices 
{
	mat4 projectionMatrix;
	mat4 modelMatrix;
	mat4 viewMatrix;
} ubo;

layout (location = 0) out vec4 outFragColor;

void main() 
{

  vec4 normalColor = vec4((inNormal + vec3(1.0)) * vec3(0.5) , 1.0);
  vec4 vertexColor = inColor;
  outFragColor = normalColor;
}