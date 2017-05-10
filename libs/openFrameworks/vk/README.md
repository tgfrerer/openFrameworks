
# Experimental Renderer using the Vulkan API

This renderer and its API is experimental. Expect things to change without warning
all the time.

## Switch between GL and VK rendering mode

To switch between Vulkan and GL for rendering API, toggle the
following `#define` near the top of `ofConstants.h`:

    #define OF_TARGET_API_VULKAN

Unfortunately, because of how `ofGLFWWindow` is organised currently, it
is not trivial to switch between Vulkan and GL for target APIs. Using
the #define seemed the most straightforward way to do it, but ideally,
you should be able to feed an `ofVkWindowSettings` object to
`ofCreateWindow` and that should be it. A future feature.

## Setup 

This has been developed and tested on Vulkan SDK 1.0.8 up to Vulkan SDK 1.0.46, on Windows, and Linux (ubuntu 16.06), with NVIDIA drivers. Other Vulkan capable systems/GPUs are expected to work, most proably requiring slight modifications. 


### Install the Vulkan SDK from LunarG

Download the matching SDK for your system from: https://vulkan.lunarg.com

It is recommended that you download an **Vulkan SDK version of 1.0.46 or above**. The Vulkan SDK library search paths have changed somewhere around SDK verions 1.0.42 and above - and the openFrameworks Vulkan renderer expects you're running the latest Vulkan SDK.

#### Windows 

* Run the installer .exe to install the SDK. 
* Check if the vulkan runtime installed successfully.

This installer should have automatically installed VulkanRT, which is the Vulkan runtime. If not, check out the toubleshooting section.
	
##### Troubleshooting: [Windows, sdk 1.0.21.1] 

Check VulkanRT install- I ran into a repeated issue with the powershell script included with the VulkanRT installer failing to execute. This script, `.\ConfigLayersAndVulkanDLL.ps1` is responsible for setting up some registry values to tell the Vulkan loader where to find the validation layers. I found that manually executing the script twice (first run fails) using an Admin Powershell console helped. 

    cd "C:\Program Files (x86)\VulkanRT\1.0.21.1"
    .\ConfigLayersAndVulkanDLL.ps1 1 64
    # and then again!
    .\ConfigLayersAndVulkanDLL.ps1 1 64
    # this time there should be no errors.

##### Troubleshooting: [Windows, SDK search paths] 

I saw that under windows sometimes the vulkan search paths are not properly set. If your app crashes in debug mode because vulkan layers cannot be found, type 

    echo $VK_SDK_PATH

and 

    echo $VK_LAYER_PATH

To see if these search paths are pointing to where you installed the Vulkan SDK. By default, `VK_LAYER_PATH` should point to a subdirectory of `VK_SDK_PATH`, named `Bin`

#### Linux 

* Run the SDK archive package - it will expand into a folder, 
* Copy it into `~/sdks/VulkanSDK`

Now you will need to set some runtime variables for debug programs so that the debug layers can be found. I'm using a shell script to set up these system-wide ENV variables. Note that you will have to replace the correct path to your latest vulkan sdk directory: 

    # This script lives at: /etc/profile.d/vk-environment.sh
    # Set VULKAN environment variables. 
    export VULKAN_SDK=/home/tim/sdks/VulkanSDK/1.0.21.1/x86_64  # <-- replace with your own path to VK_SDK here
    export PATH="$VULKAN_SDK/bin:$PATH"
    export LD_LIBRARY_PATH=$VULKAN_SDK/lib
    export VK_LAYER_PATH=$VULKAN_SDK/etc/explicit_layer.d

You might have to log out and back in again, for the variables to take effect. 

Run `vulkaninfo` to see which version of the SDK you are running. (It will say it in the fist line.) 

### Install openFrameworks dependencies

For Windows, use the command line to navigate to the `scripts/vs` directory. There, execute:

    powershell -File download_libs.ps1
    
This will update and install the latest library dependencies used for openFrameworks, precompiled.

### Clone apothecary

Apothecary is openFrameworks' dependency tracker and libraries build helper. For openFrameworks-vk it is needed to build the latest versions of `GLFW`, and `shaderc`.

To get apothecary with some extra build recipes needed for Vulkan, clone it from here:

    git clone https://github.com/openFrameworks-vk/apothecary apothecary

