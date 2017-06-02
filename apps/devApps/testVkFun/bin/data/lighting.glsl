#ifndef LIGHTING_GLSL
#define LIGHTING_GLSL

// Dummy lighting calculations include file
// We use this to demo how shader includes work

float lambert(in vec3 n, in vec3 l){
  return dot(n,l);
}

#endif // ifndef LIGHTING_GLSL