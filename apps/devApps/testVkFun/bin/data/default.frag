#version 420 core

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

layout (location = 0) in vec4 inPosition;
layout (location = 1) flat in vec3 inNormal;

layout (location = 0) out vec4 outFragColor;

void main() 
{

  vec4 lightPos = viewMatrix * vec4(-2000,100,2000,1);

  vec3 normal = normalize(inNormal);

  vec3 l = normalize(lightPos - inPosition).xyz;

  float lambert = dot(l,normal);

  vec3 normalColor = (inNormal + vec3(1.0)) * vec3(0.5);
  outFragColor = vec4(normalColor,1);
  
  outFragColor = vec4(vec3(1,0,0) * lambert, 1);
}