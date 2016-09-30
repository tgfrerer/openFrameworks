#include "ofApp.h"
#include "vk/ofVkRenderer.h"
#include "vk/DrawCommand.h"
#include "vk/RenderBatch.h"

#define EXAMPLE_TARGET_FRAME_RATE 90
bool isFrameLocked = true;

std::shared_ptr<ofVkRenderer> renderer = nullptr;


void Teapot::setup(){

	// shader creation makes shader reflect. 
	auto mShaderDefault = std::shared_ptr<of::vk::Shader>(new of::vk::Shader( renderer->getVkDevice(),
	{
		{ ::vk::ShaderStageFlagBits::eVertex  , "default.vert" },
		{ ::vk::ShaderStageFlagBits::eFragment, "default.frag" },
	}));

	of::vk::GraphicsPipelineState pipeline;

	pipeline.depthStencilState
		.setDepthTestEnable( VK_TRUE )
		.setDepthWriteEnable( VK_TRUE )
		;
	pipeline.inputAssemblyState.setTopology( ::vk::PrimitiveTopology::eTriangleList );
	// pipeline.setPolyMode( ::vk::PolygonMode::eLine );
	pipeline.setShader( mShaderDefault );

	dc.setup( pipeline );

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
	dc.getPipelineState().touchShader();
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
	ofMatrix4x4 modelMatrix = glm::rotate( float(TWO_PI * ( ( ofGetFrameNum() % 360 ) / 360.f )), glm::vec3({ 0.f, 0.f, 1.f}) );
	

	of::vk::DrawCommand ndc = dc;

	ndc.setUniform( "projectionMatrix", projectionMatrix );     // | 
	ndc.setUniform( "viewMatrix", mCam.getModelViewMatrix() );  // |> set camera matrices
	ndc.setUniform( "modelMatrix", modelMatrix );
	ndc.setUniform( "globalColor", ofFloatColor::magenta );
	ndc.setMesh( mLMesh );

	// update attribute buffer bindings
	rb.draw( ndc );

	of::vk::DrawCommand newDraw = ndc;

	modelMatrix.translate( { 100,0,0 } );
	newDraw.setUniform( "modelMatrix", modelMatrix );

	rb.draw( newDraw );

}

//--------------------------------------------------------------

void ofApp::setup(){
	ofDisableSetupScreen();
	renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

	mTeapot.setup();
	ofSetFrameRate( EXAMPLE_TARGET_FRAME_RATE );
}

//--------------------------------------------------------------

void ofApp::update(){

}

//--------------------------------------------------------------
void ofApp::draw(){
	
	of::vk::RenderBatch batch{ *renderer->getDefaultContext() };

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