Apothecary requires a linux-like environment, on Windows, the most reliable for apothecary is MinGW64, wich comes bundled when you install git for windows. (https://git-for-windows.github.io/). 

Then, move into the apothecary base directory.

    cd apothecary/apothecary

### Update GLFW dependency using apothecary
  
For Windows, visual studio 2015, and 64 bit do:

    ./apothecary -a 64 -t vs update glfw

For Linux do: 

    ./apothecary -a 64 update glfw

### Update/Create shaderc dependency using apothecary

If you are on windows, you might want to check if apothecary has access to python. Python is required to build shaderc. In a mingw terminal, issue: 

    python --version

If python is installed, you should see a version number, otherwise, install python for windows (3.5+) from here: https://www.python.org/downloads/windows/. Make sure you tick "Add Python to PATH" so that python is accessible from the console.

To compile shaderc, for Windows, visual studio 2015, and 64 bit (recommended) do:

    ./apothecary -a 64 -t vs update shaderc

If compiling fails for any reason, delete the build folder in apothecary, read over the instructions again, and see if something might have been missed, then issue the above command again. I found that on Windows, with the ConEmu terminal manager, the PATH for python was not set correctly, and that running apothecary from the default "git for windows" console worked flawlessly.
 
### Copy dependencies 

Once you have the dependencies compiled, move to the base apothecary directory, where you will find two new directories with the build results: 

    glfw
    shaderc

copy these two directories into: 

    openframeworks-vk/libs/

when asked, select "replace" to overwrite the old libraries in-place.

### Open Test Project 

In apps/devApps you'll find a project called `testVk`, that's the current example for Vulkan.

----------------------------------------------------------------------

## The Grand Plan

This renderer adds Vulkan support to openFrameworks. 

Since Vulkan is a much more explicit API than OpenGL and a rather
fundamental architectural shift, this Renderer does not aim to be a
replacement for OpenGL, but a modern middle layer for rendering, processing & tinkering with Vulkan within an openFrameworks environment.

Vulkan assumes that you know what you are doing. It also assumes that
you are very, very explicit about what you are doing.

This renderer tries to keep things fairly fun by taking care of the
boilerplate, and to provide - where this makes sense - prefabricated
building blocks pre-initialised to sensible defaults.

The idea is that you might want to prototype your app by slapping
together some of these building blocks, and then, replacing some of
these blocks (the ones that actually make a difference in speed, usability or aesthetics) with customised elements.

Advanced users will probably want to write their own scene graphs, renderer addons etc. Great! We should make sure that this is possible.

----------------------------------------------------------------------

## Architecture

Since Vulkan is not backwards-compatible with OpenGL, the renderer does not have this ambition either, and instead aims at making Vulkan more accessible, with an emphasis on things (shader hot-reloading, etc.) that may be useful for openFrameworks.

Some architectural ideas are borrowed from modern engines and projects such as [bgfx][bgfx] or [regl.party][regl], which I highly suggest studying. 

A key element in these modern approaches is to get away from stateful immediate-mode drawing as in "classic" OpenGL, to a more declarative state-less, data-driven, approach. This makes it possible engine-side to do a lot of optimisations underneath, and fits much better into how Vulkan itself is laid out.

In principle, you first define *how* something will be drawn (what shader, what primitive mode, whether you want to use a depth test), and all these choices become consolidated and compiled into pipeline.

Then, you define *what* to draw, which is the data in data-driven, so to say.

### DrawCommand

In the openFrameworks Vulkan renderer, a `DrawCommand` holds all the data needed to draw using the pipeline the `DrawCommand` was created from: (which mesh to draw, uniform settings for the shader etc). Notice that this broadly maps to the uniforms and attributes, samplers, etc. declared in the GLSL code for the shader you're using to draw.

### RenderBatch

To send a `DrawCommand` through the pipeline, you first need to create a `RenderBatch`. This is an object which helps accumulate multiple draw commands, and forward them down the engine in one go. A `RenderBatch` is a temporary object, and it is created from a `Context`.

----------------------------------------------------------------------

### Context

A `Context` is an isolated environment where temporary objects and buffer memory can
be allocated in an optimised way.

A `Context` keeps memory isolated per *Virtual Frame*. A Virtual frame is protected by a
`vk::Fence` which is set on `Context::end()` and waited upon `Context::begin()`. 
It is therefore safe to assume that a Context is ready for write after `Context::begin()`.

All `Context` operations are meant to be thread-safe 
as long as the `Context` never leaves its home thread. All objects allocated through 
a context are synchronised using a fence which keeps all objects alife until the 
frame is re-visited.

A `Context` may be initialised using a `vk::Renderpass`, so it knows where the results of draw operations will be stored. The default `Context` draws to the screen, and you can grab it by calling the `ofVkRenderer`: 

    auto renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );
    auto & context = renderer->getDefaultContext()

To render using a `Context`, create a `RenderBatch` from the Context, `.begin()` the RenderBatch, and add `DrawCommand`s into it by calling `.draw(myDrawCommand)`. When you are done drawing, `.end()` the RenderBatch. Renderbatches are there to accumulate draw commands. 

