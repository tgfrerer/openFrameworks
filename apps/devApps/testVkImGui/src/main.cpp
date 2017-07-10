#include "ofMain.h"
#include "ofApp.h"
#include "ofxImGuiLoggerChannel.h"

int main(){
	
	// Basic initialisation (mostly setup timers, and randseed)
	ofInit();

	//auto consoleLogger = new ofConsoleLoggerChannel();
	//ofSetLoggerChannel( std::shared_ptr<ofBaseLoggerChannel>( consoleLogger, []( ofBaseLoggerChannel * lhs){ } ) );

	// set the logger to imgui - this will allow imgui to use its own logger.
	auto imGuiLogger = new ofxImGui::LoggerChannel();
	ofSetLoggerChannel( std::shared_ptr<ofBaseLoggerChannel>( imGuiLogger, []( ofBaseLoggerChannel * lhs ){} ) );


	// Create a new window 
	auto mainWindow = std::make_shared<ofAppGLFWWindow>();
	
	// use this instead to render using the image swapchain
	// auto mainWindow = std::make_shared<ofAppVkNoWindow>();

	// Store main window in mainloop
	ofGetMainLoop()->addWindow( mainWindow );

	{
		ofVkWindowSettings settings;
		settings.rendererSettings.setVkVersion( 1, 0, 46 );
		settings.rendererSettings.numSwapchainImages = 3;
		settings.rendererSettings.numVirtualFrames = 3;
		settings.rendererSettings.presentMode = ::vk::PresentModeKHR::eMailbox;

		// Only load debug layers if app is compiled in Debug mode
#ifdef NDEBUG
		settings.rendererSettings.useDebugLayers = false;
#else
		settings.rendererSettings.useDebugLayers = true;
#endif

		// Initialise main window, and associated renderer.
		mainWindow->setup( settings );
}

	// Initialise and start application
	ofRunApp( new ofApp() );

}
