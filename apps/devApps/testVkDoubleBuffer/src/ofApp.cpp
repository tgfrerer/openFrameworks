#include "ofApp.h"
#include "ofVkRenderer.h"

// We keep a shared pointer to the renderer so we don't have to 
// fetch it anew every time we need it.
shared_ptr<ofVkRenderer> renderer;

//--------------------------------------------------------------

void ofApp::setup(){
	ofDisableSetupScreen();

	renderer = dynamic_pointer_cast<ofVkRenderer>( ofGetCurrentRenderer() );
	setupSecondaryRenderContext();
	setupDrawCommands();

	mMeshIco = std::make_shared<ofMesh>();
	*mMeshIco = ofBoxPrimitive(100,100,100,1,1,1).getMesh();

	mMeshIco->clearTexCoords();

	mMeshPlane = std::make_shared<ofMesh>( ofMesh::plane( 512, 256, 2, 2, OF_PRIMITIVE_TRIANGLES ) );

	setupMeshL();

}

//--------------------------------------------------------------

void ofApp::setupSecondaryRenderContext(){

	auto & device = renderer->getVkDevice();

	// setup a secondary context
	{

		auto rendererProperties = renderer->getVkRendererProperties();

		of::vk::Context::Settings contextSettings;

		contextSettings.transientMemoryAllocatorSettings.device = renderer->getVkDevice();
		contextSettings.transientMemoryAllocatorSettings.frameCount = 3;
		contextSettings.transientMemoryAllocatorSettings.physicalDeviceMemoryProperties = rendererProperties.physicalDeviceMemoryProperties;
		contextSettings.transientMemoryAllocatorSettings.physicalDeviceProperties = rendererProperties.physicalDeviceProperties;
		contextSettings.transientMemoryAllocatorSettings.size = ( ( 1ULL << 24 ) * renderer->mSettings.numVirtualFrames );
		contextSettings.renderer = renderer.get();
		contextSettings.pipelineCache = renderer->getPipelineCache();

		vk::Rect2D rect;
		rect.setExtent( { 512, 256 } );
		rect.setOffset( { 0, 0 } );

		contextSettings.renderArea = rect;

		{
			std::array<vk::AttachmentDescription, 1> attachments;

			attachments[0]		// color attachment
				.setFormat( ::vk::Format::eR8G8B8A8Unorm )
				.setSamples( vk::SampleCountFlagBits::e1 )
				.setLoadOp( vk::AttachmentLoadOp::eClear )
				.setStoreOp( vk::AttachmentStoreOp::eStore )
				.setStencilLoadOp( vk::AttachmentLoadOp::eDontCare )
				.setStencilStoreOp( vk::AttachmentStoreOp::eDontCare )
				.setInitialLayout( vk::ImageLayout::eUndefined)
				.setFinalLayout( vk::ImageLayout::eShaderReadOnlyOptimal )
				;

			// Define 2 attachments, and tell us what layout to expect these to be in.
			// Index references attachments from above.

			vk::AttachmentReference colorReference{ 0, vk::ImageLayout::eColorAttachmentOptimal };

			vk::SubpassDescription subpassDescription;
			subpassDescription
				.setPipelineBindPoint( vk::PipelineBindPoint::eGraphics )
				.setColorAttachmentCount( 1 )
				.setPColorAttachments( &colorReference )
				.setPDepthStencilAttachment( nullptr )
				;

			// Define 2 dependencies for subpass 0

			std::array<vk::SubpassDependency, 2> dependencies;
			dependencies[0]
				.setSrcSubpass( VK_SUBPASS_EXTERNAL ) // producer
				.setDstSubpass( 0 )                   // consumer
				.setSrcStageMask( vk::PipelineStageFlagBits::eBottomOfPipe )
				.setDstStageMask( vk::PipelineStageFlagBits::eColorAttachmentOutput )
				.setSrcAccessMask( vk::AccessFlagBits::eMemoryRead )
				.setDstAccessMask( vk::AccessFlagBits::eColorAttachmentWrite )
				.setDependencyFlags( vk::DependencyFlagBits::eByRegion )
				;
			dependencies[1]
				.setSrcSubpass( 0 ) // producer
				.setDstSubpass( VK_SUBPASS_EXTERNAL )                   // consumer
				.setSrcStageMask( vk::PipelineStageFlagBits::eColorAttachmentOutput )
				.setDstStageMask( vk::PipelineStageFlagBits::eTopOfPipe )
				.setSrcAccessMask( vk::AccessFlagBits::eColorAttachmentWrite )
				.setDstAccessMask( vk::AccessFlagBits::eMemoryRead )
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
			contextSettings.renderPass = renderPass;
			contextSettings.renderPassClearValues = { { reinterpret_cast<const ::vk::ClearColorValue&>( ofFloatColor::pink ) } };
		}

		mAuxContext = std::make_unique<of::vk::Context>( std::move( contextSettings ) );

		mAuxContext->setup();

		// ---- allocate imgages for context to render into

		size_t numImages = 2;

		of::vk::ImageAllocator::Settings allocatorSettings;
		allocatorSettings.device = device;
		allocatorSettings.imageTiling = vk::ImageTiling::eOptimal;
		allocatorSettings.imageUsageFlags = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
		allocatorSettings.memFlags = vk::MemoryPropertyFlagBits::eDeviceLocal;
		allocatorSettings.size = contextSettings.renderArea.extent.width * contextSettings.renderArea.extent.height * 4 * numImages;
		allocatorSettings.physicalDeviceMemoryProperties = renderer->getVkPhysicalDeviceMemoryProperties();
		allocatorSettings.physicalDeviceProperties = renderer->getVkPhysicalDeviceProperties();
		mImageAllocator = std::make_shared<of::vk::ImageAllocator>( allocatorSettings );

		mImageAllocator->setup();

		::vk::ImageCreateInfo imageCreateInfo;
		imageCreateInfo
			.setImageType( ::vk::ImageType::e2D )
			.setFormat( ::vk::Format::eR8G8B8A8Unorm )
			.setExtent( { contextSettings.renderArea.extent.width, contextSettings.renderArea.extent.height, 1 } )
			.setMipLevels( 1 )
			.setArrayLayers( 1 )
			.setSamples( ::vk::SampleCountFlagBits::e1 )
			.setTiling( ::vk::ImageTiling::eOptimal )
			.setUsage( vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled )
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
				.setSubresourceRange( { ::vk::ImageAspectFlagBits::eColor,0,1,0,1 } )
				;
			
			::vk::DeviceSize offset;
			mImageAllocator->allocate( contextSettings.renderArea.extent.width * contextSettings.renderArea.extent.height * 4, offset );
			device.bindImageMemory( img, mImageAllocator->getDeviceMemory(), offset );

			auto view = device.createImageView( imageViewCreateInfo );

			mTargetImages[i].image = img;
			mTargetImages[i].view = view;

			mTexture[i] = std::make_shared<of::vk::Texture>( device, img );
		}


		// ----

		/*

		This is how we make sure that contexts (which boil down to individual
		queue) are properly synchronised: We use semaphores.
		
		Whenever a context is submitted, there is a semaphore to wait for before
		work is being done on the device, and then there is a semaphore to signal
		when the work has been completed. 

		These semaphores get chained, so represent linear dependency.

		We probably don't need these semaphores, as there should be implicit 
		synchronisation based on the order of submissions to the queue, but 
		in case we're submitting to more than one queue, these semaphores are
		very important. You might want multiple queues when using compute.

		There is a minimum of two semaphores which we must use in case we're rendering
		to a KHR swapchain, as the swapchain will wait for renderComplete before presenting, 
		and will signal presentComplete when presentation for an image is complete. 

		/     ***** FENCE *****
		|     
		|     
		|     ===== submit to queue =====
		|     
		|     - presentComplete // wait    | submit A
		|     - renderA         // signal  | submit A
		|     
		|     - renderA         // wait    | submit B
		|     - renderB         // signal  | submit B
		|     
		|     
		|     - renderB         // wait    | submit default
		|     - renderComplete  // signal  | submit default
		|     
		|     ===== swapchain internal ======
		|     
		|     swapchain.present()          | swapchain present
		|     - renderComplete  // wait    |
		|     
		|     ------ the following will be wrapped around usually, i.e. acquire() 
		|            happens straight after the fence, before anything else.
		|     
		|     swapchain.acquire()          | swapchain acquire
		\     - presentComplete // signal  |


		*/

		/*auto & context = renderer->getDefaultContext();
		mAuxContext->addContextDependency( context.get() );
		context->addContextDependency( mAuxContext.get() );*/
	}

	mCamAux.setupPerspective( false, 60, 0.1, 500 );
	mCamAux.setGlobalPosition( 0, 0, mCam.getImagePlaneDistance( { 0,0,512,256 } ) );
	mCamAux.lookAt( { 0,0,0 }, { 0,1,0 } );

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

		const_cast<of::vk::DrawCommand&>( defaultDraw).setup( pipeline );
	}

	{
		of::vk::GraphicsPipelineState pipeline;
		pipeline.setShader( mShaderTextured );

		pipeline.rasterizationState.setCullMode( ::vk::CullModeFlagBits::eBack );
		pipeline.rasterizationState.setFrontFace( ::vk::FrontFace::eCounterClockwise );
		pipeline.depthStencilState
			.setDepthTestEnable( VK_TRUE )
			.setDepthWriteEnable( VK_TRUE )
			;
		pipeline.blendAttachmentStates[0].blendEnable = VK_TRUE;

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

	// sync: we have waited for the main context fence here. 

	static const glm::mat4x4 clip(
		1.0f, 0.0f, 0.0f, 0.0f,
		0.0f, -1.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.5f, 0.0f,
		0.0f, 0.0f, 0.5f, 1.0f
	);

	
	mAuxContext->begin();
	{
		mAuxContext->setupFrameBufferAttachments( { mTargetImages[0].view } );

		auto viewMatrix       = mCamAux.getModelViewMatrix();
		auto projectionMatrix = clip * mCamAux.getProjectionMatrix( { 0,0,512,256 } );

		ofMatrix4x4 modelMatrix = glm::rotate( float( TWO_PI * ( ( ofGetFrameNum() % 360 ) / 360.f ) ), glm::vec3( { 0.f, 1.f, 1.f } ) );

		of::vk::RenderBatch batch{ *mAuxContext };
		batch.begin();
		
		auto meshDraw = defaultDraw;

		ofFloatColor col = ofColor::white;
		col.lerp( ofColor::blue, 0.5 + 0.5 * sinf(TWO_PI * (ofGetFrameNum() % 360 ) / 360.f ));

		meshDraw
			.setUniform( "projectionMatrix", projectionMatrix )
			.setUniform( "viewMatrix", viewMatrix )
			.setUniform( "modelMatrix", modelMatrix )
			.setUniform( "globalColor", col )
			.setMesh(mMeshIco)
			;

		batch.draw( meshDraw );

		batch.end();
		
	}
	mAuxContext->end();

	// ---

	auto & context = renderer->getDefaultContext();
	
	{
		auto viewMatrix = mCam.getModelViewMatrix();
		auto projectionMatrix = clip * mCam.getProjectionMatrix( );

		auto texturedRect = drawTextured;
		texturedRect
			.setUniform( "projectionMatrix", projectionMatrix )
			.setUniform( "viewMatrix", viewMatrix )
			.setUniform( "modelMatrix", glm::mat4() )
			.setTexture( "tex_0", *mTexture[0] )
			.setMesh(mMeshPlane)
			;

		of::vk::RenderBatch batch{ *context };

		batch.begin();
		batch.draw( fullscreenQuad );
		batch.draw( texturedRect );  // draw result from previous render pass onto screen
		batch.end();

	}

	// check if this is a problem: submit happens after this, so the swap will 
	// shift the current frame before the frame has been recorded.

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
