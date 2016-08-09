#include "ofMain.h"
#include "ofApp.h"

//========================================================================
int main( ){

	ofVkWindowSettings settings;
	settings.setVkVersion( 1, 0, 21 );
	settings.numSwapchainImages = 2;
	settings.numVirtualFrames = 3;
	//settings.swapchainType = VK_PRESENT_MODE_MAILBOX_KHR;
	ofCreateWindow( settings );

	ofRunApp(new ofApp());
}
