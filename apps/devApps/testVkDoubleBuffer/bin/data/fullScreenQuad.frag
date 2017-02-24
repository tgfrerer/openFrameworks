#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

void main(){

	float dist = length(inTexCoord - vec2(0.5)) / sqrt(2.);

	outFragColor = vec4(vec3(1.0-dist), 1.0);
}