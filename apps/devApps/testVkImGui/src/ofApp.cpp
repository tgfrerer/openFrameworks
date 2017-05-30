#include "ofApp.h"
#include "ofVkRenderer.h"
#include "EngineVk.h"

// We keep a shared pointer to the renderer so we don't have to 
// fetch it anew every time we need it.
shared_ptr<ofVkRenderer> renderer;

//--------------------------------------------------------------
void ofApp::setup(){
	ofDisableSetupScreen();

	renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

	{
		// Set up a Draw Command which draws a full screen quad.
		//
		// This command uses the vertex shader to emit vertices, so 
		// doesn't need any geometry to render. 

		of::vk::Shader::Settings shaderSettings;

		shaderSettings.device = renderer->getVkDevice();
		shaderSettings.printDebugInfo = true;
		shaderSettings.sources[::vk::ShaderStageFlagBits::eVertex  ] = "fullScreenQuad.vert";
		shaderSettings.sources[::vk::ShaderStageFlagBits::eFragment] = "fullScreenQuad.frag";

		mShaderFullscreen = std::make_shared<of::vk::Shader>( shaderSettings );

		of::vk::GraphicsPipelineState pipeline;

		pipeline.setShader( mShaderFullscreen );
		
		// Our full screen quad needs to draw just the back face. This is due to 
		// how we emit the vertices on the vertex shader. Since this differs from
		// the default (back culling) behaviour, we have to set this explicitly.
		pipeline.rasterizationState
			.setCullMode( ::vk::CullModeFlagBits::eFront )
			.setFrontFace( ::vk::FrontFace::eCounterClockwise );
		
		// We don't care about depth testing when drawing the full screen quad. 
		// It shall always cover the full screen.
		pipeline.depthStencilState
			.setDepthTestEnable( VK_FALSE )
			.setDepthWriteEnable( VK_FALSE )
			;
		pipeline.blendAttachmentStates[0].blendEnable = VK_TRUE;

		fullscreenQuad.setup( pipeline );
		
		// As this draw command issues vertices on the vertex shader
		// we must tell it how many vertices to render.
		fullscreenQuad.setNumVertices( 3 );
	}
	

	
	mGui.setup();

}

//--------------------------------------------------------------

void ofApp::exit(){
	// we set the logger back to console, so that we don't try to write to the wrong channel upon exit.
	auto logger = new ofConsoleLoggerChannel();
	ofSetLoggerChannel( std::shared_ptr<ofBaseLoggerChannel>( logger, []( ofBaseLoggerChannel * lhs ){} ) );
}

//--------------------------------------------------------------
void ofApp::update(){
	
}

// Usage:
//  static ExampleAppLog my_log;
//  my_log.AddLog("Hello %d world\n", 123);
//  my_log.Draw("title");
struct ExampleAppLog
{
	ImGuiTextBuffer&    Buf = ofxImGui::LoggerChannel::getBuffer();
	ImGuiTextFilter     Filter;
	ImVector<int>       LineOffsets;        // Index to lines offset
	bool                ScrollToBottom;

	void    Clear(){
		Buf.clear(); LineOffsets.clear();
	}

	void    AddLog( const char* fmt, ... ) IM_PRINTFARGS( 2 ){
		int old_size = Buf.size();
		va_list args;
		va_start( args, fmt );
		Buf.appendv( fmt, args );
		va_end( args );
		for ( int new_size = Buf.size(); old_size < new_size; old_size++ )
			if ( Buf[old_size] == '\n' )
				LineOffsets.push_back( old_size );
		ScrollToBottom = true;
	}

