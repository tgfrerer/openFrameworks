
# Experimental Renderer using the Vulkan API

This renderer is more than experimental. Expect everything to change
all the time. Send pull requests to influence what changes.

## Setup 

This has been initially developed on: 
     +  Windows 10/64bit, NVIDIA GTX 980 (Vulkan API 1.0.8), Vulkan SDK 1.0.13

1. Install the Vulkan SDK from LunarG

   download from: https://vulkan.lunarg.com

2. update GLFW using apothecary
   
	cd openFrameworks/scripts/apothecary
   	./apothecary update glfw

   Check the apothecary headers to make sure they are at version 3.2

3. In apps/devApps you'll find a project called `testVk`, that's where
   some things can be tested.

## The Grand Plan

This renderer allows us to use Vulkan with openFrameworks. 

Since Vulkan is a much more explicit API than OpenGL and a rather
fundamental architectural shift, this Renderer does not aim to be a
replacement for OpenGL, but a modern middleware layer for using Vulkan
within an openFrameworks environment.

Vulkan assumes that you know what you are doing. It also assumes that
you are very very explicit about what you are doing.

This renderer tries to keep things fairly fun by taking care of the
boilerplate, and to provide - where this makes sense - prefabricated
building blocks using sensible defaults.

The idea is that you might want to prototype your app by slapping
together some of these building blocks, and then, replacing some of
these blocks (the ones that actually make a difference for your
project) with customised elements.

The goal is to provide generators for a whide range of Vulkan objects
which can be used together or not at all if need be. On top of these
Vulkan objects, and using these generators we create a default
renderer environment (see Context), which may function as a starting
point for exploration. Advanced users will probably want to write
their own scene graphs, renderer addons etc. Great! We should make
sure that this is possible.
 
To make sure that the basic elements and helper methods inside
openFrameworks/vk have maximum compatiblity with 1) each other, 2) the
Vulkan Api, and 3) possible 3rd party middleware libraries, the
function returns and parameters aim to be *undecorated* Vulkan API
variables. Think of it as a C-style API. It's not super pretty and
clever, but it's bound to work everywhere.

That means, the openFrameworks Vulkan middle layer will aim not to
produce objects within shared pointers or even to encapsulate Vulkan
objects with "helper" classes.

## Context.h

To facilitate some simulacrum of old-style OpenGL immediate mode and
to ease the transition, there is a Context class which is responsible
to deal with tracking drawing state and to translate this into
meaningful Vulkan Command buffers and pipeline changes. 

Context also has a matrix stack, which makes it possible to use the
familiar ofPush/ofPopMatrix methods here.

The aim for Context should be to provide a friendly environment to
quickly prototype drawing using vulkan.

## SPIR-V Cross

With Vulkan, a new intermediary shader language, SPIR-V was
introduced. Vulkan only accepts SPIR-V as shader language. SPIR-V
files come precompiled, as a blob of 32bit words.

You generate these from GLSL source using the
`glslLangValidator`, part of the Vulkan SDK. There is also a Google
sponsored tool out there, `shaderc` which does more or less the same
thing, but is slightly more powerful when it comes to parsing include
files. It's likely that other front-ends will be added to SPIR-V, and
you might be able to write shader code in a number of other languages,
that will compile down to SPIR-V. So SPIR-V is what any shader
language eventually boils down to.

Spir-V Cross, a new static dependency added to openFrameworks/vk
allows us to do reflection at runtime on these shader programs. This
makes it possible to derive our pipeline binding points much less
painfully, and on the fly.

It could also allow us to cross-compile spirv shader code to GLSL or
even .cpp if we wanted, which is pretty nifty.

Currently spir-v cross is provided as a static windows 64 bit lib for
release and debug taragets. It's worth investigating to create an
apothecary recipe to generate a dynamic library (one lib for debug
*and* release) which would make it possible to track development of
SPIR-V cross more closely.

## Vulkan Quirks

+ In Screen Space, Vulkan flips Y, compared to OpenGL. If you set up
  your camera to do flipping, you should be fine again.

+ Vulkan does not use an unit cube for the view frustum, the frustum
  cube is mapped to x: -1..+1, y: -1..+1, z: 0..1