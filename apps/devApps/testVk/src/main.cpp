#include "ofMain.h"
#include "ofApp.h"

//========================================================================
int main( ){

	ofVkWindowSettings settings;
	settings.setVkVersion( 1, 0, 11 );
	ofCreateWindow( settings );

	ofRunApp(new ofApp());
}
