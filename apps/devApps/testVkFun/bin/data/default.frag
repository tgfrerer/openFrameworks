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

layout (std430, set = 0, binding = 1)  buffer colorLayout 
{
  vec4 colourList[];
};

layout (location = 0) in vec4 inPosition;
layout (location = 1) flat in vec3 inNormal;

layout (location = 0) out vec4 outFragColor;

// include external glsl shader source file
#include "lighting.glsl"

void main() 
{

  // We do light calculations in eye==camera space
  vec4 lightPos = viewMatrix * vec4(-2000,100,2000,1);

  vec3 normal = normalize(inNormal);

  vec3 l = normalize(lightPos - inPosition).xyz;

  vec3 normalColor = (inNormal + vec3(1.0)) * vec3(0.5);
  // outFragColor = vec4(normalColor,1);
  
  vec3 plainColor =  colourList[1].rgb;

  outFragColor = vec4(plainColor * lambert(l,normal), 1);
}