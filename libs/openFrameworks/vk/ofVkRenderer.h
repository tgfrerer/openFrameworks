#pragma once
#include <vulkan/vulkan.h>
#include "ofBaseTypes.h"
#include "ofPolyline.h"
#include "ofMatrix4x4.h"
#include "ofMatrixStack.h"
#include "of3dGraphics.h"
#include "ofBitmapFont.h"
#include "ofPath.h"
#include "ofMaterial.h"
#include "vk/Swapchain.h"
#include "vk/Context.h"
#include "ofMesh.h"

class ofShapeTessellation;
//class ofMesh;

namespace of{
namespace vk{
	class Shader;
	class ShaderManager;
}
};


class ofVkRenderer : public ofBaseRenderer
{

	bool bBackgroundAuto;
	bool wrongUseLoggedOnce;

	const ofBaseMaterial * currentMaterial;

	ofStyle currentStyle;
	deque <ofStyle> styleHistory;
	of3dGraphics m3dGraphics;
	ofBitmapFont bitmapFont;
	ofPath mPath;
	const ofAppBaseWindow * window;

	mutable ofMesh mRectMesh;

public:
	static const string TYPE;

	const struct Settings
	{
		uint32_t vkVersion = 1 << 22;                                  // target version
		uint32_t numVirtualFrames = 0;                                 // number of virtual frames to allocate and to produce - set this through vkWindowSettings
		uint32_t numSwapchainImages = 0;                               // number of swapchain images to aim for (api gives no guarantee for this.)
		VkPresentModeKHR swapchainType = VK_PRESENT_MODE_FIFO_KHR;	   // selected swapchain type (api only guarantees FIFO)
		bool useDebugLayers = false;                                    // whether to use vulkan debug layers
	} mSettings;

	ofVkRenderer( const ofAppBaseWindow * window, Settings settings ); 
	
	void setup();
	void setupDefaultContext();
	virtual ~ofVkRenderer() override;

	virtual const string & getType() override{
		return TYPE;
	};

	virtual void startRender() override;
	virtual void finishRender() override;

	const uint32_t getSwapChainSize();

	const VkRenderPass& getDefaultRenderPass();

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

	virtual glm::mat4x4 getCurrentMatrix( ofMatrixMode matrixMode_ ) const override;
	virtual glm::mat4x4 getCurrentOrientationMatrix() const override;

	virtual void translate( float x, float y, float z = 0 ){
		translate( { x,y,z } );
	};
	virtual void translate( const glm::vec3& p );
	virtual void scale( float xAmnt, float yAmnt, float zAmnt = 1 ){};
	virtual void rotateRad( float degrees, float axisX, float axisY, float axisZ ) override;
	virtual void rotateXRad( float degrees ) override;
	virtual void rotateYRad( float degrees ) override;
	virtual void rotateZRad( float degrees ) override;
	virtual void rotateRad( float degrees ) override;

	virtual void matrixMode( ofMatrixMode mode ){};


	virtual void loadMatrix( const glm::mat4x4 & m ){};
	virtual void loadMatrix( const float *m ){};
	virtual void loadIdentityMatrix( void ){};
	virtual void loadViewMatrix( const glm::mat4x4 & m ){};
	virtual void multViewMatrix( const glm::mat4x4 & m ){};
	virtual void multMatrix( const glm::mat4x4 & m ){};
	virtual void multMatrix( const float *m ){};

	virtual glm::mat4x4 getCurrentViewMatrix() const override;
	virtual glm::mat4x4 getCurrentNormalMatrix() const override;

	virtual void bind( const ofCamera & camera, const ofRectangle & viewport );
	virtual void unbind( const ofCamera & camera );

	virtual void setupGraphicDefaults(){};
	virtual void setupScreen(){};
	void resizeScreen( int w, int h );

	virtual void setRectMode( ofRectMode mode ){};

	virtual ofRectMode getRectMode() override;

	virtual void setFillMode( ofFillFlag fill );

	virtual ofFillFlag getFillMode() override;

	virtual void setLineWidth( float lineWidth ){};
	virtual void setDepthTest( bool depthTest ){};

	virtual void setBlendMode( ofBlendMode blendMode ){};
	virtual void setLineSmoothing( bool smooth ){};

	virtual void setCircleResolution( int res ){};
	virtual void enableAntiAliasing(){};
	virtual void disableAntiAliasing(){};

	virtual void setColor( int r, int g, int b ){};
	virtual void setColor( int r, int g, int b, int a ){
		setColor( ofColor( r, g, b, a ));
	};
	virtual void setColor( const ofColor & color );
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
	virtual void drawRectangle( float x, float y, float z, float w, float h ) const;
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


	void createInstance();
	void destroyInstance();

	void  createDevice();
	void destroyDevice();

	void destroySurface();

	VkDebugReportCallbackEXT           mDebugReportCallback = nullptr;
	VkDebugReportCallbackCreateInfoEXT mDebugCallbackCreateInfo = {};

