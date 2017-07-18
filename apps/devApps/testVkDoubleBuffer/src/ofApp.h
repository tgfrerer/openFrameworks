#pragma once

#include "ofMain.h"
#include "vk/DrawCommand.h"
#include "vk/ImageAllocator.h"
#include "vk/HelperTypes.h"
#include "vk/Context.h"

class ofApp : public ofBaseApp{

	const of::vk::DrawCommand fullscreenQuad;
	const of::vk::DrawCommand outlinesDraw;
	const of::vk::DrawCommand drawTextured;

	of::vk::ImageAllocator mImageAllocator;

	std::shared_ptr<of::vk::Shader> mShaderFullscreen;

	struct ImageWithView
	{
		::vk::Image     image;
		::vk::ImageView view;
	};

	std::array<ImageWithView, 2> mTargetImages;
	std::array<of::vk::Texture, 2> mTexture;

	ofEasyCam  mCam;
	ofCamera   mCamPrepass;

	std::shared_ptr<ofMesh> mMeshIco;
	std::shared_ptr<ofMesh> mMeshPlane;
	std::shared_ptr<ofMesh> mMeshL;
	
	std::shared_ptr<::vk::RenderPass> mPrepassRenderPass;
	vk::Rect2D mPrepassRect; // dimensions for prepass render targets 

public:
		void setup();
		void update();
		void draw();
		void exit() override;

		void setupPrepass();
		void setupDrawCommands();

		void keyPressed(int key);
		void keyReleased(int key);
		void mouseMoved(int x, int y );
		void mouseDragged(int x, int y, int button);
		void mousePressed(int x, int y, int button);
		void mouseReleased(int x, int y, int button);
		void mouseEntered(int x, int y);
		void mouseExited(int x, int y);
		void windowResized(int w, int h);
		void dragEvent(ofDragInfo dragInfo);
		void gotMessage(ofMessage msg);
		
		void setupMeshL(){
			// Horizontally elongated "L___" shape
			mMeshL = make_shared<ofMesh>();
			vector<glm::vec3> vert{
				{ 0.f,0.f,0.f },
				{ 20.f,20.f,0.f },
				{ 0.f,100.f,0.f },
				{ 20.f,100.f,0.f },
				{ 200.f,0.f,0.f },
				{ 200.f,20.f,0.f }
			};

			vector<ofIndexType> idx{
				0, 1, 2,
				1, 3, 2,
				0, 4, 1,
				1, 4, 5,
			};

			vector<glm::vec3> norm( vert.size(), { 0, 0, 1.f } );

			mMeshL->addVertices( vert );
			mMeshL->addNormals( norm );
			mMeshL->addIndices( idx );
		}
};
