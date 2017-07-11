#include "ofApp.h"
#include "ofVkRenderer.h"
#include "vk/ofAppVkNoWindow.h"

// We keep a pointer to the renderer so we don't have to 
// fetch it anew every time we need it.
ofVkRenderer* renderer;

//--------------------------------------------------------------
void ofApp::setup(){

	ofDisableSetupScreen();

	ofSetFrameRate( 0 );

	renderer = dynamic_cast<ofVkRenderer*>( ofGetCurrentRenderer().get() );

	setupDrawCommands();
	setupTextureData();
	setupStaticGeometry();

	flagStripDraw
		.setAttribute( 0, flagVertices )
		.setAttribute( 1, flagTexCoords )
		.setIndices( flagIndices )
		.setDrawMethod( of::vk::DrawCommand::DrawMethod::eIndexed )
		.setNumIndices( flagIndices.numElements )
		.setInstanceCount( 400 )
		.setTexture( "tex_0", *mFlagTexture )
		;

	backGroundDraw
		.setNumVertices( 3 )
		;

	mCam.setupPerspective( false, 60, 0.f, 5000 );
	mCam.setPosition( { 0, 0, mCam.getImagePlaneDistance() } );
	mCam.lookAt( { 0, 0, 0 } );
	
	if ( auto ptr = std::dynamic_pointer_cast<ofAppVkNoWindow>( ofGetCurrentWindow() ) ){
		ofLog() << "Running in headless mode";
		// When we're in headless mode, we want to make sure to create animation 
		// at the right tempo - here we set the frame rate to 12, which is what 
		// we need to create an animated gif.
		ofSetTimeModeFixedRate( ofGetFixedStepForFps( 12 ) );
	} else{
		ofLog() << "Running in regular mode";
		mCam.setEvents( ofEvents() );
	}

	mCam.setGlobalOrientation( { 0.923094f, 0.359343f, -0.125523f, -0.0548912f } );
	mCam.setGlobalPosition( { -389.696f, -509.342f, 422.886f } );
}

//--------------------------------------------------------------

void ofApp::setupDrawCommands(){
	
	of::vk::Shader::Settings shaderSettings;
	shaderSettings.device = renderer->getVkDevice();
	shaderSettings.printDebugInfo = true;

		{

			shaderSettings.setSource( ::vk::ShaderStageFlagBits::eVertex  , "shaders/flag.vert" );
			shaderSettings.setSource( ::vk::ShaderStageFlagBits::eFragment, "shaders/flag.frag" );

			// Shader which will draw animated and textured flag
			mFlagShader = std::make_shared<of::vk::Shader>( shaderSettings );

			// Define pipeline state to use with draw command
			of::vk::GraphicsPipelineState pipeline;

			pipeline.setShader( mFlagShader );

			pipeline.rasterizationState
				.setPolygonMode( ::vk::PolygonMode::eFill )
				.setCullMode( ::vk::CullModeFlagBits::eNone )
				.setFrontFace( ::vk::FrontFace::eCounterClockwise )
				;
			pipeline.inputAssemblyState
				.setTopology( ::vk::PrimitiveTopology::eTriangleStrip )
				;
			pipeline.depthStencilState
				.setDepthTestEnable( VK_TRUE )
				.setDepthWriteEnable( VK_TRUE )
				;
			pipeline.blendAttachmentStates[0].blendEnable = VK_TRUE;

			// Setup draw command using pipeline state above
			flagStripDraw.setup( pipeline );
		}
		
		{
			shaderSettings.clearSources();
			shaderSettings.setSource( ::vk::ShaderStageFlagBits::eVertex  , "shaders/background.vert" );
			shaderSettings.setSource( ::vk::ShaderStageFlagBits::eFragment, "shaders/background.frag" );
			
			// Shader which draws a full screen quad without need for geometry input
			mBgShader = std::make_shared<of::vk::Shader>( shaderSettings );

			// Set up a Draw Command which draws a full screen quad.
			//
			// This command uses the vertex shader to emit vertices, so 
			// doesn't need any geometry to render. 

			of::vk::GraphicsPipelineState pipeline;
			pipeline.setShader( mBgShader );

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

			backGroundDraw.setup( pipeline );
		}

		{
			shaderSettings.clearSources();

			shaderSettings.setSource( ::vk::ShaderStageFlagBits::eVertex  , "shaders/lambert.vert" );
			shaderSettings.setSource( ::vk::ShaderStageFlagBits::eFragment, "shaders/lambert.frag" );
			
			// Shader which draws using global color, and "lambert" shading
			mLambertShader = std::make_shared<of::vk::Shader>( shaderSettings );

			of::vk::GraphicsPipelineState pipeline;
			pipeline.setShader( mLambertShader );

			pipeline.rasterizationState
				.setPolygonMode( ::vk::PolygonMode::eFill )
				.setCullMode( ::vk::CullModeFlagBits::eBack )
				.setFrontFace( ::vk::FrontFace::eClockwise )
				;
			pipeline.inputAssemblyState
				.setTopology( ::vk::PrimitiveTopology::eTriangleStrip )
				;
			pipeline.depthStencilState
				.setDepthTestEnable( VK_TRUE )
				.setDepthWriteEnable( VK_TRUE )
				;
			pipeline.blendAttachmentStates[0].blendEnable = VK_TRUE;

			lambertDraw.setup( pipeline );
		}

}

