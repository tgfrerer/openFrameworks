#include "ofApp.h"
#include "vk/ofVkRenderer.h"
#include "vk/DrawCommand.h"

#include "vk/RenderBatch.h"

void Teapot::setup(){
	auto & renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );

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

	dc = std::move(std::make_unique<of::DrawCommand>( dcs ));

}
//--------------------------------------------------------------

void Teapot::update(){
	
}

//--------------------------------------------------------------

void Teapot::draw(of::RenderPassContext& rp){

	// update uniforms inside the draw command 
	
	//dc.setDefaultMatrices(mCamera); // camera will do view and projection
	//dc.setUniform( "ModelViewMatrix", ofMatrix4x4() );
	//dc.setUniform( "globalColor", ofColor::white );
	//dc.setUniformTexture( "texName", tex, 0 );

	// update attribute buffer bindings
	rp.draw( dc );

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
	//							|		      |       if framebuffer is omitted, we assume the back buffer
	// of::RenderBatch batch( renderContext, frameBuffer);
	
	::vk::RenderPass  mRenderPass = *renderer->getDefaultRenderPass();  // needs to be created upfront
	::vk::Framebuffer mFramebuffer = renderer->getDefaultContext()->getFramebuffer();	// needs to be re-created each frame based on current viewport width.

	// the framebuffer contains the link from renderpass -> where image memory will be stored (which image views will receive image output)
	// ::vk::Framebuffer mFramebuffer = renderer::swapchain::getDefaultFramebuffer(mRenderPass);
	
	{
		of::RenderBatch batch( *renderer->getDefaultContext() );
		//batch.begin();
		// per render thread:
		{
			of::CommandBufferContext cmdCtx( batch );
			// begin command buffer
			{
				// this should create a framebuffer, inside the rendercontext, kept alife until rendercontext[frame] fence has signalled.
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
		
		// queue.submit( batch );
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

