#pragma once

#include "ofMain.h"
#include "vk/ofVkRenderer.h"
#include "vk/DrawCommand.h"
#include "vk/RenderBatch.h"


class ofApp : public ofBaseApp{

	const of::vk::DrawCommand dc;

	ofEasyCam mCam;

	std::shared_ptr<ofMesh> mMeshL;
	std::shared_ptr<ofMesh> mMeshTeapot;
	std::unique_ptr<of::vk::Allocator> mStaticAllocator;

	public:
		void setup();
		void setupStaticAllocator();
		void setupDrawCommand();
		void setupMeshL();
		void update();
		void draw();

		void uploadStaticAttributes( of::vk::RenderContext & currentContext );

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
		
};
