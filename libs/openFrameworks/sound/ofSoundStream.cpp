#include "ofSoundStream.h"
#include "ofAppRunner.h"

ofSoundStream soundStreamInput;
ofSoundStream soundStreamOutput;	

//------------------------------------------------------------
void ofSoundStreamSetup(int nOutputChannels, int nInputChannels, ofBaseApp * appPtr){
	if( appPtr == NULL ){
		appPtr = ofGetAppPtr();
	}
	ofSoundStreamSetup(nOutputChannels, nInputChannels, appPtr, 44100, 256, 4);
}

//------------------------------------------------------------
void ofSoundStreamSetup(int nOutputChannels, int nInputChannels, int sampleRate, int bufferSize, int nBuffers){
	ofSoundStreamSetup(nOutputChannels, nInputChannels, ofGetAppPtr(), sampleRate, bufferSize, nBuffers);
}

//------------------------------------------------------------
void ofSoundStreamSetup(int nOutputChannels, int nInputChannels, ofBaseApp * appPtr, int sampleRate, int bufferSize, int nBuffers){
	soundStreamOutput.setup(appPtr, nOutputChannels, nInputChannels, sampleRate, bufferSize, nBuffers);
}

//------------------------------------------------------------
void ofSoundStreamStop(){
	soundStreamOutput.stop();
	soundStreamInput.stop();
}

//------------------------------------------------------------
void ofSoundStreamStart(){
	soundStreamOutput.start();
	soundStreamInput.start();
}

//------------------------------------------------------------
void ofSoundStreamClose(){
	soundStreamOutput.close();
	soundStreamInput.close();
}

//------------------------------------------------------------
std::vector<ofSoundDevice> ofSoundStreamListDevices(){
	std::vector<ofSoundDevice> deviceList = soundStreamOutput.getDeviceList();
	ofLogNotice("ofSoundStreamListDevices") << std::endl << deviceList;
	return deviceList;
}

//------------------------------------------------------------
ofSoundStream::ofSoundStream(){
	#ifdef OF_SOUND_STREAM_TYPE
		setSoundStream(std::shared_ptr<OF_SOUND_STREAM_TYPE>(new OF_SOUND_STREAM_TYPE) );
	#endif
}

//------------------------------------------------------------
void ofSoundStream::setSoundStream(std::shared_ptr<ofBaseSoundStream> soundStreamPtr){
	soundStream = soundStreamPtr;
}

//------------------------------------------------------------
std::shared_ptr<ofBaseSoundStream> ofSoundStream::getSoundStream(){
	return soundStream;
}

//------------------------------------------------------------
std::vector<ofSoundDevice> ofSoundStream::getDeviceList() const{
	if( soundStream ){
		return soundStream->getDeviceList();
	} else {
		return std::vector<ofSoundDevice>();
	}
}

//------------------------------------------------------------
std::vector<ofSoundDevice> ofSoundStream::listDevices() const{
	std::vector<ofSoundDevice> deviceList = getDeviceList();
	ofLogNotice("ofSoundStream::listDevices") << std::endl << deviceList;
	return deviceList;
}

//------------------------------------------------------------
void ofSoundStream::printDeviceList()  const{
	if( soundStream ) {
		soundStream->printDeviceList();
	}
}

//------------------------------------------------------------
void ofSoundStream::setDeviceID(int deviceID){
	if( soundStream ){
		soundStream->setDeviceID(deviceID);
	}	
}

//------------------------------------------------------------
void ofSoundStream::setDevice(const ofSoundDevice &device) {
	setDeviceID(device.deviceID);
}

//------------------------------------------------------------
bool ofSoundStream::setup(ofBaseApp * app, int outChannels, int inChannels, int sampleRate, int bufferSize, int nBuffers){
	if( soundStream ){
		return soundStream->setup(app, outChannels, inChannels, sampleRate, bufferSize, nBuffers);
	}
	return false;
}

