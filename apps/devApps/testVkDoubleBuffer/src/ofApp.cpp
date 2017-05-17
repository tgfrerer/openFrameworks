#include "ofApp.h"
#include "ofVkRenderer.h"

// We keep a shared pointer to the renderer so we don't have to 
// fetch it anew every time we need it.
shared_ptr<ofVkRenderer> renderer;

const glm::mat4x4 cClipMatrix {
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, -1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 0.5f, 0.0f,
	0.0f, 0.0f, 0.5f, 1.0f
};


//--------------------------------------------------------------

void ofApp::setup(){
	ofDisableSetupScreen();

	renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );
	setupPrepass();
	setupDrawCommands();

	mMeshIco = std::make_shared<ofMesh>();
	*mMeshIco = ofBoxPrimitive(100,100,100,1,1,1).getMesh();

	mMeshIco->clearTexCoords();

	mMeshPlane = std::make_shared<ofMesh>( ofMesh::plane( 512, 256, 2, 2, OF_PRIMITIVE_TRIANGLES ) );

	setupMeshL();

}

//--------------------------------------------------------------

void ofApp::setupPrepass(){

	auto & device = renderer->getVkDevice();

	// set dimensions for aux render target
	mPrepassRect.setExtent( { 512, 256 } );
	mPrepassRect.setOffset( { 0, 0 } );

	// Create a Renderpass which defines dependencies,
	// attachments, initialisation behaviour, and colour 
	// formats for the subpass.
	{
		std::array<vk::AttachmentDescription, 1> attachments;

		attachments[0]		// color attachment
			.setFormat( ::vk::Format::eR8G8B8A8Unorm )
			.setSamples( ::vk::SampleCountFlagBits::e1 )
			.setLoadOp( ::vk::AttachmentLoadOp::eClear )  // <-- try setting this to  vk::AttachmentLoadOp::eDontCare and see what happens!
			.setStoreOp( ::vk::AttachmentStoreOp::eStore )
			.setStencilLoadOp( ::vk::AttachmentLoadOp::eDontCare )
			.setStencilStoreOp( ::vk::AttachmentStoreOp::eDontCare )
			.setInitialLayout( ::vk::ImageLayout::eUndefined )
			.setFinalLayout( ::vk::ImageLayout::eShaderReadOnlyOptimal )
			;

		// Define 2 attachments, and tell us what layout to expect these to be in.
		// Index references attachments from above.

		vk::AttachmentReference colorReference{ 0, ::vk::ImageLayout::eColorAttachmentOptimal };

		vk::SubpassDescription subpassDescription;
		subpassDescription
			.setPipelineBindPoint( ::vk::PipelineBindPoint::eGraphics )
			.setColorAttachmentCount( 1 )
			.setPColorAttachments( &colorReference )
			.setPDepthStencilAttachment( nullptr )
			;

		// Define 2 dependencies for subpass 0

		std::array<vk::SubpassDependency, 2> dependencies;
		dependencies[0]
			.setSrcSubpass( VK_SUBPASS_EXTERNAL ) // producer
			.setDstSubpass( 0 )                   // consumer
			.setSrcStageMask( ::vk::PipelineStageFlagBits::eBottomOfPipe )
			.setDstStageMask( ::vk::PipelineStageFlagBits::eColorAttachmentOutput )
			.setSrcAccessMask( ::vk::AccessFlagBits::eMemoryRead )
			.setDstAccessMask( ::vk::AccessFlagBits::eColorAttachmentWrite )
			.setDependencyFlags( ::vk::DependencyFlagBits::eByRegion )
			;
		dependencies[1]
			.setSrcSubpass( 0 )                   // producer
			.setDstSubpass( VK_SUBPASS_EXTERNAL ) // consumer
			.setSrcStageMask( ::vk::PipelineStageFlagBits::eColorAttachmentOutput )
			.setDstStageMask( ::vk::PipelineStageFlagBits::eTopOfPipe )
			.setSrcAccessMask( ::vk::AccessFlagBits::eColorAttachmentWrite )
			.setDstAccessMask( ::vk::AccessFlagBits::eMemoryRead )
			.setDependencyFlags( vk::DependencyFlagBits::eByRegion )
			;

		// Define 1 renderpass with 1 subpass

		vk::RenderPassCreateInfo renderPassCreateInfo;
		renderPassCreateInfo
			.setAttachmentCount( attachments.size() )
			.setPAttachments( attachments.data() )
			.setSubpassCount( 1 )
			.setPSubpasses( &subpassDescription )
			.setDependencyCount( dependencies.size() )
			.setPDependencies( dependencies.data() );

		auto renderPass = device.createRenderPass( renderPassCreateInfo );

		// We put renderpass into a shared_ptr so we can be sure it will get destroyed on app teardown
		mPrepassRenderPass = std::shared_ptr<::vk::RenderPass>( new ::vk::RenderPass( renderPass ), [d = device]( ::vk::RenderPass* rp ){
			d.destroyRenderPass( *rp );
			delete rp;
		} );
	}

	// ---- Allocate imgages for context to render into
	{
		// we allocate 2 images, so that we could ping-pong between
		// rendertargets. In this example, we're not really 
		// making full use of this, to keep things simple.

		size_t numImages = 2;

		of::vk::ImageAllocator::Settings allocatorSettings;
		allocatorSettings.device = device;
		allocatorSettings.imageTiling = vk::ImageTiling::eOptimal;
		allocatorSettings.imageUsageFlags = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
		allocatorSettings.memFlags = vk::MemoryPropertyFlagBits::eDeviceLocal;
		allocatorSettings.size = mPrepassRect.extent.width * mPrepassRect.extent.height * 4 * numImages;
		allocatorSettings.physicalDeviceMemoryProperties = renderer->getVkPhysicalDeviceMemoryProperties();
		allocatorSettings.physicalDeviceProperties = renderer->getVkPhysicalDeviceProperties();
		mImageAllocator = std::make_shared<of::vk::ImageAllocator>( allocatorSettings );

		mImageAllocator->setup();

		::vk::ImageCreateInfo imageCreateInfo;
		imageCreateInfo
			.setImageType( ::vk::ImageType::e2D )
			.setFormat( ::vk::Format::eR8G8B8A8Unorm )
			.setExtent( { mPrepassRect.extent.width, mPrepassRect.extent.height, 1 } )
			.setMipLevels( 1 )
			.setArrayLayers( 1 )
			.setSamples( ::vk::SampleCountFlagBits::e1 )
			.setTiling( ::vk::ImageTiling::eOptimal )
			.setUsage( ::vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled )
			.setSharingMode( ::vk::SharingMode::eExclusive )
			.setQueueFamilyIndexCount( 0 )
			.setPQueueFamilyIndices( nullptr )
			.setInitialLayout( ::vk::ImageLayout::eUndefined )
			;

		::vk::ImageViewCreateInfo imageViewCreateInfo;

		// Todo: these elements need to be destroyed when the app quits.
		for ( size_t i = 0; i != numImages; ++i ){
			auto img = device.createImage( imageCreateInfo );
			imageViewCreateInfo
				.setImage( img )
				.setViewType( ::vk::ImageViewType::e2D )
				.setFormat( imageCreateInfo.format )
				.setComponents( {} ) // identity
				.setSubresourceRange( { ::vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 } )
				;

			::vk::DeviceSize offset;
			mImageAllocator->allocate( mPrepassRect.extent.width * mPrepassRect.extent.height * 4, offset );
			device.bindImageMemory( img, mImageAllocator->getDeviceMemory(), offset );

			auto view = device.createImageView( imageViewCreateInfo );

			mTargetImages[i].image = img;
			mTargetImages[i].view = view;

			mTexture[i] = std::make_shared<of::vk::Texture>( device, img );
		}
	}


	mCamPrepass.setupPerspective( false, 60, 0.1, 500 );
	mCamPrepass.setGlobalPosition( 0, 0, mCam.getImagePlaneDistance( { 0,0,512,256 } ) );
	mCamPrepass.lookAt( { 0,0,0 }, { 0,1,0 } );

	mCam.setupPerspective( false, 60 );
	mCam.setPosition( { 0,0, mCam.getImagePlaneDistance() } );
	mCam.lookAt( { 0,0,0 } );
	mCam.setEvents( ofEvents() );
	
}

