#pragma once
#include "ofEvents.h"
#include "ofFileUtils.h"
#include "ofTypes.h"

class ofHttpRequest{
public:
	ofHttpRequest()
	:saveTo(false)
	,id(nextID++){};

	ofHttpRequest(std::string url, std::string name,bool saveTo=false)
	:url(url)
	,name(name)
	,saveTo(saveTo)
	,id(nextID++){}

	std::string				url;
	std::string				name;
	bool					saveTo;
	std::map<std::string, std::string>	headers;

	int getID(){return id;}
private:
	int					id;
	static int			nextID;
};

class ofHttpResponse{
public:
	ofHttpResponse()
	:status(0){}

	ofHttpResponse(ofHttpRequest request,const ofBuffer & data,int status, std::string error)
	:request(request)
	,data(data)
	,status(status)
	,error(error){}

	ofHttpResponse(ofHttpRequest request,int status, std::string error)
	:request(request)
	,status(status)
	,error(error){}

	operator ofBuffer&(){
		return data;
	}

	ofHttpRequest	    request;
	ofBuffer		    data;
	int					status;
	std::string			error;
};

ofHttpResponse ofLoadURL(std::string url);
int ofLoadURLAsync(std::string url, std::string name=""); // returns id
ofHttpResponse ofSaveURLTo(std::string url, std::string path);
int ofSaveURLAsync(std::string url, std::string path);
void ofRemoveURLRequest(int id);
void ofRemoveAllURLRequests();

void ofStopURLLoader();

ofEvent<ofHttpResponse> & ofURLResponseEvent();

template<class T>
void ofRegisterURLNotification(T * obj){
	ofAddListener(ofURLResponseEvent(),obj,&T::urlResponse);
}

template<class T>
void ofUnregisterURLNotification(T * obj){
	ofRemoveListener(ofURLResponseEvent(),obj,&T::urlResponse);
}

class ofBaseURLFileLoader;

class ofURLFileLoader  {
    public:
        ofURLFileLoader();	
        ofHttpResponse get(std::string url);
        int getAsync(std::string url, std::string name=""); // returns id
        ofHttpResponse saveTo(std::string url, std::string path);
        int saveAsync(std::string url, std::string path);
		void remove(int id);
		void clear();
        void stop();
        ofHttpResponse handleRequest(ofHttpRequest & request);

    private:
		std::shared_ptr<ofBaseURLFileLoader> impl;
};
