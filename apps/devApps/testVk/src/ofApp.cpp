#include "ofApp.h"
#include "vk/ofVkRenderer.h"

uint32_t display_mode = 0;
uint32_t max_display_mode = 3;
bool     isFrameRateLocked = true;

//--------------------------------------------------------------
void ofApp::setup(){
	ofSetFrameRate( 120 );
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

}

//--------------------------------------------------------------
void ofApp::update(){

	ofSetWindowTitle( ofToString( ofGetFrameRate() ));
}

//--------------------------------------------------------------
void ofApp::draw(){
	static ofMesh ico = ofMesh::icosphere(50, 3);
	static ofMesh box = ofMesh::box( 50, 50,50 );


	switch (display_mode) {
	case 0:{
		mCam1.begin();

		ofSetColor( ofColor::white );
		{
			//auto scope = context.shaders["shader1"].getScoped(); // scoped shader unbinds when out of scope

			ofPushMatrix();
			ofTranslate( -200, +200, 100 );
			ico.draw();
			ofPopMatrix();

		} // end shader scope

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
		ofRotate( ofGetFrameNum() % 360 ); // this should rotate at a speed of one revolution every 6 seconds.
		mLMesh.draw();
		ofPopMatrix();

		ofSetColor( ofColor::teal );
		ofPushMatrix();
		ofTranslate( 200, 0 );
		ofRotate( 360.f * ((ofGetElapsedTimeMillis() % 6000) / 6000.f) ); // this should rotate at a speed of one revolution every 6 seconds.
		mLMesh.draw();
		ofPopMatrix();

		mCam1.end();
	}
	break;
	case 1:
	{
		mCam1.begin();

		ofSetColor( ofColor::white );
		int w = 1024;
		int h = 768;

		ofPushMatrix();
		int xOffset = ofGetFrameNum() % w;
		ofTranslate(xOffset - w * 1.5, -h/2 );
		for (int i =0; i*100 < w * 2; ++i){
			ofTranslate(100 , 0);
			ofDrawRectangle({-5,0,5,float(h)});
		}

		ofPopMatrix();
		mCam1.end();
	}
	break;
	case 2:
	{
		mCam1.begin();

		
		ofSetColor(ofColor::white);

		ofPushMatrix();
		ofTranslate(ofGetWidth()*0.5f, ofGetHeight()*0.5f);
		ofRotate((ofGetFrameNum() % 120) * (360/120.f));
		ofDrawRectangle({-1200,-50,2400,100});
		ofPopMatrix();
		mCam1.end();

	}
	break;
	default:
	break;
	}

}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){
	if (key=='m'){
		display_mode = ( ++display_mode ) % max_display_mode;
	}
	if ( key == 'l' ){
		isFrameRateLocked ^= true;
		if ( isFrameRateLocked ){
			ofSetFrameRate( 120 );
			ofLog() << "Frame production rate locked at 120 fps";
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
