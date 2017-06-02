#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#include <shaders/gradient.glsl>

// inputs 
layout (location = 0) in vec2 inTexCoord;

// outputs
layout (location = 0) out vec4 outFragColor;

void main(){

	float dist = 1.f - inTexCoord.y;

	vec4 c1 = vec4(vec3(253,231,104)*1/255.,0.75);
	vec4 c2 = vec4(vec3(63,226,246)*1/255.,.1);

	vec3 outColor = getGradient(c1, c2, dist);
	// dist = pow(dist,0.2);

	outFragColor = vec4(outColor, 1.0);
}