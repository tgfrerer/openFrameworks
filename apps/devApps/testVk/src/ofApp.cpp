#include "ofApp.h"
#include "vk/ofVkRenderer.h"
#include "vk/vkUtils.h"
#include "vk/vkTexture.h"

uint32_t display_mode = 3;
uint32_t numDisplayModes = 4;
bool     isFrameRateLocked = true;
uint32_t TARGET_FRAME_RATE = 90;

//--------------------------------------------------------------
void ofApp::setup(){
	ofSetFrameRate( TARGET_FRAME_RATE );
	mCam1.disableMouseInput();
	mCam1.setupPerspective( false, 60, 0.1, 5000 );
	mCam1.setGlobalPosition( 0, 0, mCam1.getImagePlaneDistance() );
	mCam1.lookAt( { 0,0,0 }, {0,1,0} );
	//mCam1.setDistance( 200 );
	mCam1.enableMouseInput();

	mFontMesh.load( "untitled.ply" );
	
	{	// Horizontally elongated "L___" shape

		vector<glm::vec3> vert {
			{0.f,0.f,0.f},
			{20.f,20.f,0.f},
			{0.f,100.f,0.f},
			{20.f,100.f,0.f},
			{200.f,0.f,0.f},
			{200.f,20.f,0.f}
		};

		vector<ofIndexType> idx {
			0, 1, 2,
			1, 3, 2,
			0, 4, 1,
			1, 4, 5,
		};

		vector<glm::vec3> norm( vert.size(), { 0, 0, 1.f } );

		vector<ofFloatColor> col( vert.size(), ofColor::white );

		mLMesh.addVertices( vert );
		mLMesh.addNormals( norm );
		mLMesh.addColors( col );
		mLMesh.addIndices( idx );

	};

	/*

	lets think for a bit about how we would want rendering to work in a vulkan idiomatic way.

	vulkan needs : 
	
	renderpass
		pipeline  
			vertex inputs
			descriptor sets
				uniform buffers
				sampled images
	
	Vertex inputs and descriptor inputs need to be immutable, as they are not immediately consumed,
	but will only be released for reuse once the frame has been rendered async.
	
	Also, most of your data is immutable. There needs to be a way to mark buffers as immutable.

	really, when you draw, you say: 
		here is some geometry, 
		here are the standard transformations (model-, view-, projection-matrices)
		here are additional transform parameters - 
		here is a material - now draw geometry with these transformations with this material.

	When you do skinning for example, is this part of the material? no, it's part of the transformations

	*/

	// WIP: texture loading & binding
	// 
	// ofPixels tmpImagePix;
	// ofLoadImage( tmpImagePix, "images/brighton.jpg" );
	// mVkTex.load( tmpImagePix );

	// use this to swap out the default context with a newly created one.
	if (false )
	{
		auto & renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

		of::vk::Context::Settings contextSettings;

		contextSettings.device             = renderer->getVkDevice();
		contextSettings.numSwapchainImages = renderer->getSwapChainSize();
		contextSettings.renderPass         = renderer->getDefaultRenderPass();
		contextSettings.framebuffers       = renderer->getDefaultFramebuffers();
		contextSettings.shaderManager      = renderer->getShaderManager();

		mExplicitContext = make_shared<of::vk::Context>( contextSettings );

		of::vk::Shader::Settings settings{
			renderer->getShaderManager(),
			{
				{ VK_SHADER_STAGE_VERTEX_BIT  , "triangle.vert" },
				{ VK_SHADER_STAGE_FRAGMENT_BIT, "triangle.frag" },
			}
		};

		// shader creation makes shader reflect. 
		mShader = std::make_shared<of::vk::Shader>( settings );
		mExplicitContext->addShader( mShader );

		// this will analyse our shaders and build descriptorset
		// layouts. it will also build pipelines.
		mExplicitContext->setup( renderer.get() );
		
		renderer->setDefaultContext( mExplicitContext );
	}

}

//--------------------------------------------------------------
void ofApp::update(){

	ofSetWindowTitle( ofToString( ofGetFrameRate() ));
}

//--------------------------------------------------------------

void ofApp::draw(){
	switch (display_mode) {
	case 0:
		drawModeMeshes();
	break;
	case 1:
		drawModeLines();
	break;
	case 2:
		drawModeSpinning();
	break;
	case 3:
		drawModeExplicit();
		break;
	default:
	break;
	}

}

//--------------------------------------------------------------

