#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

struct Particle {
	vec2 pos;
	vec2 vel;
	vec4 result;
};


// Binding 0 : Position storage buffer
layout(std430, binding = 0) buffer ParticleBuf 
{
   Particle particles[ ];
};

layout(set=0, binding=1) uniform Parameters {
	uint flipFlop; // will be either 0 or 1
};


layout (local_size_x = 1, local_size_y = 1) in;

void main(){
	 // uint srcIndex = (flipFlop )          * gl_GlobalInvocationID.x;
	 // uint dstIndex = ((flipFlop + 1) % 2) * gl_GlobalInvocationID.x;
	 uint srcIndex = (flipFlop )          ;
	 uint dstIndex = ((flipFlop + 1) % 2) ;

	 particles[dstIndex].pos.xy = particles[srcIndex].pos + particles[srcIndex].vel;
	 

}