//--------------------------------------------------------------

void ofApp::setupDrawCommands(){
	
	of::vk::Shader::Settings shaderSettings;

	shaderSettings.device = renderer->getVkDevice();
	shaderSettings.printDebugInfo = true;

	shaderSettings.sources[::vk::ShaderStageFlagBits::eVertex  ] = "fullScreenQuad.vert";
	shaderSettings.sources[::vk::ShaderStageFlagBits::eFragment] = "fullScreenQuad.frag";
	mShaderFullscreen = std::make_shared<of::vk::Shader>( shaderSettings );

	shaderSettings.sources[::vk::ShaderStageFlagBits::eVertex  ] = "default.vert";
	shaderSettings.sources[::vk::ShaderStageFlagBits::eFragment] = "default.frag";
	auto mShaderDefault = std::make_shared<of::vk::Shader>( shaderSettings );

	shaderSettings.sources[::vk::ShaderStageFlagBits::eVertex  ] = "textured.vert";
	shaderSettings.sources[::vk::ShaderStageFlagBits::eFragment] = "textured.frag";
	auto mShaderTextured = std::make_shared<of::vk::Shader>( shaderSettings );

	
	{
		// Set up a Draw Command which draws a full screen quad.
		//
		// This command uses the vertex shader to emit vertices, so 
		// doesn't need any geometry to render. 

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

		const_cast<of::vk::DrawCommand&>( fullscreenQuad).setup( pipeline );

		// As this draw command issues vertices on the vertex shader
		// we must tell it how many vertices to render.
		const_cast<of::vk::DrawCommand&>( fullscreenQuad).setNumVertices( 3 );
	}

	{   
		// Draw Command which draws geometry as outlines

		of::vk::GraphicsPipelineState pipeline;
		pipeline.setShader( mShaderDefault );

		pipeline.depthStencilState
			.setDepthTestEnable( VK_TRUE )
			.setDepthWriteEnable( VK_TRUE )
			;

		pipeline.inputAssemblyState.setTopology( ::vk::PrimitiveTopology::eTriangleList );
		pipeline.rasterizationState.setPolygonMode( ::vk::PolygonMode::eLine );
		pipeline.blendAttachmentStates[0]
			.setBlendEnable( VK_TRUE )
			;

		const_cast<of::vk::DrawCommand&>( outlinesDraw ).setup( pipeline );
	}

	{
		// Draw command which draws textured geometry

		of::vk::GraphicsPipelineState pipeline;
		pipeline.setShader( mShaderTextured );

		pipeline.rasterizationState.setCullMode( ::vk::CullModeFlagBits::eBack );
		pipeline.rasterizationState.setFrontFace( ::vk::FrontFace::eCounterClockwise );
		pipeline.depthStencilState
			.setDepthTestEnable( VK_TRUE )
			.setDepthWriteEnable( VK_TRUE )
			;
		pipeline.blendAttachmentStates[0]
			.setBlendEnable(VK_TRUE);

		const_cast<of::vk::DrawCommand&>( drawTextured ).setup( pipeline );
	}
}

