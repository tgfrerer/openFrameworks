#version 450 core

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

#include "shaders/noise.glsl"

// uniforms (resources)
layout (set = 0, binding = 0) uniform DefaultMatrices 
{
	mat4 projectionMatrix;
	mat4 modelMatrix;
	mat4 viewMatrix;
}; // note: if you don't specify a variable name for the block its elements will live in the global namespace.

layout (set = 0, binding = 1) uniform Style
{
	vec4  globalColor;
	float timeInterval; // time in 3 sec interval
};

// inputs (vertex attributes)
layout ( location = 0) in vec3 inPos;
layout ( location = 1) in vec2 inTexCoord;

// outputs 
layout (location = 0) out vec4 outColor;
layout (location = 1) out vec2 outTexCoord;
layout (location = 2) out vec3 outNormal;

// we override the built-in fixed function outputs
// to have more control over the SPIR-V code created.
out gl_PerVertex
{
    vec4 gl_Position;
};

const float TWO_PI = 2 * 3.1415168f;

void main() 
{
	
	outTexCoord = inTexCoord * vec2(1, 0.0025f) + vec2(0, gl_InstanceIndex * 0.0025);
	outColor    = globalColor;

	int instanceId = gl_InstanceIndex % 30;

	vec3 perInstancePos = inPos.xyz + vec3(0, (gl_InstanceIndex + 1) * -1.0 + 200.f, 0);

	float maxAmplitude = 100.f;

	float dummy = timeInterval;
	
	float waveProgress = (3. - timeInterval/3.f) * TWO_PI + 200.f * 3.1415168f * (perInstancePos.x + perInstancePos.y );

	float noiseSample = snoise(vec2(perInstancePos.x/-250, perInstancePos.y/5.f)) * 0.2;

	float noiseAmplitude = 20  
		* (step(15, instanceId) * 2 -1)
		* smoothstep(1.f-abs(noiseSample), 1., perInstancePos.x/-500);
	
	maxAmplitude += noiseAmplitude;

	perInstancePos.z = sin(waveProgress) * maxAmplitude  * (perInstancePos.x * 0.001);
	
	float firstDerivativeZ = (cos(waveProgress) * maxAmplitude * (perInstancePos.x * 0.001));
	vec3 calculatedNormal = normalize(vec3(firstDerivativeZ, 0, maxAmplitude + 20 + 1));

	outNormal = (inverse(transpose( viewMatrix * modelMatrix)) * vec4(calculatedNormal,0.0)).xyz;

	gl_Position = projectionMatrix * viewMatrix * modelMatrix * vec4(perInstancePos, 1.0);
}
