#include "ofParameterGroup.h"
#include "ofUtils.h"
#include "ofParameter.h"

ofParameterGroup::ofParameterGroup()
:obj(new Value)
{

}

ofParameterGroup::~ofParameterGroup(){
	for(auto & p: obj->parameters){
		p->setParent(NULL);
	}
}

ofParameterGroup::ofParameterGroup(const ofParameterGroup& mom){
	// copy object
	obj = mom.obj;

	// correct parent of parameters
	for(auto & p: obj->parameters){
		p->setParent(this);
	}
}

ofParameterGroup & ofParameterGroup::operator=(const ofParameterGroup& mom){

	// copy object
	obj = mom.obj;

	// correct parent of parameters
	for(auto & p: obj->parameters){
		p->setParent(this);
	}

	return *this;
}

void ofParameterGroup::add(ofAbstractParameter & parameter){
	std::shared_ptr<ofAbstractParameter> param = parameter.newReference();
	obj->parameters.push_back(param);
	obj->parametersIndex[param->getEscapedName()] = obj->parameters.size()-1;
	param->setParent(this);
}

void ofParameterGroup::clear(){
	obj->parameters.clear();
	obj->parametersIndex.clear();
}

ofParameter<bool> ofParameterGroup::getBool(std::string name) const	{
	return get<bool>(name);
}

ofParameter<int> ofParameterGroup::getInt(std::string name) const{
	return get<int>(name);
}

ofParameter<float> ofParameterGroup::getFloat(std::string name) const{
	return get<float>(name);
}

ofParameter<char> ofParameterGroup::getChar(std::string name) const{
	return get<char>(name);
}

ofParameter<std::string> ofParameterGroup::getString(std::string name) const{
	return get<std::string>(name);
}

ofParameter<ofPoint> ofParameterGroup::getPoint(std::string name) const{
	return get<ofPoint>(name);
}

ofParameter<ofVec2f> ofParameterGroup::getVec2f(std::string name) const{
	return get<ofVec2f>(name);
}

ofParameter<ofVec3f> ofParameterGroup::getVec3f(std::string name) const{
	return get<ofVec3f>(name);
}

ofParameter<ofVec4f> ofParameterGroup::getVec4f(std::string name) const{
	return get<ofVec4f>(name);
}

ofParameter<ofColor> ofParameterGroup::getColor(std::string name) const{
	return get<ofColor>(name);
}

ofParameter<ofShortColor> ofParameterGroup::getShortColor(std::string name) const{
	return get<ofShortColor>(name);
}

ofParameter<ofFloatColor> ofParameterGroup::getFloatColor(std::string name) const{
	return get<ofFloatColor>(name);
}

ofParameterGroup ofParameterGroup::getGroup(std::string name) const{
	return static_cast<ofParameterGroup& >(get(name));
}

ofParameter<bool> ofParameterGroup::getBool(int pos) const{
	return get<bool>(pos);
}

ofParameter<int> ofParameterGroup::getInt(int pos) const{
	return get<int>(pos);
}

ofParameter<float> ofParameterGroup::getFloat(int pos) const{
	return get<float>(pos);
}

ofParameter<char> ofParameterGroup::getChar(int pos) const{
	return get<char>(pos);
}

ofParameter<std::string> ofParameterGroup::getString(int pos) const{
	return get<std::string>(pos);
}

ofParameter<ofPoint> ofParameterGroup::getPoint(int pos)	 const{
	return get<ofPoint>(pos);
}

ofParameter<ofVec2f> ofParameterGroup::getVec2f(int pos) const{
	return get<ofVec2f>(pos);
}

ofParameter<ofVec3f> ofParameterGroup::getVec3f(int pos) const{
	return get<ofVec3f>(pos);
}

ofParameter<ofVec4f> ofParameterGroup::getVec4f(int pos) const{
	return get<ofVec4f>(pos);
}

ofParameter<ofColor> ofParameterGroup::getColor(int pos) const{
	return get<ofColor>(pos);
}

ofParameter<ofShortColor> ofParameterGroup::getShortColor(int pos) const{
	return get<ofShortColor>(pos);
}

ofParameter<ofFloatColor> ofParameterGroup::getFloatColor(int pos) const{
	return get<ofFloatColor>(pos);
}


ofParameterGroup ofParameterGroup::getGroup(int pos) const{
	if(pos>=size()){
		return ofParameterGroup();
	}else{
		if(getType(pos)==typeid(ofParameterGroup).name()){
			return *static_cast<ofParameterGroup* >(obj->parameters[pos].get());
		}else{
			ofLogError("ofParameterGroup") << "get(): bad type for pos " << pos << ", returning empty group";
			return ofParameterGroup();
		}
	}
}