//--------------------------------------------------------------

void ofApp::setupTextureData(){
	{
		of::vk::ImageAllocator::Settings allocatorSettings;
		allocatorSettings
			.setRendererProperties( renderer->getVkRendererProperties() )
			.setSize( 1 << 24UL ) // 16 MB
			.setMemFlags( ::vk::MemoryPropertyFlagBits::eDeviceLocal )
			;

		mImageAllocator = std::make_unique<of::vk::ImageAllocator>( allocatorSettings );
		mImageAllocator->setup();
	}

	// Grab staging context to place pixel data there for upload to device local image memory
	auto & stagingContext = renderer->getStagingContext();

	ofPixels tmpPix;
	
	ofLoadImage( tmpPix, "helloworld-amatic.png" );
	
	// We must make sure our image has an alpha channel when we upload.
	tmpPix.setImageType( ofImageType::OF_IMAGE_COLOR_ALPHA );

	of::vk::ImageTransferSrcData imgTransferData;
	imgTransferData.pData = tmpPix.getData();
	imgTransferData.numBytes = tmpPix.size();
	imgTransferData.extent.width = tmpPix.getWidth();
	imgTransferData.extent.height = tmpPix.getHeight();

	// Queue pixel data for upload to device local memory via image allocator -
	//
	// This copies the image data immediately into the staging context's device and host visible memory area, 
	// and queues up a transfer command in the staging context.
	//
	// Which means that, since the staging context's memory is coherent, the transfer 
	// to staging memory has completed by the time the staging context begins executing 
	// its commands.
	//
	// The transfer command queued up earlier is then executed, and this command 
	// does the heavy lifting of transferring the image from staging memory to 
	// device-only target memory ownded by mImageAllocator
	mFlagImage = stagingContext->storeImageCmd( imgTransferData, mImageAllocator );

	// Create a Texture (which is a combination of ImageView+Sampler) using vk image
	mFlagTexture = std::make_shared<of::vk::Texture>( renderer->getVkDevice(), *mFlagImage );

}

//--------------------------------------------------------------

void ofApp::setupStaticGeometry(){

	of::vk::BufferAllocator::Settings as;
	as.device = renderer->getVkDevice();
	as.frameCount = 1;
	as.memFlags = ::vk::MemoryPropertyFlagBits::eDeviceLocal;
	as.physicalDeviceProperties = renderer->getVkPhysicalDeviceProperties();
	as.physicalDeviceMemoryProperties = renderer->getVkPhysicalDeviceMemoryProperties();
	as.queueFamilyIndices = { renderer->getVkRendererProperties().graphicsFamilyIndex };
	as.size = 1'000'000; // ~ 1 MB

	mStaticAllocator = std::make_unique<of::vk::BufferAllocator>(as);
	mStaticAllocator->setup();

	std::vector<glm::vec3>   vertices;
	std::vector<glm::vec2>   texCoords;
	std::vector<ofIndexType> indices;

	// Create geometry for single flag strip - which will get instanced when drawn
	size_t numElements = 1000;

	for ( size_t i = 0; i != numElements; ++i ){
		vertices.emplace_back( -float( numElements / 2 ) + float( i/2 ), float( i % 2 ), 0.f );
	}

	for ( size_t i = 0; i != numElements; ++i ){
		texCoords.emplace_back( float( i / 2 ) / float( numElements / 2), 1.f - float( i % 2 ) );
	}
	
	for ( size_t i = 0; i != numElements; ++i ){
		indices.emplace_back( ofIndexType( i ) );
	}

	std::vector<of::vk::TransferSrcData> transferSrc {
		{ vertices.data() , vertices.size() , sizeof( glm::vec3 )  },
		{ texCoords.data(), texCoords.size(), sizeof( glm::vec2 )  },
		{ indices.data()  , indices.size()  , sizeof( ofIndexType) },
	};

	// Create geometry for flagpole
	auto tmpMesh = ofCylinderPrimitive( 10, 800, 12, 2 ).getMesh();

	// Add created geometry to transfer jobs list
	transferSrc.push_back( { tmpMesh.getVerticesPointer(), tmpMesh.getNumVertices(), sizeof( ofDefaultVertexType ) } );
	transferSrc.push_back( { tmpMesh.getNormalsPointer(), tmpMesh.getNumVertices(), sizeof( ofDefaultNormalType ) } );
	transferSrc.push_back( { tmpMesh.getIndexPointer(), tmpMesh.getNumIndices(), sizeof( ofIndexType ) } );

	// Upload geometry to device local memory via staging context
	auto & stagingContext = renderer->getStagingContext();

	auto buffersVec = stagingContext->storeBufferDataCmd( transferSrc, mStaticAllocator );

	// Recieve buffers from storeBuffer operation, and keep them so we can attach them to draw commands

	flagVertices  = buffersVec[0];
	flagTexCoords = buffersVec[1];
	flagIndices   = buffersVec[2];

	flagPoleVertices = buffersVec[3];
	flagPoleNormals  = buffersVec[4];
	flagPoleIndices  = buffersVec[5];

}



