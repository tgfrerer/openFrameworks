#pragma once
#include "ofBaseTypes.h"
#include "ofPolyline.h"
#include "ofMatrix4x4.h"
#include "ofMatrixStack.h"
#include "of3dGraphics.h"
#include "ofBitmapFont.h"
#include "ofPath.h"
#include "ofMaterial.h"
#include <vulkan/vulkan.h>
#include "vk/Swapchain.h"
#include "vk/Context.h"

class ofShapeTessellation;
class ofMesh;

namespace of{
namespace vk{
	class Shader;
}
};


class ofVkRenderer : public ofBaseRenderer
{

	bool bBackgroundAuto;
	bool wrongUseLoggedOnce;

	const ofBaseMaterial * currentMaterial;
	int alphaMaskTextureTarget;

	ofStyle currentStyle;
	deque <ofStyle> styleHistory;
	of3dGraphics m3dGraphics;
	ofBitmapFont bitmapFont;
	ofPath mPath;
	const ofAppBaseWindow * window;


public:
	static const string TYPE;

	ofVkRenderer( const ofAppBaseWindow * window );
	void setup();
	virtual ~ofVkRenderer() override;


	virtual const string & getType() override{
		return TYPE;
	};

	virtual void startRender() override;
	virtual void finishRender() override;

	virtual void draw( const ofPolyline & poly ) const{};
	virtual void draw( const ofPath & shape ) const{};
	virtual void draw( const ofMesh & vertexData, ofPolyRenderMode renderType, bool useColors, bool useTextures, bool useNormals ) const;
	virtual void draw( const of3dPrimitive& model, ofPolyRenderMode renderType ) const{};
	virtual void draw( const ofNode& model ) const{};
	virtual void draw( const ofImage & image, float x, float y, float z, float w, float h, float sx, float sy, float sw, float sh ) const{};
	virtual void draw( const ofFloatImage & image, float x, float y, float z, float w, float h, float sx, float sy, float sw, float sh ) const{};
	virtual void draw( const ofShortImage & image, float x, float y, float z, float w, float h, float sx, float sy, float sw, float sh ) const{};
	virtual void draw( const ofBaseVideoDraws & video, float x, float y, float w, float h ) const{};

	virtual void pushView(){};
	virtual void popView(){};

	virtual void viewport( ofRectangle viewport ){};
	virtual void viewport( float x = 0, float y = 0, float width = -1, float height = -1, bool vflip = true ){};

	virtual void setupScreenPerspective( float width = -1, float height = -1, float fov = 60, float nearDist = 0, float farDist = 0 ){};
	virtual void setupScreenOrtho( float width = -1, float height = -1, float nearDist = -1, float farDist = 1 ){};
	virtual void setOrientation( ofOrientation orientation, bool vFlip ){};

	virtual ofRectangle getCurrentViewport() const override;
	virtual ofRectangle getNativeViewport() const override;
	virtual int getViewportWidth() const override;
	virtual int getViewportHeight() const override;

	virtual bool isVFlipped() const override;
	virtual void setCoordHandedness( ofHandednessType handedness ){};
	virtual ofHandednessType getCoordHandedness() const override;

	virtual void pushMatrix() override;
	virtual void popMatrix() override;

	virtual ofMatrix4x4 getCurrentMatrix( ofMatrixMode matrixMode_ ) const override;
	virtual ofMatrix4x4 getCurrentOrientationMatrix() const override;

	virtual void translate( float x, float y, float z = 0 ){
		translate( { x,y,z } );
	};
	virtual void translate( const ofPoint & p );
	virtual void scale( float xAmnt, float yAmnt, float zAmnt = 1 ){};
	virtual void rotate( float degrees, float vecX, float vecY, float vecZ ){};
	virtual void rotateX( float degrees ){};
	virtual void rotateY( float degrees ){};
	virtual void rotateZ( float degrees ){};
	virtual void rotate( float degrees ){};

	virtual void matrixMode( ofMatrixMode mode ){};


	virtual void loadMatrix( const ofMatrix4x4 & m ){};
	virtual void loadMatrix( const float *m ){};
	virtual void loadIdentityMatrix( void ){};
	virtual void loadViewMatrix( const ofMatrix4x4 & m ){};
	virtual void multViewMatrix( const ofMatrix4x4 & m ){};
	virtual void multMatrix( const ofMatrix4x4 & m ){};
	virtual void multMatrix( const float *m ){};

	virtual ofMatrix4x4 getCurrentViewMatrix() const override;
	virtual ofMatrix4x4 getCurrentNormalMatrix() const override;

	virtual void bind( const ofCamera & camera, const ofRectangle & viewport );
	virtual void unbind( const ofCamera & camera );

	virtual void setupGraphicDefaults(){};
	virtual void setupScreen(){};

	virtual void setRectMode( ofRectMode mode ){};

	virtual ofRectMode getRectMode() override;

	virtual void setFillMode( ofFillFlag fill ){};

	virtual ofFillFlag getFillMode() override;