int ofParameterGroup::size() const{
	return obj->parameters.size();
}

std::string ofParameterGroup::getName(int position) const{
	if(position>=size()){
		return "";
	}else{
		return obj->parameters[position]->getName();
	}
}

std::string ofParameterGroup::getType(int position) const{
	if(position>=size()) return "";
	else return obj->parameters[position]->type();
}


int ofParameterGroup::getPosition(std::string name) const{
	if(obj->parametersIndex.find(escape(name))!=obj->parametersIndex.end())
		return obj->parametersIndex.find(escape(name))->second;
	return -1;
}

std::string ofParameterGroup::getName() const{
	return obj->name;
}

void ofParameterGroup::setName(std::string _name){
	obj->name = _name;
}

std::string ofParameterGroup::getEscapedName() const{
	if(getName()==""){
		return "group";
	}else{
		return ofAbstractParameter::getEscapedName();
	}
}

std::string ofParameterGroup::toString() const{
	std::stringstream out;
	out << *this;
	return out.str();
}


ofAbstractParameter & ofParameterGroup::get(std::string name) const{
	std::map<std::string,int>::const_iterator it = obj->parametersIndex.find(escape(name));
	int index = it->second;
	return get(index);
}

ofAbstractParameter & ofParameterGroup::get(int pos) const{
	return *obj->parameters[pos];
}


ofAbstractParameter & ofParameterGroup::operator[](std::string name) const{
	return get(name);
}

ofAbstractParameter & ofParameterGroup::operator[](int pos) const{
	return get(pos);
}

std::ostream& operator<<(std::ostream& os, const ofParameterGroup& group) {
	std::streamsize width = os.width();
	for(int i=0;i<group.size();i++){
		if(group.getType(i)==typeid(ofParameterGroup).name()){
			os << group.getName(i) << ":" << std::endl;
			os << std::setw(width+4);
			os << group.getGroup(i);
		}else{
			os << group.getName(i) << ":" << group.get(i) << std::endl;
			os << std::setw(width);
		}
	}
	return os;
}

bool ofParameterGroup::contains(std::string name){
	return obj->parametersIndex.find(escape(name))!=obj->parametersIndex.end();
}

void ofParameterGroup::notifyParameterChanged(ofAbstractParameter & param){
	ofNotifyEvent(parameterChangedE,param);
	if(getParent()) getParent()->notifyParameterChanged(param);
}

ofAbstractParameter & ofParameterGroup::back(){
	return *obj->parameters.back();
}

ofAbstractParameter & ofParameterGroup::front(){
	return *obj->parameters.front();
}

const ofAbstractParameter & ofParameterGroup::back() const{
	return *obj->parameters.back();
}

const ofAbstractParameter & ofParameterGroup::front() const{
	return *obj->parameters.front();
}

void ofParameterGroup::setSerializable(bool _serializable){
	obj->serializable = _serializable;
}

bool ofParameterGroup::isSerializable() const{
	return obj->serializable;
}

std::shared_ptr<ofAbstractParameter> ofParameterGroup::newReference() const{
	return std::shared_ptr<ofAbstractParameter>(new ofParameterGroup(*this));
}

void ofParameterGroup::setParent(ofParameterGroup * _parent){
	obj->parent = _parent;
}

const ofParameterGroup * ofParameterGroup::getParent() const{
	return obj->parent;
}

ofParameterGroup * ofParameterGroup::getParent(){
	return obj->parent;
}

std::vector<std::shared_ptr<ofAbstractParameter> >::iterator ofParameterGroup::begin(){
	return obj->parameters.begin();
}

std::vector<std::shared_ptr<ofAbstractParameter> >::iterator ofParameterGroup::end(){
	return obj->parameters.end();
}

std::vector<std::shared_ptr<ofAbstractParameter> >::const_iterator ofParameterGroup::begin() const{
	return obj->parameters.begin();
}

std::vector<std::shared_ptr<ofAbstractParameter> >::const_iterator ofParameterGroup::end() const{
	return obj->parameters.end();
}

std::vector<std::shared_ptr<ofAbstractParameter> >::reverse_iterator ofParameterGroup::rbegin(){
	return obj->parameters.rbegin();
}

std::vector<std::shared_ptr<ofAbstractParameter> >::reverse_iterator ofParameterGroup::rend(){
	return obj->parameters.rend();
}

std::vector<std::shared_ptr<ofAbstractParameter> >::const_reverse_iterator ofParameterGroup::rbegin() const{
	return obj->parameters.rbegin();
}

std::vector<std::shared_ptr<ofAbstractParameter> >::const_reverse_iterator ofParameterGroup::rend() const{
	return obj->parameters.rend();
}

