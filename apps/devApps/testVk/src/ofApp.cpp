#include "ofApp.h"
#include "vk/ofVkRenderer.h"

//--------------------------------------------------------------
void ofApp::setup(){
	mCam1.disableMouseInput();
	mCam1.setupPerspective( false, 60, 0.1, 5000 );
	mCam1.setGlobalPosition( 0, 0, mCam1.getImagePlaneDistance() );
	mCam1.lookAt( { 0,0,0 }, {0,1,0} );
	//mCam1.setDistance( 200 );
	mCam1.enableMouseInput();

	mFontMesh.load( "untitled.ply" );
	
	{	// Horizontally elongated "L___" shape

		vector<ofVec3f> vert {
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

		vector<ofVec3f> norm( vert.size(), { 0, 0, 1.f } );

		vector<ofFloatColor> col( vert.size(), ofColor::white );

		mLMesh.addVertices( vert );
		mLMesh.addNormals( norm );
		mLMesh.addColors( col );
		mLMesh.addIndices( idx );

	};

	// 0. define swapchain state

	/*
	
		+ behaviour : fifo, mailbox, immediate
		+ number of swapchain images (size of swapchain maps to size of uniform buffers --> double buffering means uniform buffers are double-buffered as well )
		
	*/

	// 1. define render passes and framebuffers

	/*
		+ color attachments
		+ depth buffers?
		+ which attachment is mapped to swapchain image? (this one needs to be double-buffered)
		+ multisampling?
		+ clear color
		
		+ subpasses 
			+ relationship (dependency graph) between subpasses
	
	*/

	// 3. define global uniform state (lights, matrices) ==> scene

	// 2. define pipelines and specify dynamic pipeline state, and possible pipeline permutations --> materials

	// 4. define per-object uniform state (this is based on materials)

}

//--------------------------------------------------------------
void ofApp::update(){

	ofSetWindowTitle( ofToString( ofGetFrameRate() ));
}

//--------------------------------------------------------------
void ofApp::draw(){
	mCam1.begin();

	// now that the buffer have been submitted eagerly, 
	// we need to have a memory barrier here to make sure that that
	// buffer has finished transfering to GPU before the draw happens.

	// -----
	// draw command issued here:

	// some engines group mesh(es) + shader together at this point
	// so that the draw happens in a "batch" or "list" 
	// 
	// this batch then gets re-ordered before submission to minimise 
	// state changes when drawing.
	// 
	// it might also be possible to use a hashmap conatiner with a 
	// custom key generator which automatically places a new draw 
	// call in the correct order
	//
	// all this would mean deferring the construction of the command
	// buffer, though. 
	//
	// we could group *materials* and geometry together to create a batch
	// the batch draw command queries current render state from context 
	// and submits this way.
	
	// look at nvidia vulkan demo and at how they structure rendering.

	static ofMesh ico = ofMesh::icosphere(50, 3);
	static ofMesh box = ofMesh::box( 50, 50,50 );
	
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

	mFontMesh.draw();

	ofPushMatrix();
	ofRotate( ofGetFrameNum() % 360 ); // this should rotate at a speed of one revolution every 6 seconds. 
	mLMesh.draw();
	ofPopMatrix();

	ofPushMatrix();
	ofTranslate( 200, 0 );
	ofRotate( 360.f * ((ofGetElapsedTimeMillis() % 6000) / 6000.f) ); // this should rotate at a speed of one revolution every 6 seconds. 
	mLMesh.draw();
	ofPopMatrix();

	/*ofTranslate( 100, -100, 50 );
	
	ofTranslate( 100, -100, 50 );
	m.draw();*/
	mCam1.end();
}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){

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
