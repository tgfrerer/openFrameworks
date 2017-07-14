#include "ofApp.h"
#include "ofVkRenderer.h"

// We keep a pointer to the renderer so we don't have to 
// fetch it anew every time we need it.
ofVkRenderer* renderer;

//--------------------------------------------------------------
void ofApp::setup(){
	
	ofDisableSetupScreen();
	
	ofSetFrameRate( 0 );

	renderer = dynamic_cast<ofVkRenderer*>( ofGetCurrentRenderer().get() );

	{
		of::vk::Shader::Settings shaderSettings;
		shaderSettings.device = renderer->getVkDevice();
		// Enable printing of verbose debug information at shader compilation
		shaderSettings
			.setPrintDebugInfo(true)
			.setSource(::vk::ShaderStageFlagBits::eVertex,   "shaders/default.vert")
			.setSource(::vk::ShaderStageFlagBits::eFragment, "shaders/default.frag")
			;

		// Initialise default shader with settings above
		defaultShader = std::make_shared<of::vk::Shader>( shaderSettings );

		// Define pipeline state to use with draw command
		of::vk::GraphicsPipelineState pipeline;

		pipeline.setShader( defaultShader );

		pipeline.rasterizationState
			.setPolygonMode( ::vk::PolygonMode::eLine )
			.setCullMode( ::vk::CullModeFlagBits::eBack )
			.setFrontFace( ::vk::FrontFace::eCounterClockwise )
			;

		// Setup draw command using pipeline state above
		defaultDraw.setup( pipeline );
	}

	mMesh = std::make_shared<ofMesh>(ofIcoSpherePrimitive(100, 1).getMesh());
	
	mCam.setupPerspective( false, 60, 0.f, 5000 );
	mCam.setPosition( { 0, 0, mCam.getImagePlaneDistance() } );
	mCam.lookAt( { 0, 0, 0 } );
	mCam.setEvents( ofEvents() );

}

//--------------------------------------------------------------
void ofApp::update(){
	
}

//--------------------------------------------------------------
void ofApp::draw(){

	// Vulkan uses a slightly different clip space than OpenGL - 
	// In Vulkan, z goes from 0..1, instead of OpenGL's -1..1
	// and y is flipped. 
	// We apply the clip matrix to the projectionMatrix to transform
	// from openFrameworks (GL-style) to Vulkan (Vulkan-style) clip space.
	static const glm::mat4x4 clip (
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.5f, 0.0f,
		0.0f, 0.0f, 0.5f, 1.0f
	);

	auto viewMatrix = mCam.getModelViewMatrix();
	auto projectionMatrix = clip * mCam.getProjectionMatrix( ofGetCurrentViewport() );
	auto modelMatrix = glm::rotate( float( TWO_PI * (fmodf( ofGetElapsedTimef(), 8.f) * 0.125) ), glm::normalize(glm::vec3( { 0.f, 1.f, 1.f } )) );

	auto & context = renderer->getDefaultContext();
	
	defaultDraw
		.setUniform( "projectionMatrix", projectionMatrix )
		.setUniform( "viewMatrix", viewMatrix )
		.setUniform( "modelMatrix", modelMatrix )
		.setUniform( "globalColor", ofFloatColor::black)
		.setDrawMethod( of::vk::DrawCommand::DrawMethod::eIndexed )
		.setMesh( mMesh )
		;

	modelMatrix = glm::scale( modelMatrix, { 0.1,0.1,0.1 } );

	// Setup the main pass RenderBatch
	// RenderBatch is a light-weight helper object which encapsulates
	// a Vulkan Command Buffer with a Vulkan RenderPass. 
	//
	of::vk::RenderBatch::Settings settings;
	settings
		.setContext(context.get())
		.setFramebufferAttachmentsExtent(renderer->getSwapchain()->getWidth(), renderer->getSwapchain()->getHeight())
		.setRenderArea(::vk::Rect2D({}, { uint32_t(renderer->getViewportWidth()), uint32_t(renderer->getViewportHeight()) }))
		.setRenderPass(*renderer->getDefaultRenderpass())
		.addFramebufferAttachment(context->getSwapchainImageView())
		.addClearColorValue(ofFloatColor::white)
		.addFramebufferAttachment(renderer->getDepthStencilImageView())
		.addClearDepthStencilValue({ 1.f,0 })
		;

	of::vk::RenderBatch batch{ settings };
	{
		// Beginning a batch allocates a new command buffer in the context
		// and begins a RenderPass
		batch.begin();
		batch.draw( defaultDraw );
		// Ending a batch accumulates all draw commands into a command buffer
		// and finalizes the command buffer.
		batch.end();
	}

}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){

}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){
	if ( key == ' ' ){
		if ( defaultShader ){
			defaultShader->compile();
		}
	}
}

//--------------------------------------------------------------
void ofApp::mouseMoved(int x, int y ){

}

//--------------------------------------------------------------
void ofApp::mouseDragged(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mousePressed(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseReleased(int x, int y, int button){

}

//--------------------------------------------------------------
void ofApp::mouseEntered(int x, int y){

}

//--------------------------------------------------------------
void ofApp::mouseExited(int x, int y){

}

//--------------------------------------------------------------
void ofApp::windowResized(int w, int h){
	mCam.setControlArea( { 0,0,float( w ),float( h ) } );
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}
