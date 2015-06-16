#include "ofParameter.h"
#include "ofParameterGroup.h"

ofAbstractParameter::ofAbstractParameter(){

}

ofAbstractParameter::~ofAbstractParameter(){

}

std::string ofAbstractParameter::getName() const {
	return "";
}

void ofAbstractParameter::setName(std::string name) {

}

std::string ofAbstractParameter::getEscapedName() const{
	return escape(getName());
}


std::string ofAbstractParameter::escape(std::string str) const{
	ofStringReplace(str, " ", "_");
	ofStringReplace(str, "<", "_");
	ofStringReplace(str, ">", "_");
	ofStringReplace(str, "{", "_");
	ofStringReplace(str, "}", "_");
	ofStringReplace(str, "[", "_");
	ofStringReplace(str, "]", "_");
	ofStringReplace(str, ",", "_");
	ofStringReplace(str, "(", "_");
	ofStringReplace(str, ")", "_");
	ofStringReplace(str, "/", "_");
	ofStringReplace(str, "\\", "_");
	ofStringReplace(str, ".", "_");
	return str;
}

std::string ofAbstractParameter::toString() const {
	return "";
}

void ofAbstractParameter::fromString(std::string str) {

}

std::string ofAbstractParameter::type() const{
	return typeid(*this).name();
}

void ofAbstractParameter::setParent(ofParameterGroup * _parent){

}

const ofParameterGroup * ofAbstractParameter::getParent() const{
	return NULL;
}

ofParameterGroup * ofAbstractParameter::getParent(){
	return NULL;
}

std::vector<std::string> ofAbstractParameter::getGroupHierarchyNames() const{
	std::vector<std::string> hierarchy;
	if(getParent()){
		hierarchy = getParent()->getGroupHierarchyNames();
	}
	hierarchy.push_back(getEscapedName());
	return hierarchy;
}


void ofAbstractParameter::notifyParent(){
	if(getParent()) getParent()->notifyParameterChanged(*this);
}

void ofAbstractParameter::setSerializable(bool serializable){

}

bool ofAbstractParameter::isSerializable() const{
	return true;
}

std::shared_ptr<ofAbstractParameter> ofAbstractParameter::newReference() const{
	return std::shared_ptr<ofAbstractParameter>(new ofAbstractParameter(*this));
}

std::ostream& operator<<(std::ostream& os, const ofAbstractParameter& p){
	os << p.toString();
	return os;
}

std::istream& operator>>(std::istream& is, ofAbstractParameter& p){
	std::string str;
	is >> str;
	p.fromString(str);
	return is;
}