	virtual void setLineWidth( float lineWidth ){};
	virtual void setDepthTest( bool depthTest ){};

	virtual void setBlendMode( ofBlendMode blendMode ){};
	virtual void setLineSmoothing( bool smooth ){};

	virtual void setCircleResolution( int res ){};
	virtual void enableAntiAliasing(){};
	virtual void disableAntiAliasing(){};

	virtual void setColor( int r, int g, int b ){};
	virtual void setColor( int r, int g, int b, int a ){};
	virtual void setColor( const ofColor & color ){};
	virtual void setColor( const ofColor & color, int _a ){};
	virtual void setColor( int gray ){};
	virtual void setHexColor( int hexColor ){};

	virtual void setBitmapTextMode( ofDrawBitmapMode mode ){};

	virtual ofColor getBackgroundColor() override;
	virtual void setBackgroundColor( const ofColor & c ){};

	virtual void background( const ofColor & c ){};
	virtual void background( float brightness ){};
	virtual void background( int hexColor, float _a = 255.0f ){};
	virtual void background( int r, int g, int b, int a = 255 ){};
	virtual void setBackgroundAuto( bool bManual ){};

	virtual bool getBackgroundAuto() override;

	virtual void clear(){};
	virtual void clear( float r, float g, float b, float a = 0 ){};
	virtual void clear( float brightness, float a = 0 ){};
	virtual void clearAlpha(){};

	virtual void drawLine( float x1, float y1, float z1, float x2, float y2, float z2 ) const{};
	virtual void drawRectangle( float x, float y, float z, float w, float h ) const{};
	virtual void drawTriangle( float x1, float y1, float z1, float x2, float y2, float z2, float x3, float y3, float z3 ) const{};
	virtual void drawCircle( float x, float y, float z, float radius ) const{};
	virtual void drawEllipse( float x, float y, float z, float width, float height ) const{};
	virtual void drawString( string text, float x, float y, float z ) const{};
	virtual void drawString( const ofTrueTypeFont & font, string text, float x, float y ) const{};

	virtual ofPath & getPath() override;
	virtual ofStyle getStyle() const override;
	virtual void setStyle( const ofStyle & style ){};
	virtual void pushStyle(){};
	virtual void popStyle(){};

	virtual void setCurveResolution( int resolution ){};

	virtual void setPolyMode( ofPolyWindingMode mode ){};

	virtual const of3dGraphics & get3dGraphics() const override;
	virtual of3dGraphics & get3dGraphics() override;


private:

	std::shared_ptr<of::vk::Context> mContext;
	/*

	We want the instance to be shared across multiple renderers, i.e. across multiple windows.

	currently, we have one renderer per window

	the instance is shared across windows

	each window will have its own rendering environment

	they can share elements because they will end up on the same device,
	each window will have its own render queue.
	but renderers will have to be synchronised using semaphores if you want to share memory.

	multiwindow is something to implement at a later point.

	*/

	void createInstance();
	void destroyInstance();

	void  createDevice();
	void destroyDevice();

	void destroySurface();

	VkDebugReportCallbackEXT debugReportCallback = VK_NULL_HANDLE;
	VkDebugReportCallbackCreateInfoEXT debugCallbackCreateInfo = {};

	void setupDebugLayers();
	void createDebugLayers();
	void destroyDebugLayers();

	VkInstance       mInstance = VK_NULL_HANDLE;		   // context
	VkDevice         mDevice = VK_NULL_HANDLE;		   // virtual device


	VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;		   // actual GPU
	VkPhysicalDeviceProperties mPhysicalDeviceProperties = {};
	VkPhysicalDeviceMemoryProperties mPhysicalDeviceMemoryProperties;


	std::vector<const char*> mInstanceLayers;       // debug layer list for instance
	std::vector<const char*> mInstanceExtensions;   // debug layer list for device
	std::vector<const char*> mDeviceLayers;         // debug layer list for device
	std::vector<const char*> mDeviceExtensions;     // debug layer list for device

	std::vector<shared_ptr<of::vk::Shader>>	 mShaders;

	uint32_t         mVkGraphicsFamilyIndex = 0;

public:

	// return handle to renderer's vkDevice
	// TODO: error checking for when device has not been aqcuired yet.
	const VkDevice& getVkDevice() const{
		return mDevice;
	};

	VkCommandBuffer& getDrawCommandBuffer(){
		return mDrawCmdBuffer;
	};

	const VkPhysicalDeviceProperties& getVkPhysicalDeviceProperties() const {
		return mPhysicalDeviceProperties;
	}

	// get memory allocation info for best matching memory type that matches any of the type bits and flags
	bool  getMemoryAllocationInfo( const VkMemoryRequirements& memReqs, VkFlags memProps, VkMemoryAllocateInfo& memInfo ) const;


private:

	ofRectangle mViewport;

	// in the future, we will want users to create their own command pools etc. 
	// so we will want users to be able to access the current device.
	VkCommandPool            mCommandPool = nullptr;
	
