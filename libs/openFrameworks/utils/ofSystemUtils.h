#pragma once
#include "ofConstants.h"

class ofFileDialogResult{
	public:
		ofFileDialogResult();
		
		//TODO: only 1 file for now
		std::string getName();
		std::string getPath();
		
		std::string filePath;
		std::string fileName;
		bool bSuccess;
};

void ofSystemAlertDialog(std::string errorMessage);
ofFileDialogResult ofSystemLoadDialog(std::string windowTitle="", bool bFolderSelection = false, std::string defaultPath="");
ofFileDialogResult ofSystemSaveDialog(std::string defaultName, std::string messageName);
std::string ofSystemTextBoxDialog(std::string question, std::string text="");