	void    Draw( const char* title, bool* p_open = NULL ){
		ImGui::SetNextWindowSize( ImVec2( 500, 400 ), ImGuiSetCond_FirstUseEver );
		ImGui::Begin( title, p_open );
		if ( ImGui::Button( "Clear" ) ) Clear();
		ImGui::SameLine();
		bool copy = ImGui::Button( "Copy" );
		ImGui::SameLine();
		Filter.Draw( "Filter", -100.0f );
		ImGui::Separator();
		ImGui::BeginChild( "scrolling", ImVec2( 0, 0 ), false, ImGuiWindowFlags_HorizontalScrollbar );
		if ( copy ) ImGui::LogToClipboard();

		if ( Filter.IsActive() ){
			const char* buf_begin = Buf.begin();
			const char* line = buf_begin;
			for ( int line_no = 0; line != NULL; line_no++ ){
				const char* line_end = ( line_no < LineOffsets.Size ) ? buf_begin + LineOffsets[line_no] : NULL;
				if ( Filter.PassFilter( line, line_end ) )
					ImGui::TextUnformatted( line, line_end );
				line = line_end && line_end[1] ? line_end + 1 : NULL;
			}
		} else{
			ImGui::TextUnformatted( Buf.begin() );
		}

		if ( ScrollToBottom )
			ImGui::SetScrollHere( 1.0f );
		ScrollToBottom = false;
		ImGui::EndChild();
		ImGui::End();
	}
};

//--------------------------------------------------------------
void ofApp::draw(){

	static ofFloatColor backgroundColor = ofFloatColor::fuchsia;

	static ExampleAppLog appLog;
	static bool appLogOpen = true;
	static bool debugWindowOpen = true;

	auto mainSettings = ofxImGui::Settings();
	mGui.begin();

	{
		ImGui::SetNextWindowSize( ImVec2( 500, 400 ), ImGuiSetCond_FirstUseEver );
		ImGui::Begin("Debug window", &debugWindowOpen );
		static float floatValue = 0.f;
		
		ImGui::Text( "Hello, Vulkan!" );

		// This will change the app background color
		ImGui::ColorEdit4( "Background Color", &backgroundColor.r, false );
		ImGui::Text( "Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate );
		ImGui::End();
		appLog.Draw("log", &appLogOpen);
	}



	// Fetch the default context. This context is automatically
	// set up upon app initialisation to draw to the swapchain.
	auto & context = renderer->getDefaultContext();

	// Batch is a light-weight helper object which encapsulates
	// a Vulkan Command Buffer. The command buffer is associated 
	// with the context it has been created from. As long as the 
	// command buffer lives on the same thread as the context, and 
	// only uses resources which are either global readonly static, 
	// or resources which are temporarily allocated though the 
	// context inside the context's thread, this is considered 
	// thread-safe. 
	
	// setup the main pass renderbatch
	//
	std::vector<::vk::ClearValue> clearValues( 2 );
	clearValues[0].setColor( ( ::vk::ClearColorValue& )backgroundColor );
	clearValues[1].setDepthStencil( { 1.f, 0 } );

	of::vk::RenderBatch::Settings settings;
	settings.clearValues = clearValues;
	settings.context = context.get();
	settings.framebufferAttachmentHeight = renderer->getSwapchain()->getHeight();
	settings.framebufferAttachmentWidth  = renderer->getSwapchain()->getWidth();
	settings.renderArea = ::vk::Rect2D( {}, { uint32_t( renderer->getViewportWidth() ), uint32_t( renderer->getViewportHeight() ) } );
	settings.renderPass = *renderer->getDefaultRenderpass();
	settings.framebufferAttachments = {
		context->getSwapchainImageView(),
		renderer->getDepthStencilImageView()
	};

	of::vk::RenderBatch batch{ settings };
	auto & imguiEngine = *(dynamic_cast<ofxImGui::EngineVk*>( mGui.engine ));

	batch.begin();
	{
		batch.draw( fullscreenQuad );
		imguiEngine.setRenderBatch( batch );
		mGui.end(); // renders imgui into current batch
	}
	batch.end();

}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){

}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){
	if ( key == ' ' ){
		// Recompile the full screen shader and 
		// touch (force implicit re-creation of) any 
		// associated pipelines.
		mShaderFullscreen->compile();
	}
	else if ( key == 'f' ){
		ofToggleFullscreen();
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
