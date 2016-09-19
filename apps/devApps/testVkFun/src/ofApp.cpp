#include "ofApp.h"
#include "vk/ofVkRenderer.h"
#include "vk/DrawCommand.h"

#include "vk/RenderBatch.h"

void Teapot::setup(){
	auto & renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

	::vk::RenderPass & renderPass = *renderer->getDefaultRenderPass();  // needs to be created upfront

	// shader creation makes shader reflect. 
	auto mShaderDefault = std::shared_ptr<of::vk::Shader>(new of::vk::Shader( renderer->getVkDevice(),
	{
		{ ::vk::ShaderStageFlagBits::eVertex  , "default.vert" },
		{ ::vk::ShaderStageFlagBits::eFragment, "default.frag" },
	}));

	of::DrawCommandInfo dcs;

	//!TODO: this is far from ideal - a pipeline should start out fully setup.
	dcs.modifyPipeline().setup();

	dcs.modifyPipeline().depthStencilState
		.setDepthTestEnable( VK_TRUE )
		.setDepthWriteEnable( VK_TRUE )
		;
	dcs.modifyPipeline().inputAssemblyState.setTopology( ::vk::PrimitiveTopology::eTriangleList );
	dcs.modifyPipeline().setShader( mShaderDefault );
	dcs.modifyPipeline().setRenderPass( renderPass );

	dc = std::move(std::make_unique<of::DrawCommand>( dcs ));

}
//--------------------------------------------------------------

void Teapot::update(){
	
}

//--------------------------------------------------------------

void Teapot::draw(of::RenderBatch& rb){

	// update uniforms inside the draw command 
	
	//dc.setDefaultMatrices(mCamera); // camera will do view and projection
	dc->setUniform("globalColor", ofFloatColor::magenta );
	//dc.setUniform( "globalColor", ofColor::white );
	//dc.setUniformTexture( "texName", tex, 0 );

	// update attribute buffer bindings
	rb.draw( *dc );

}



//--------------------------------------------------------------
void ofApp::setup(){
	
	const auto & renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

	//of::RenderContext::Settings renderContextSettings;
	//
	//renderContextSettings.transientMemoryAllocatorSettings
	//	.setPhysicalDeviceMemoryProperties ( renderer->getVkPhysicalDeviceMemoryProperties() )
	//	.setPhysicalDeviceProperties       ( renderer->getVkPhysicalDeviceProperties() )
	//	.setFrameCount                     ( 2 )
	//	.setDevice                         ( renderer->getVkDevice() )
	//	.setSize                           ( ( 1UL << 24 ) * 2)  // (16 MB * number of frames))
	//	;

	//mRenderContext = std::make_unique<of::RenderContext>(renderContextSettings);

	//mRenderContext = renderer->getDefaultContext();

	mTeapot.setup();
}

//--------------------------------------------------------------
void ofApp::update(){

}

//--------------------------------------------------------------
void ofApp::draw(){
	const auto & renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );
	
	of::RenderBatch batch( *renderer->getDefaultContext() /*, reorder = true*/ );
	
	// a renderbatch 
	
	// we can't specify the framebuffer upfront, as the framebuffer is 
	// created at frame start - based on what?
	//
	// the framebuffer is created to link the current renderpass with 
	// images so that the output can be stored somewhere.
	//
	// the framebuffer is created inside the renderer - and it is the renderer which
	// will connect the default rendercontext/framebuffer to the outputs of the swapchain.
	//


	mTeapot.draw( batch );
	mTeapot.draw( batch );
	mTeapot.draw( batch );


	batch.submit();

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

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}