//--------------------------------------------------------------

void ofApp::update(){
	ofSetWindowTitle( ofToString( ofGetFrameRate(), 10, ' ' ) );
}

//--------------------------------------------------------------

void ofApp::draw(){

	static uint32_t pingPong = 0;
	auto & context = renderer->getDefaultContext();

	{   // prepass 

		// setup the prepass renderbatch
		//
		std::vector<::vk::ClearValue> clearValues( 1 );
		clearValues[0].setColor( ( ::vk::ClearColorValue& )ofFloatColor::bisque );

		of::vk::RenderBatch::Settings settings;
		settings.clearValues = clearValues;
		settings.context = renderer->getDefaultContext().get();
		settings.framebufferAttachmentHeight = mPrepassRect.extent.height;
		settings.framebufferAttachmentWidth = mPrepassRect.extent.width;
		settings.renderArea = mPrepassRect;
		settings.renderPass = *mPrepassRenderPass;
		settings.framebufferAttachments = {
			mTargetImages[pingPong].view,    // << this image is where the result of our prepass will be stored
		};

		of::vk::RenderBatch prepass{ settings };

		auto viewMatrix = mCamPrepass.getModelViewMatrix();
		auto projectionMatrix = cClipMatrix * mCamPrepass.getProjectionMatrix( { 0, 0, float( mPrepassRect.extent.width ), float( mPrepassRect.extent.height ) } );

		ofMatrix4x4 modelMatrix = glm::rotate( float( TWO_PI * ( ( ofGetFrameNum() % 360 ) / 360.f ) ), glm::vec3( { 0.f, 1.f, 1.f } ) );

		auto meshDraw = outlinesDraw;

		ofFloatColor col = ofColor::white;
		col.lerp( ofColor::blue, 0.5 + 0.5 * sinf(TWO_PI * (ofGetFrameNum() % 360 ) / 360.f ));

		meshDraw
			.setUniform( "projectionMatrix", projectionMatrix )
			.setUniform( "viewMatrix", viewMatrix )
			.setUniform( "modelMatrix", modelMatrix )
			.setUniform( "globalColor", col )
			.setMesh(mMeshIco)
			.setDrawMethod( of::vk::DrawCommand::DrawMethod::eIndexed )
			;

		prepass.begin();
		prepass.draw( meshDraw );
		prepass.end();
		
	}

	{   // main pass

		// setup the main pass renderbatch
		//
		std::vector<::vk::ClearValue> clearValues( 2 );
		clearValues[0].setColor( ( ::vk::ClearColorValue& )ofFloatColor::blueSteel );
		clearValues[1].setDepthStencil( { 1.f, 0 } );

		of::vk::RenderBatch::Settings settings;
		settings.clearValues = clearValues;
		settings.context = renderer->getDefaultContext().get();
		settings.framebufferAttachmentHeight = renderer->getSwapchain()->getHeight();
		settings.framebufferAttachmentWidth = renderer->getSwapchain()->getWidth();
		settings.renderArea = ::vk::Rect2D( {}, { uint32_t( renderer->getViewportWidth() ), uint32_t( renderer->getViewportHeight() ) } );
		settings.renderPass = *renderer->getDefaultRenderpass();
		settings.framebufferAttachments = {
			context->getSwapchainImageView(),
			renderer->getDepthStencilImageView()
		};

		of::vk::RenderBatch batch{ settings };

		auto viewMatrix = mCam.getModelViewMatrix();
		auto projectionMatrix = cClipMatrix * mCam.getProjectionMatrix();

		auto texturedRect = drawTextured;
		texturedRect
			.setUniform( "projectionMatrix", projectionMatrix )
			.setUniform( "viewMatrix", viewMatrix )
			.setUniform( "modelMatrix", glm::mat4() )
			.setTexture( "tex_0", *mTexture[(pingPong + 1 ) % 2] )
			.setMesh( mMeshPlane )
			.setDrawMethod( of::vk::DrawCommand::DrawMethod::eIndexed )
			;

		batch.begin();
		batch
			.draw( fullscreenQuad )
			.draw( texturedRect )    // draw result from previous render pass onto screen
			;
		batch.end();

	}

	// Note that ping-pong in this case doesn't really do anything, 
	// as the way we have setup our renderpasses, their dependencies (outside writes 
	// must have finished before reading inside the renderpass) warrant that
	// the result of our prepass is available for the main pass to draw. 
	pingPong = ( pingPong + 1) % 2;
}

//--------------------------------------------------------------
void ofApp::exit(){
	// cleanup
	
	auto & device = renderer->getVkDevice();
	
	device.waitIdle();

	for ( auto& i : mTargetImages ){
			device.destroyImageView( i.view );
			device.destroyImage( i.image );
	}

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
	mCam.setControlArea( { 0,0,float( w ),float( h ) } );
}

//--------------------------------------------------------------
void ofApp::gotMessage(ofMessage msg){

}

//--------------------------------------------------------------
void ofApp::dragEvent(ofDragInfo dragInfo){ 

}
