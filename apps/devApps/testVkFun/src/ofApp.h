#pragma once

#include "ofMain.h"
#include "vk/ofVkRenderer.h"
#include "vk/DrawCommand.h"
#include "vk/RenderBatch.h"



class Teapot
{
	std::unique_ptr<of::DrawCommand> dc;

public:
	void setup();
	void update();
	void draw(of::RenderBatch & rp);

};



class ofApp : public ofBaseApp{


	Teapot mTeapot;

	public:
		void setup();
		void update();
		void draw();

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