void ofApp::drawModeExplicit(){
	
	auto & renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );
	auto & context = *renderer->getDefaultContext();

	static ofMesh ico = ofMesh::icosphere( 50, 3 );
	
	mCam1.begin();
	
	//context.bind( mCam1 );
	//context.bind(shader1);
	context
		.setUniform( "globalColor", ofFloatColor::lightBlue )
		.pushMatrix()
		.translate( { -200, +200, 100 } )
		.draw(ico)
		.popMatrix();
	//context.unbind( shader1 );
	//context.unbind( mCam1 );

	context
		.setPolyMode( VK_POLYGON_MODE_LINE )
		.pushMatrix()
		.setUniform( "globalColor", ofFloatColor::white )
		.translate( { -200, -200, -200 } )
		.draw(ico)
		.popMatrix();

	context
		.pushMatrix()
		.translate( { 200, +200, -200 } )
		.draw( ico )
		.popMatrix();

	context
		.pushMatrix()
		.setPolyMode( VK_POLYGON_MODE_POINT )
		.translate( { 200, -200, 200 } )
		.draw( ico )
		.popMatrix();

	context
		.setUniform( "globalColor", ofFloatColor::red )
		.setPolyMode( VK_POLYGON_MODE_FILL )
		.draw( mFontMesh );
	
	context
		.pushMatrix()
		.rotateRad( ( ofGetFrameNum() % 360 )*DEG_TO_RAD, { 0.f,0.f,1.f } )
		.draw( mLMesh )
		.popMatrix();

	context
		.pushMatrix()
		.setUniform( "globalColor", ofFloatColor::teal )
		.translate( { 200.f,0.f,0.f } )
		.rotateRad( 360.f * ( ( ofGetElapsedTimeMillis() % 6000 ) / 6000.f ) * DEG_TO_RAD, { 0.f,0.f,1.f } )
		.draw( mLMesh )
		.popMatrix();

	mCam1.end();
}

//--------------------------------------------------------------

void ofApp::drawModeMeshes(){
	static ofMesh ico = ofMesh::icosphere( 50, 3 );
	mCam1.begin();

	ofSetColor( ofColor::white );
	ofPushMatrix();
	ofTranslate( -200, +200, 100 );
	ico.draw();
	ofPopMatrix();

	ofPushMatrix();
	ofTranslate( -200, -200, -200 );
	ico.draw();
	ofPopMatrix();

	ofPushMatrix();
	ofTranslate( 200, +200, -200 );
	ico.draw();
	ofPopMatrix();

	ofPushMatrix();
	ofTranslate( 200, -200, 200 );
	ico.draw();
	ofPopMatrix();

	ofSetColor( ofColor::red );
	mFontMesh.draw();

	ofPushMatrix();
	ofRotate( ofGetFrameNum() % 360 ); // this should rotate at a speed of one revolution every 6 seconds if frame rate is locked to vsync.
	mLMesh.draw();
	ofPopMatrix();

	ofSetColor( ofColor::teal );
	ofPushMatrix();
	ofTranslate( 200, 0 );
	ofRotate( 360.f * ( ( ofGetElapsedTimeMillis() % 6000 ) / 6000.f ) ); // this should rotate at a speed of one revolution every 6 seconds.
	mLMesh.draw();
	ofPopMatrix();

	mCam1.end();
}
//--------------------------------------------------------------

void ofApp::drawModeLines(){
	mCam1.begin();

	ofSetColor( ofColor::white );
	int w = 1024;
	int h = 768;

	ofPushMatrix();
	int xOffset = ofGetFrameNum() % w;
	ofTranslate( xOffset - w * 1.5, -h / 2 );
	for ( int i = 0; i * 100 < w * 2; ++i ){
		ofTranslate( 100, 0 );
		ofDrawRectangle( { -5,0,5,float( h ) } );
	}

	ofPopMatrix();
	mCam1.end();
}

//--------------------------------------------------------------

void ofApp::drawModeSpinning(){
	mCam1.begin();

	ofSetColor( ofColor::white );

	ofPushMatrix();
	ofTranslate( 0, 0);
	ofRotate( ( ofGetFrameNum() % 120 ) * ( 360 / 120.f ) );
	ofDrawRectangle( { -1200,-50,2400,100 } );
	ofPopMatrix();
	mCam1.end();
}

//--------------------------------------------------------------

void ofApp::keyPressed(int key){
	if (key=='m'){
		display_mode = ( ++display_mode ) % numDisplayModes;
	}
	if ( key == 'l' ){
		isFrameRateLocked ^= true;
		if ( isFrameRateLocked ){
			ofSetFrameRate( TARGET_FRAME_RATE );
			ofLog() << "Frame production rate locked at "<< TARGET_FRAME_RATE << " fps";
		}
		else{
			ofSetFrameRate( 0 );
			ofLog() << "Frame rate unlocked.";
		}
	}

}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){

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

void ofApp::exit(){
	mCam1.disableMouseInput();
}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}
