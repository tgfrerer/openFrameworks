#include "ofApp.h"
#include "vk/ofVkRenderer.h"
#include "vk/DrawCommand.h"
#include "vk/RenderBatch.h"

#define EXAMPLE_TARGET_FRAME_RATE 90
bool isFrameLocked = true;

void Teapot::setup(){
	auto & renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

	::vk::RenderPass & renderPass = *renderer->getDefaultRenderPass();  // needs to be created upfront

	// shader creation makes shader reflect. 
	auto mShaderDefault = std::shared_ptr<of::vk::Shader>(new of::vk::Shader( renderer->getVkDevice(),
	{
		{ ::vk::ShaderStageFlagBits::eVertex  , "default.vert" },
		{ ::vk::ShaderStageFlagBits::eFragment, "default.frag" },
	}));

	of::vk::DrawCommandInfo dcs;

	dcs.modifyPipeline().depthStencilState
		.setDepthTestEnable( VK_TRUE )
		.setDepthWriteEnable( VK_TRUE )
		;
	dcs.modifyPipeline().inputAssemblyState.setTopology( ::vk::PrimitiveTopology::eTriangleList );
	dcs.modifyPipeline().setShader( mShaderDefault );
	dcs.modifyPipeline().setRenderPass( renderPass );

	dc = std::move(std::make_unique<of::vk::DrawCommand>( dcs ));
	mLMesh = make_shared<ofMesh>();
	{	// Horizontally elongated "L___" shape

		vector<glm::vec3> vert{
			{ 0.f,0.f,0.f },
			{ 20.f,20.f,0.f },
			{ 0.f,100.f,0.f },
			{ 20.f,100.f,0.f },
			{ 200.f,0.f,0.f },
			{ 200.f,20.f,0.f }
		};

		vector<ofIndexType> idx{
			0, 1, 2,
			1, 3, 2,
			0, 4, 1,
			1, 4, 5,
		};

		vector<glm::vec3> norm( vert.size(), { 0, 0, 1.f } );

		vector<ofFloatColor> col( vert.size(), ofColor::white );

		mLMesh->addVertices( vert );
		mLMesh->addNormals( norm );
		mLMesh->addColors( col );
		mLMesh->addIndices( idx );
	};
}
//--------------------------------------------------------------

void Teapot::recompile(){
	const_cast<of::vk::DrawCommandInfo&>(dc->getInfo()).modifyPipeline().getShader()->compile();
}

//--------------------------------------------------------------

void Teapot::draw( of::vk::RenderBatch& rb ){

	// update uniforms inside the draw command 

	auto projectionMatrix = glm::mat4x4();
	static ofCamera mCam;

	mCam.setPosition( { 0,0, mCam.getImagePlaneDistance() } );
	mCam.lookAt( { 0,0,0 } );

	static const glm::mat4x4 clip( 1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.5f, 0.0f,
		0.0f, 0.0f, 0.5f, 1.0f );

	projectionMatrix = clip * mCam.getProjectionMatrix( ofGetCurrentViewport() );

	glm::mat4x4 modelMatrix;
	modelMatrix = glm::rotate( float(TWO_PI * ( ( ofGetFrameNum() % 360 ) / 360.f )), glm::vec3({ 0.f, 0.f, 1.f}) ) * modelMatrix;

	dc->setUniform( "modelMatrix", modelMatrix );

	dc->setUniform( "projectionMatrix", projectionMatrix );
	dc->setUniform( "viewMatrix", mCam.getModelViewMatrix() );
	dc->setUniform( "globalColor", ofFloatColor::magenta );

	dc->setMesh( mLMesh );

	// update attribute buffer bindings
	rb.draw( *dc );

}



//--------------------------------------------------------------
void ofApp::setup(){
	ofDisableSetupScreen();
	const auto & renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

	//of::RenderContext::Settings renderContextSettings;
	//
	// renderContextSettings.transientMemoryAllocatorSettings
	//	.setPhysicalDeviceMemoryProperties ( renderer->getVkPhysicalDeviceMemoryProperties() )
	//	.setPhysicalDeviceProperties       ( renderer->getVkPhysicalDeviceProperties() )
	//	.setFrameCount                     ( 2 )
	//	.setDevice                         ( renderer->getVkDevice() )
	//	.setSize                           ( ( 1UL << 24 ) * 2)  // (16 MB * number of frames))
	//	;

	//mRenderContext = std::make_unique<of::RenderContext>(renderContextSettings);

	//mRenderContext = renderer->getDefaultContext();

	mTeapot.setup();
	ofSetFrameRate( EXAMPLE_TARGET_FRAME_RATE );
}

//--------------------------------------------------------------

void ofApp::update(){
	


}

//--------------------------------------------------------------
void ofApp::draw(){
	const auto & renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );
	
	of::vk::RenderBatch batch( *renderer->getDefaultContext() /*, reorder = true*/ );
	
	// a renderbatch 
	
	// we can't specify the framebuffer upfront, as the framebuffer is 
	// created at frame start - based on what?
	//
	// the framebuffer is created to link the current renderpass with 
	// images so that the output can be stored somewhere.
	//
	// the framebuffer is created inside the renderer - and it is the renderer which
	// will connect the default rendercontext/framebuffer to the outputs of the swapchain.
	//


	mTeapot.draw( batch );
	mTeapot.draw( batch );
	mTeapot.draw( batch );

	batch.submit();	// this will build, but not yet submit Vk Commandbuffer

	ofSetWindowTitle( ofToString( ofGetFrameRate(), 2, ' ' ) );
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){

}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){
	if ( key == ' ' ){
		mTeapot.recompile();
	} else if ( key == 'l' ){
		isFrameLocked ^= true;
		ofSetFrameRate( isFrameLocked ? EXAMPLE_TARGET_FRAME_RATE : 0);
		ofLog() << "Framerate " << ( isFrameLocked ? "" : "un" ) << "locked.";
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

}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}

