#include "ofApp.h"
#include "ofVkRenderer.h"

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
	
}

//--------------------------------------------------------------
void ofApp::update(){
	ofSetWindowTitle( ofToString( ofGetFrameRate(), 10, ' ' ) );
}

//--------------------------------------------------------------
void ofApp::draw(){

	// Fetch the default context. This context is automatically
	// set up upon app initialisation to draw to the swapchain.
	auto & context = renderer->getDefaultContext();

	// Batch is a light-weight helper object which encapsulates
	// a Vulkan Command Buffer. The command buffer is associated 
	// with the context it has been created from. As long as the 
	// command buffer lives on the same thread as the context, and 
	// only uses resources which are either global readonly static, 
	// or resources which are temporarily allocated though the 
	// context inside the context's thread, this is thread-safe. 
	
	// setup the main pass renderbatch
	//
	std::vector<::vk::ClearValue> clearValues( 2 );
	clearValues[0].setColor( ( ::vk::ClearColorValue& )ofFloatColor::blueSteel );
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

	batch.begin();
	batch.draw( fullscreenQuad );
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
