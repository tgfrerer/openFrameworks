/*
 * ofxParameterGroup.h
 *
 *  Created on: 10/07/2012
 *      Author: arturo
 */

#ifndef OFXPARAMETERGROUP_H_
#define OFXPARAMETERGROUP_H_

#include <map>
#include "ofConstants.h"
#include "ofLog.h"
#include "ofParameter.h"
#include "ofTypes.h"

class ofParameterGroup: public ofAbstractParameter {
public:
	ofParameterGroup();
	~ofParameterGroup();

	template<typename ...Args>
	ofParameterGroup(const std::string & name, Args&... p)
	:obj(new Value){
		add(p...);
		setName(name);
	}

	template<typename ...Args>
	void add(ofAbstractParameter & p, Args&... parameters){
		add(p);
		add(parameters...);
	}

	ofParameterGroup(const ofParameterGroup& other);
	ofParameterGroup & operator=(const ofParameterGroup& other);

	void add(ofAbstractParameter & param);


	void clear();

	ofParameter<bool> getBool(std::string name) const;
	ofParameter<int> getInt(std::string name) const;
	ofParameter<float> getFloat(std::string name) const;
	ofParameter<char> getChar(std::string name) const;
	ofParameter<std::string> getString(std::string name)	 const;
	ofParameter<ofPoint> getPoint(std::string name)	 const;
	ofParameter<ofVec2f> getVec2f(std::string name) const;
	ofParameter<ofVec3f> getVec3f(std::string name) const;
	ofParameter<ofVec4f> getVec4f(std::string name) const;
	ofParameter<ofColor> getColor(std::string name) const;
	ofParameter<ofShortColor> getShortColor(std::string name) const;
	ofParameter<ofFloatColor> getFloatColor(std::string name) const;

	ofParameterGroup getGroup(std::string name) const;


	ofParameter<bool> getBool(int pos) const;
	ofParameter<int> getInt(int pos) const;
	ofParameter<float> getFloat(int pos) const;
	ofParameter<char> getChar(int pos) const;
	ofParameter<std::string> getString(int pos)	 const;
	ofParameter<ofPoint> getPoint(int pos)	 const;
	ofParameter<ofVec2f> getVec2f(int pos) const;
	ofParameter<ofVec3f> getVec3f(int pos) const;
	ofParameter<ofVec4f> getVec4f(int pos) const;
	ofParameter<ofColor> getColor(int pose) const;
	ofParameter<ofShortColor> getShortColor(int pos) const;
	ofParameter<ofFloatColor> getFloatColor(int pos) const;
	ofParameterGroup getGroup(int pos) const;

	ofAbstractParameter & get(std::string name) const;
	ofAbstractParameter & get(int pos) const;

	ofAbstractParameter & operator[](std::string name) const;
	ofAbstractParameter & operator[](int pos) const;

	template<typename ParameterType>
	ofParameter<ParameterType> get(std::string name) const;

	template<typename ParameterType>
	ofParameter<ParameterType> get(int pos) const;

	int size() const;
	std::string getName(int position) const;
	std::string getType(int position) const;
	int getPosition(std::string name) const;

	friend std::ostream& operator<<(std::ostream& os, const ofParameterGroup& group);

	std::string getName() const;
	void setName(std::string name);
	std::string getEscapedName() const;
	std::string toString() const;

	bool contains(std::string name);

	void notifyParameterChanged(ofAbstractParameter & param);

	ofEvent<ofAbstractParameter> parameterChangedE;

	ofAbstractParameter & back();
	ofAbstractParameter & front();
	const ofAbstractParameter & back() const;
	const ofAbstractParameter & front() const;

	void setSerializable(bool serializable);
	bool isSerializable() const;
	std::shared_ptr<ofAbstractParameter> newReference() const;

	void setParent(ofParameterGroup * _parent);
	const ofParameterGroup * getParent() const;
	ofParameterGroup * getParent();

	std::vector<std::shared_ptr<ofAbstractParameter> >::iterator begin();
	std::vector<std::shared_ptr<ofAbstractParameter> >::iterator end();
	std::vector<std::shared_ptr<ofAbstractParameter> >::const_iterator begin() const;
	std::vector<std::shared_ptr<ofAbstractParameter> >::const_iterator end() const;
	std::vector<std::shared_ptr<ofAbstractParameter> >::reverse_iterator rbegin();
	std::vector<std::shared_ptr<ofAbstractParameter> >::reverse_iterator rend();
	std::vector<std::shared_ptr<ofAbstractParameter> >::const_reverse_iterator rbegin() const;
	std::vector<std::shared_ptr<ofAbstractParameter> >::const_reverse_iterator rend() const;

private:
	class Value{
	public:
		Value()
		:serializable(true)
		,parent(NULL){}

		std::map<std::string,int> parametersIndex;
		std::vector<std::shared_ptr<ofAbstractParameter> > parameters;
		std::string name;
		bool serializable;
		ofParameterGroup * parent;
	};
	std::shared_ptr<Value> obj;
};

template<typename ParameterType>
ofParameter<ParameterType> ofParameterGroup::get(std::string name) const{
	return static_cast<ofParameter<ParameterType>& >(get(name));
}

template<typename ParameterType>
ofParameter<ParameterType> ofParameterGroup::get(int pos) const{
	return static_cast<ofParameter<ParameterType>& >(get(pos));
}
#endif /* OFXPARAMETERGROUP_H_ */
