#include "ofMain.h"
#include "ofApp.h"

int main(){
	
	// Basic initialisation (mostly setup timers, and randseed)
	ofInit();

	auto consoleLogger = new ofConsoleLoggerChannel();
	ofSetLoggerChannel( std::shared_ptr<ofBaseLoggerChannel>( consoleLogger, []( ofBaseLoggerChannel * lhs){} ) );

	// Create a new window 
	auto mainWindow = std::make_shared<ofAppGLFWWindow>();
	
	// use this instead to render using the image swapchain
	// auto mainWindow = std::make_shared<ofAppVkNoWindow>();

	// Store main window in mainloop
	ofGetMainLoop()->addWindow( mainWindow );

	{
		ofVkWindowSettings settings;
		settings.setVkVersion( 1, 0, 46 );
		settings.numSwapchainImages = 3;
		settings.numVirtualFrames = 3;
		settings.presentMode = ::vk::PresentModeKHR::eMailbox;

		// Only load debug layers if app is compiled in Debug mode
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