//--------------------------------------------------------------

void ofApp::update(){
	
}

//--------------------------------------------------------------
void ofApp::draw(){

	// Vulkan uses a slightly different clip space than OpenGL - 
	// In Vulkan, z goes from 0..1, instead of OpenGL's -1..1
	// and y is flipped. 
	// We apply the clip matrix to the projectionMatrix to transform
	// from openFrameworks (GL-style) to Vulkan (Vulkan-style) clip space.
	static const glm::mat4x4 clip(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.5f, 0.0f,
		0.0f, 0.0f, 0.5f, 1.0f
	);

	auto viewMatrix       = mCam.getModelViewMatrix();
	auto projectionMatrix = clip * mCam.getProjectionMatrix( ofGetCurrentViewport() );
	auto modelMatrix      = glm::scale( glm::mat4x4(), { 1, 1, 1 } );

	auto & context = renderer->getDefaultContext();

	flagStripDraw
		.setUniform( "projectionMatrix", projectionMatrix )
		.setUniform( "viewMatrix", viewMatrix )
		.setUniform( "modelMatrix", modelMatrix )
		.setUniform( "globalColor", ofFloatColor::white )
		.setUniform( "timeInterval", fmodf( ofGetElapsedTimef(), 3.f ) )
		;

	lambertDraw
		.setUniform( "projectionMatrix", projectionMatrix )
		.setUniform( "viewMatrix", viewMatrix )
		.setUniform( "modelMatrix", glm::translate( modelMatrix, {0,-200,0}) )
		.setUniform( "globalColor", ofFloatColor::white )
		.setAttribute(0,flagPoleVertices)
		.setAttribute(1,flagPoleNormals)
		.setIndices(flagPoleIndices)
		.setDrawMethod(of::vk::DrawCommand::DrawMethod::eIndexed )
		.setNumIndices(flagPoleIndices.numElements)
		;

	// Setup the main pass RenderBatch
	of::vk::RenderBatch::Settings settings;
	settings
		.setContext( context.get() )
		.setFramebufferAttachmentsExtent( renderer->getSwapchain()->getWidth(), renderer->getSwapchain()->getHeight() )
		.setRenderAreaExtent(  uint32_t( renderer->getViewportWidth() ), uint32_t( renderer->getViewportHeight() )  )
		.setRenderPass( *renderer->getDefaultRenderpass() )
		.addFramebufferAttachment( context->getSwapchainImageView() )
		.addClearColorValue( ofFloatColor::white )
		.addFramebufferAttachment( renderer->getDepthStencilImageView() )
		.addClearDepthStencilValue({1.f,0})
		;

	of::vk::RenderBatch batch{ settings };
	{
		// Beginning a batch allocates a new command buffer in its context
		// and begins a RenderPass
		batch.begin();
		batch.draw( backGroundDraw );
		batch.draw( lambertDraw );
		batch.draw( flagStripDraw );
		// Ending a batch accumulates all draw commands into a command buffer
		// and finalizes the command buffer.
		batch.end();
	}

}

//--------------------------------------------------------------
void ofApp::keyPressed(int key){

}

//--------------------------------------------------------------
void ofApp::keyReleased(int key){
	if ( key == ' ' ){
		// Recompile all shaders on spacebar press
		if ( mFlagShader ){
			mFlagShader->compile();
		}
		if ( mBgShader ){
			mBgShader->compile();
		}
		if ( mLambertShader ){
			mLambertShader->compile();
		}
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