//------------------------------------------------------------
void ofSoundStream::setInput(ofBaseSoundInput * soundInput){
	if( soundStream ){
		soundStream->setInput(soundInput);
	}
}

//------------------------------------------------------------
void ofSoundStream::setInput(ofBaseSoundInput &soundInput){
	setInput(&soundInput);
}

//------------------------------------------------------------
void ofSoundStream::setOutput(ofBaseSoundOutput * soundOutput){
	if( soundStream ){
		soundStream->setOutput(soundOutput);
	}
}

//------------------------------------------------------------
void ofSoundStream::setOutput(ofBaseSoundOutput &soundOutput){
	setOutput(&soundOutput);
}

//------------------------------------------------------------
bool ofSoundStream::setup(int outChannels, int inChannels, int sampleRate, int bufferSize, int nBuffers){
	if( soundStream ){
		return soundStream->setup(outChannels, inChannels, sampleRate, bufferSize, nBuffers);
	}
	return false;
}

//------------------------------------------------------------
void ofSoundStream::start(){
	if( soundStream ){
		soundStream->start();
	}
}

//------------------------------------------------------------
void ofSoundStream::stop(){
	if( soundStream ){
		soundStream->stop();
	}
}

//------------------------------------------------------------
void ofSoundStream::close(){
	if( soundStream ){
		soundStream->close();
	}
}

//------------------------------------------------------------
long unsigned long ofSoundStream::getTickCount() const{
	if( soundStream ){
		return soundStream->getTickCount();
	}
	return 0;
}

//------------------------------------------------------------
int ofSoundStream::getNumInputChannels() const{
	if( soundStream ){
		return soundStream->getNumInputChannels();
	}
	return 0;
}

//------------------------------------------------------------
int ofSoundStream::getNumOutputChannels() const{
	if( soundStream ){
		return soundStream->getNumOutputChannels();
	}
	return 0;
}

//------------------------------------------------------------
int ofSoundStream::getSampleRate() const{
	if( soundStream ){
		return soundStream->getSampleRate();
	}
	return 0;
}

//------------------------------------------------------------
int ofSoundStream::getBufferSize() const{
	if( soundStream ){
		return soundStream->getBufferSize();
	}
	return 0;
}

//------------------------------------------------------------
ofSoundDevice::ofSoundDevice()
: name("Unknown")
, deviceID(0)
, inputChannels(0)
, outputChannels(0)
, isDefaultInput(false)
, isDefaultOutput(false) {

}

//------------------------------------------------------------
std::vector<ofSoundDevice> ofSoundStream::getMatchingDevices(const std::string& name, unsigned int inChannels, unsigned int outChannels) const {
	std::vector<ofSoundDevice> devs = getDeviceList();
	std::vector<ofSoundDevice> hits;
	
	for(size_t i = 0; i < devs.size(); i++) {
		bool nameMatch = devs[i].name.find(name) != std::string::npos;
		bool inMatch = (inChannels == UINT_MAX) || (devs[i].inputChannels == inChannels);
		bool outMatch = (outChannels == UINT_MAX) || (devs[i].outputChannels == outChannels);
		
		if(nameMatch && inMatch && outMatch) {
			hits.push_back(devs[i]);
		}
	}
	
	return hits;
}

//------------------------------------------------------------
std::ostream& operator << (std::ostream& os, const ofSoundDevice& dev) {
	os << "[" << dev.deviceID << "] " << dev.name;
	os << " [in:" << dev.inputChannels << " out:" << dev.outputChannels << "]";
	if(dev.isDefaultInput) os << " (default in)";
	if(dev.isDefaultOutput) os << " (default out)";
	return os;
}

//------------------------------------------------------------
std::ostream& operator << (std::ostream& os, const std::vector<ofSoundDevice>& devs) {
	for(std::size_t i = 0; i < devs.size(); i++) {
		os << devs[i] << std::endl;
	}
	return os;
}
