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

// inputs from earlier shader stages
layout (location = 0) in vec4 inColor;
layout (location = 1) in vec3 inNormal;

// outputs
layout (location = 0) out vec4 outFragColor;

void main() 
{

  vec4 normalColor = vec4((inNormal + vec3(1.0)) * vec3(0.5) , 1.0);
  vec4 vertexColor = inColor;

  // set the actual fragment color here
  outFragColor = vertexColor;
}