#pragma once

#include "ofMain.h"
#include "vk/DrawCommand.h"
#include "vk/HelperTypes.h"
#include "vk/BufferAllocator.h"
#include "vk/ImageAllocator.h"
#include "vk/Texture.h"

class ofApp : public ofBaseApp
{

	of::vk::DrawCommand flagStripDraw;
	of::vk::DrawCommand backGroundDraw;
	of::vk::DrawCommand lambertDraw;

	std::shared_ptr<of::vk::Shader> mFlagShader;
	std::shared_ptr<of::vk::Shader> mBgShader;
	std::shared_ptr<of::vk::Shader> mLambertShader;


	ofEasyCam mCam;

	void setupStaticGeometry();
	void setupTextureData();
	void setupDrawCommands();

	std::unique_ptr<of::vk::BufferAllocator> mStaticAllocator;
	std::unique_ptr<of::vk::ImageAllocator> mImageAllocator;

	of::vk::BufferRegion flagVertices;
	of::vk::BufferRegion flagTexCoords;
	of::vk::BufferRegion flagIndices;

	of::vk::BufferRegion flagPoleVertices;
	of::vk::BufferRegion flagPoleNormals;
	of::vk::BufferRegion flagPoleIndices;

	std::shared_ptr<::vk::Image> mFlagImage;
	std::shared_ptr<of::vk::Texture> mFlagTexture;

public:
	void setup();
	void update();
	void draw();

	void keyPressed( int key );
	void keyReleased( int key );
	void mouseMoved( int x, int y );
	void mouseDragged( int x, int y, int button );
	void mousePressed( int x, int y, int button );
	void mouseReleased( int x, int y, int button );
	void mouseEntered( int x, int y );
	void mouseExited( int x, int y );
	void windowResized( int w, int h );
	void dragEvent( ofDragInfo dragInfo );
	void gotMessage( ofMessage msg );

};