	void requestDebugLayers();
	void createDebugLayers();
	void destroyDebugLayers();

	VkInstance                         mInstance                           = nullptr;  // vulkan loader instance
	VkDevice                           mDevice                             = nullptr;  // virtual device
	VkPhysicalDevice                   mPhysicalDevice                     = nullptr;  // actual GPU
	VkPhysicalDeviceProperties         mPhysicalDeviceProperties = {};
	VkPhysicalDeviceMemoryProperties   mPhysicalDeviceMemoryProperties;

	std::vector<const char*>           mInstanceLayers;                                // debug layer list for instance
	std::vector<const char*>           mInstanceExtensions;                            // debug layer list for device
	std::vector<const char*>           mDeviceLayers;                                  // debug layer list for device
	std::vector<const char*>           mDeviceExtensions;                              // debug layer list for device

	uint32_t                           mVkGraphicsFamilyIndex = 0;


public:

	// return handle to renderer's vkDevice
	// CONSIDER: error checking for when device has not been aqcuired yet.
	const VkDevice& getVkDevice() const{
		return mDevice;
	};

	const VkPhysicalDeviceProperties& getVkPhysicalDeviceProperties() const {
		return mPhysicalDeviceProperties;
	}

	// get memory allocation info for best matching memory type that matches any of the type bits and flags
	bool  getMemoryAllocationInfo( const VkMemoryRequirements& memReqs, VkFlags memProps, VkMemoryAllocateInfo& memInfo ) const;

	// testing

	const VkCommandPool& getCommandPool() const {
		return mDrawCommandPool;
	}

	const VkCommandBuffer& getCurrentDrawCommandBuffer() const {
		if ( mFrameIndex < mFrameResources.size() ){
			return mFrameResources[mFrameIndex].cmd;
		} else{
			static VkCommandBuffer errorCmdBuffer = nullptr;
			ofLogError() << "No current draw command buffer";
			return errorCmdBuffer;
		}
	}

	const VkQueue& getQueue() const {
		return mQueue;
	}

	const shared_ptr<of::vk::Context>& getDefaultContext(){
		return mDefaultContext;
	}

	void setDefaultContext( std::shared_ptr<of::vk::Context>& defaultContext){
		mDefaultContext = defaultContext;
	}

	shared_ptr<of::vk::ShaderManager>& getShaderManager(){
		return mShaderManager;
	}

	const size_t getVirtualFramesCount(){
		return mFrameResources.size();
	}

private:

	ofRectangle mViewport;

	VkCommandPool            mDrawCommandPool = nullptr;
	
	struct FrameResources
	{
		VkCommandBuffer                       cmd                     = nullptr;
		VkSemaphore                           semaphoreImageAcquired  = nullptr;
		VkSemaphore                           semaphoreRenderComplete = nullptr;
		VkFence                               fence                   = nullptr;
		VkFramebuffer                         framebuffer             = nullptr;
	};

	std::vector<FrameResources> mFrameResources; // one frame resource per virtual frame
	uint32_t                    mFrameIndex = 0; // index of frame currently in production

	VkRenderPass                mRenderPass = nullptr; /*main renderpass*/ 


	void                     setupFrameResources();

	void                     querySurfaceCapabilities();
	void                     createCommandPool();

	void                     setupSwapChain();
	void                     setupDepthStencil();
	void                     setupRenderPass();
	
	void                     setupFrameBuffer(uint32_t swapchainImageIndex);
	void                     flushSetupCommandBuffer(VkCommandBuffer cmd);


	// our main (primary) gpu queue. all commandbuffers are submitted to this queue
	// as are present commands.
	VkQueue	mQueue = nullptr;

	// the actual window drawing surface to actually really show something on screen.
	// this is set externally using GLFW.
	VkSurfaceKHR mWindowSurface = nullptr;	

	VkSurfaceFormatKHR mWindowColorFormat = {};

	// Depth buffer format
	// Depth format is selected during Vulkan initialization, in createDevice()
	VkFormat mDepthFormat;

	// our depth stencil: 
	// we only need one since there is only ever one frame in flight.
	// !TODO: maybe move this into swapchain
	struct DepthStencilResource
	{
		VkImage image      = nullptr;
		VkDeviceMemory mem = nullptr;
		VkImageView view   = nullptr;
	};

	// one depth stencil image per swapchain frame
	std::vector<DepthStencilResource> mDepthStencil;

	// vulkan swapchain
	Swapchain mSwapchain;

	// context used for implicit rendering
	// reset this context if you don't want explicit rendering
	// but want to use your own.
	std::shared_ptr<of::vk::Context> mDefaultContext;

	// shader manager - this should be a unique object.
	std::shared_ptr<of::vk::ShaderManager> mShaderManager;

	uint32_t mWindowWidth = 0;
	uint32_t mWindowHeight = 0;

public:

	const VkInstance& getInstance();
	const VkSurfaceKHR& getWindowSurface();

	friend class of::vk::Context;
};
