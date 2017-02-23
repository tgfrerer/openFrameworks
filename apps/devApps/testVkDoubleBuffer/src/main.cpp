#include "ofMain.h"
#include "ofApp.h"

int main(){
	// Do basic initialisation (mostly setup timers, and randseed)
	ofInit();

	auto consoleLogger = new ofConsoleLoggerChannel();
	ofSetLoggerChannel( std::shared_ptr<ofBaseLoggerChannel>( consoleLogger, []( ofBaseLoggerChannel * lhs){} ) );

	// Create a new window 
	auto mainWindow = std::make_shared<ofAppGLFWWindow>();
	//auto mainWindow = std::make_shared<ofAppVkNoWindow>();

	// Store main window in mainloop
	ofGetMainLoop()->addWindow( mainWindow );

	{
		ofVkWindowSettings settings;
		settings.setVkVersion( 1, 0, 39 );
		settings.numSwapchainImages = 3;
		settings.numVirtualFrames = 3;
		settings.presentMode = ::vk::PresentModeKHR::eFifo;

#ifdef NDEBUG
		settings.useDebugLayers = false;
#else
		settings.useDebugLayers = true;
#endif

		// Initialise main window, and associated renderer.
		mainWindow->setup( settings );
}

	// Initialise and start application
	ofRunApp( new ofApp() );

}
