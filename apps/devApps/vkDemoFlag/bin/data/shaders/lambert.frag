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
layout (location = 1) in vec4 inPos;
layout (location = 2) in vec3 inNormal;

// outputs
layout (location = 0) out vec4 outFragColor;

void main() 
{

  vec4 lightPos = vec4(1000,1000,1000,1); // world space
  lightPos = viewMatrix * lightPos;    // light in view space

  vec3 l = normalize(lightPos.xyz - inPos.xyz); // vector from fragment, to light
  vec3 n = normalize(inNormal);

  float lambert = dot(l,n);

  vec4 normalColor = vec4((n + vec3(1.0)) * vec3(0.5) , 1.0);
  vec4 albedo = vec4(inColor.rgb * lambert, 1);

  // set the actual fragment color here
  outFragColor = albedo;
  outFragColor = normalColor;
}