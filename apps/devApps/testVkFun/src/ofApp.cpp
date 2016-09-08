#include "ofApp.h"
#include "vk/ofVkRenderer.h"
#include "vk/DrawCommand.h"

#include "vk/RenderBatch.h"

void Teapot::setup(){
	const auto & renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

	of::vk::Shader::Settings shaderSettings{
		renderer->getShaderManager(),
		{
			{ ::vk::ShaderStageFlagBits::eVertex  , "default.vert" },
			{ ::vk::ShaderStageFlagBits::eFragment, "default.frag" },
		}
	};

	// shader creation makes shader reflect. 
	auto mShaderDefault = std::make_shared<of::vk::Shader>( shaderSettings );

	of::DrawCommandInfo dcs;

	dcs.getPipeline().depthStencilState
		.setDepthTestEnable( VK_TRUE )
		.setDepthWriteEnable( VK_TRUE )
		;
	dcs.getPipeline().inputAssemblyState.setTopology( ::vk::PrimitiveTopology::eTriangleList );

	dcs.getPipeline().setShader( mShaderDefault );

	dc = std::make_unique<of::DrawCommand>( dcs );

}
//--------------------------------------------------------------

void Teapot::update(){

}

//--------------------------------------------------------------

void Teapot::draw(of::RenderPassContext& rp){

	// update uniforms 
	// update attribute buffer bindings

	//rp.storeUniforms( dc );
	
	// optional - otherwise, indices will be read from 
	// device local memory.

	// rp.storeAttributes( dc );
	// rp.storeIndices( dc );
	
	rp.draw( dc );

}



//--------------------------------------------------------------
void ofApp::setup(){
	
	const auto & renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

	of::RenderContext::Settings renderContextSettings;
	
	renderContextSettings.transientMemoryAllocatorSettings
		.setPhysicalDeviceMemoryProperties ( renderer->getVkPhysicalDeviceMemoryProperties() )
		.setPhysicalDeviceProperties       ( renderer->getVkPhysicalDeviceProperties() )
		.setFrameCount                     ( 2 )
		.setDevice                         ( renderer->getVkDevice() )
		.setSize                           ( ( 2UL << 24 ) * 2)  // (16 MB * number of frames))
		;

	mRenderContext = std::make_unique<of::RenderContext>(renderContextSettings);

	mTeapot.setup();
}

//--------------------------------------------------------------
void ofApp::update(){

}

//--------------------------------------------------------------
void ofApp::draw(){

	/*
	
	drawing should be about laying out the different render passes
	and their dependencies.
	
	*/
	
	// framestore is where transient data is saved in. 
	// this data is kept alife until the batch has finished
	// rendering and has come around.
	//
	// 						  renderContext holds all transient memory and pools 
	//							|
	// 							|		  framebuffer is backing image memory - where results are stored
	//							|		      |       if framebuffer is ommitted, we assume the back buffer
	// of::RenderBatch batch( renderContext, frameBuffer);
	
	::vk::RenderPass mRenderPass;   // needs to be created upfront
	::vk::Framebuffer mFramebuffer;	// needs to be re-created each frame
	
	of::RenderBatch batch(*mRenderContext);
	{
		// per render thread:
		{
			of::CommandBufferContext cmdCtx( batch );
			// begin command buffer
			{
				of::RenderPassContext renderPassCtx( cmdCtx, mRenderPass, mFramebuffer );
				// begin renderpass	 (this should also include the renderpass: ::vk::RenderPass)
				mTeapot.draw( renderPassCtx );
				mTeapot.draw( renderPassCtx );
				auto subpassId = renderPassCtx.nextSubpass();
				mTeapot.draw( renderPassCtx );
				// end renderpass
			}

			{
				// begin another renderpass
				// end another renderpass
			}
			// end command buffer 
		}
	}

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

