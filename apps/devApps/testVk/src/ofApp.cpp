#include "ofApp.h"
#include "ofVkRenderer.h"

//--------------------------------------------------------------
void ofApp::setup(){

	mCam1.setupPerspective( true, 60, 0.1, 500 );
	mCam1.setGlobalPosition( 0, 0, 200 );
	mCam1.setDistance( 200 );
	
	mFontMesh.load( "untitled.ply" );
	
}

//--------------------------------------------------------------
void ofApp::update(){


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

	ofMesh m = ofMesh::icosphere(30,3);
	
	ofTranslate( -100, +100, -50 );
	m.draw();
	ofTranslate( 100, -100, 50 );
	mFontMesh.draw();
	ofTranslate( 100, -100, 50 );
	m.draw();
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