When the RenderBatch ends, its internal queue of DrawCommands is translated into a single `vk::Commandbuffer`, which is in turn queued inside the Context.

	of::vk::RenderBatch batch{ *context };

	batch.begin();
	batch.draw( myDrawCommand_1 );
    batch.draw( myDrawCommand_2 );
    batch.draw( myDrawCommand_3 );
    // ...
	batch.end();


When the Context ends, its internal queue of `vk::CommandBuffer` is submitted 
to the `vk::Queue` for rendering. 

----------------------------------------------------------------------

## Allocator

Vulkan requires you to do your own memory management and allocations.

Allocator is a simple, linear GPU memory allocator written especially for dynamic memory. Dynamic memory is memory which changes every frame (think your matrices, and uniforms), in contrast to memory which is static (think your scene geometry). 

For the context, the Allocator will pre-allocate about 32MB of memory *per swapchain frame* and map this to a *single* buffer and a *single* chunk of physical memory. 

Whenever you request memory from the Allocator, it returns an address to mapped CPU memory you can write into - and an offset into the GPU buffer which you can use to offset your draw binding position to use the memory you 
just wrote. 

We're doing this because the cost of allocating memory is immense compared 
to everything else, and the complexity of having lots of buffers flying 
around is mind-boggling. We're allocating all dynamic memory on renderer setup. And we're returning all the memory on renderer teardown. 

By using a single buffer we can move all memory for a frame from CPU visible memory to GPU-only visible memory in parallel - on a transfer queue if that makes sense. The transfer is for the whole range of a frame (32 MB) - and ideally done on a paralell, transfer-only queue. This is a common technique for triple-buffering. 

Most cards without special VRAM such as Intel integrated cards are perfectly happy not to do this, but there might be benefits for NVidia cards for example, which have faster, GPU-only visible memory.



----------------------------------------------------------------------

## SPIR-V Cross

Vulkan introduces a new intermediary shader language, SPIR-V. Vulkan only accepts SPIR-V as shader language. SPIR-V files come precompiled, as a blob of 32bit words.

You can generate SPIR-V from GLSL source using the `glslLangValidator`, which is part of the Vulkan SDK. 

Spir-V Cross, which is included in source within openFrameworks/vk,
allows us to do reflection at runtime on these shader programs. This
makes it possible to derive our pipeline binding points much less
painfully, and on the fly.

It could also allow us to cross-compile spirv shader code to GLSL or
even .cpp if we wanted, which is pretty nifty.

----------------------------------------------------------------------

## ShaderC

To compile glsl shader code into SPIR-V, we include shaderc, a static library which is the reference GLSL shader compiler with some helpers and syntactic sugar added by Google, who maintain this project. 

There is a recipe for shaderc included in `https://github.com/openframeworks-vk/apothecary`. The recipe file contains hints in the comments on the command line parameters to invoke on the recipe depending on the target operating system.

This allows us to ingest shaders as GLSL, and to print out error messages if there was a compilation error. If there was a compilation error, `vk::Shader` will print out the context of the offending line alongside the compilation message. If a previous version of the shader compiled successfully, shader compilation aborts at this point, and the previous shader is used for rendering until the new version compiles successfully. This helps when prototyping. 

The Vulkan renderer uses shaderC to make `#include` statements possible in glsl shader code. Any errors found when compiling includes is printed out with the correct line number of the offending include file, together with some lines of context of where the shader error was found.

----------------------------------------------------------------------

## Vulkan Quirks

+ In Screen Space, Vulkan flips Y, compared to OpenGL.
+ Vulkan does not use an unit cube for the view frustum, the frustum
  has half-depth (z does from 0 to +1 instead of -1 to +1)
+ To deal with the two points above, we pre-multiply the projection
  matrix with a clip matrix:

```cpp
ofMatrix4x4 clip(1.0f,  0.0f, 0.0f, 0.0f,
                 0.0f, -1.0f, 0.0f, 0.0f,
                 0.0f,  0.0f, 0.5f, 0.0f,
                 0.0f,  0.0f, 0.5f, 1.0f);
```

----------------------------------------------------------------------

# Vulkan Resources

* Khronos Vulkan spec ([HTML][spec])
* [Awesome Vulkan][awesome] -- A curated collection of links to resources around Vulkan

----------------------------------------------------------------------

# Design Principles

1. Make it correct
3. Make it extensible
2. Make it simple(r)
4. Optimise

----------------------------------------------------------------------

[spec]: https://www.khronos.org/registry/vulkan/specs/1.0-extensions/html/vkspec.html
[awesome]: https://github.com/vinjn/awesome-vulkan
[regl]: http://regl.party/
[bgfx]: https://github.com/bkaradzic/bgfx