	// Command buffer used for rendering
	VkCommandBuffer			mDrawCmdBuffer = nullptr;

	// command buffers used to present frame buffer to screen
	VkCommandBuffer          mPrePresentCommandBuffer     = VK_NULL_HANDLE;
	VkCommandBuffer          mPostPresentCommandBuffer    = VK_NULL_HANDLE;

	// command buffer used to prepare things during setup
	VkCommandBuffer mSetupCommandBuffer = VK_NULL_HANDLE;
	
	// find these implemented in VKPrepare.cpp
	void querySurfaceCapabilities();
	void createCommandPool();
	void createSetupCommandBuffer();
	void setupSwapChain();
	void createCommandBuffers();
	void setupDepthStencil();
	void setupRenderPass();
	
	void setupFrameBuffer();
	void flushSetupCommandBuffer();

	// creates synchronisation primitives 
	void createSemaphores();

	// load and compile shaders, then
	// derive descriptor set table from shaders
	void setupShaders();
	void setupDescriptorSets();
	void setupPipelines();
	void setupDescriptorPool();

	void beginDrawCommandBuffer();
	void endDrawCommandBuffer();

	void beginRenderPass();
	void endRenderPass();

	struct
	{
		std::vector<VkVertexInputBindingDescription> binding;
		std::vector<VkVertexInputAttributeDescription> attribute;
		VkPipelineVertexInputStateCreateInfo vi;
	} mVertexInfo;

	VkPipelineCache       mPipelineCache;


	// TODO: make this a dymanic sctucture.
	// this is only there to store pipelines once they have been set up.
	// A pipeline is very close to a material - it's worth exploring 
	// this a bit further.
	struct
	{
		VkPipeline solid; // solid triangle render pipeline
		VkPipeline wireframe; // wireframe render pipeline
	} mPipelines;
	
	// the pool where all descriptors will be allocated from
	VkDescriptorPool      mDescriptorPool;

	/*
	
	descriptor sets are owned by the renderer
	as these are used with multiple pipelines
	and bindings.

	they are initially derived from iterating over
	all shaders
	
	*/
	

	// a descriptor set is a sequence of descriptors,
	// laid out in a way specified by its descriptorSetLayout
	vector<VkDescriptorSet>  mDescriptorSets;

	// ideally, descriptorsets are kept within a map 
	// so that they can be referenced by their ubo name from the shader.
	// but it's important that these then match!

	// describes the layout for a descriptorset, i.e. the count and ordering
	// of descriptors within a set
	//
	// each descriptorset is derived from a layout
	// multiple descriptorsets may be derived from the same layout
	std::vector<std::shared_ptr<VkDescriptorSetLayout>> mDescriptorSetLayouts;

	std::vector<std::shared_ptr<VkPipelineLayout>> mPipelineLayouts;

	// our main (primary) gpu queue. all commandbuffers are submitted to this queue
	// as are present commands.
	VkQueue	mQueue = VK_NULL_HANDLE;

	// the actual window drawing surface to actually really show something on screen.
	// this is set externally using GLFW.
	VkSurfaceKHR mWindowSurface = VK_NULL_HANDLE;	

	VkSurfaceFormatKHR mWindowColorFormat = {};

	// Depth buffer format
	// Depth format is selected during Vulkan initialization, in createDevice()
	VkFormat mDepthFormat;

	// main renderpass 
	VkRenderPass mRenderPass;

	// Synchronization semaphores
	struct
	{
		VkSemaphore presentComplete = VK_NULL_HANDLE;
		VkSemaphore renderComplete  = VK_NULL_HANDLE;
	} mSemaphores;

	// our depth stencil: 
	// we only need one since there is only ever one frame in flight.
	struct
	{
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
	} mDepthStencil;

	// vulkan swapchain
	Swapchain mSwapchain;

	// frame buffers for each image in the swapchain
	std::vector<VkFramebuffer> mFrameBuffers;
	// Active frame buffer index
	uint32_t mCurrentFramebufferIndex = 0;

	uint32_t mWindowWidth = 0;
	uint32_t mWindowHeight = 0;


	VkClearColorValue mDefaultClearColor = { { 0.025f, 0.025f, 0.025f, 1.0f } };

	struct BufferObject
	{
		VkBuffer buf;
		VkDeviceMemory mem;
		size_t num_elements;
	};

	// buffer allocated on the fly for dynamic vertex data
	class TransientVertexBuffer : public BufferObject
	{
	public:
		static shared_ptr<BufferObject> create(  ofVkRenderer* renderer_, const vector<ofVec3f>& vec_);
	};

	// buffer allocated on the fly for dynamic index data
	struct TransientIndexBuffer	: public BufferObject
	{
		static shared_ptr<BufferObject>	create(  ofVkRenderer* renderer_, const vector<uint32_t>& vec_ );
	};

	std::list<shared_ptr<BufferObject>> mTransientBufferObjects;
	
public:

	const VkInstance& getInstance();
	const VkSurfaceKHR& getWindowSurface();

	friend class of::vk::Context;
};
