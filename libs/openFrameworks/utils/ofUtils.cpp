#include "ofUtils.h"
#include "ofImage.h"
#include "ofFileUtils.h"

#include <chrono>
#include <numeric>
#include <locale>
#include "utf8.h"
#include <network/uri.hpp>


#ifdef TARGET_WIN32
    #ifndef _MSC_VER
        #include <unistd.h> // this if for MINGW / _getcwd
	#include <sys/param.h> // for MAXPATHLEN
    #endif
#endif


#if defined(TARGET_OF_IOS) || defined(TARGET_OSX ) || defined(TARGET_LINUX) || defined(TARGET_EMSCRIPTEN)
	#include <sys/time.h>
#endif

#ifdef TARGET_OSX
	#ifndef TARGET_OF_IOS
		#include <mach-o/dyld.h>
		#include <sys/param.h> // for MAXPATHLEN
	#endif
	#include <mach/clock.h>
	#include <mach/mach.h>
#endif

#ifdef TARGET_WIN32
    #include <mmsystem.h>
	#ifdef _MSC_VER
		#include <direct.h>
	#endif

#endif

#ifdef TARGET_OF_IOS
#include "ofxiOSExtras.h"
#endif

#ifdef TARGET_ANDROID
#include "ofxAndroidUtils.h"
#endif

#ifndef MAXPATHLEN
	#define MAXPATHLEN 1024
#endif

static bool enableDataPath = true;
static uint64_t startTimeSeconds;   //  better at the first frame ?? (currently, there is some delay from static init, to running.
static uint64_t startTimeNanos;


//--------------------------------------
void ofGetMonotonicTime(uint64_t & seconds, uint64_t & nanoseconds){
#if (defined(TARGET_LINUX) && !defined(TARGET_RASPBERRY_PI)) || defined(TARGET_EMSCRIPTEN)
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	seconds = now.tv_sec;
	nanoseconds = now.tv_nsec;
#elif defined(TARGET_OSX)
	clock_serv_t cs;
	mach_timespec_t now;
	host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &cs);
	clock_get_time(cs, &now);
	mach_port_deallocate(mach_task_self(), cs);
	seconds = now.tv_sec;
	nanoseconds = now.tv_nsec;
#elif defined( TARGET_WIN32 )
	LARGE_INTEGER freq;
	LARGE_INTEGER counter;
	QueryPerformanceFrequency(&freq);
	QueryPerformanceCounter(&counter);
	seconds = counter.QuadPart/freq.QuadPart;
	nanoseconds = (counter.QuadPart % freq.QuadPart)*1000000000/freq.QuadPart;
#else
	struct timeval now;
	gettimeofday( &now, NULL );
	seconds = now.tv_sec;
	nanoseconds = now.tv_usec * 1000;
#endif
}


//--------------------------------------
uint64_t ofGetElapsedTimeMillis(){
    uint64_t seconds;
    uint64_t nanos;
	ofGetMonotonicTime(seconds,nanos);
	return (seconds - startTimeSeconds)*1000 + ((long long)(nanos - startTimeNanos))/1000000;
}

//--------------------------------------
uint64_t ofGetElapsedTimeMicros(){
    uint64_t seconds;
    uint64_t nanos;
	ofGetMonotonicTime(seconds,nanos);
	return (seconds - startTimeSeconds)*1000000 + ((long long)(nanos - startTimeNanos))/1000;
}

//--------------------------------------
float ofGetElapsedTimef(){
    uint64_t seconds;
    uint64_t nanos;
	ofGetMonotonicTime(seconds,nanos);
	return (seconds - startTimeSeconds) + ((long long)(nanos - startTimeNanos))/1000000000.;
}

//--------------------------------------
void ofResetElapsedTimeCounter(){
	ofGetMonotonicTime(startTimeSeconds,startTimeNanos);
}

//=======================================
// this is from freeglut, and used internally:
/* Platform-dependent time in milliseconds, as an unsigned 32-bit integer.
 * This value wraps every 49.7 days, but integer overflows cancel
 * when subtracting an initial start time, unless the total time exceeds
 * 32-bit, where the GLUT API return value is also overflowed.
 */
uint64_t ofGetSystemTime( ) {
	uint64_t seconds, nanoseconds;
	ofGetMonotonicTime(seconds,nanoseconds);
	return seconds * 1000 + nanoseconds / 1000000;
}

uint64_t ofGetSystemTimeMicros( ) {
    uint64_t seconds, nanoseconds;
	ofGetMonotonicTime(seconds,nanoseconds);
	return seconds * 1000000 + nanoseconds / 1000;
}

//--------------------------------------------------
unsigned int ofGetUnixTime(){
	return (unsigned int)time(NULL);
}


//--------------------------------------
void ofSleepMillis(int millis){
	#ifdef TARGET_WIN32
		Sleep(millis);
	#elif defined(TARGET_LINUX)
		timespec interval = {millis/1000, millis%1000*1000000};
		timespec rem = {0,0};
		clock_nanosleep(CLOCK_MONOTONIC,0,&interval,&rem);
	#elif !defined(TARGET_EMSCRIPTEN)
		usleep(millis * 1000);
	#endif
}

//default ofGetTimestampString returns in this format: 2011-01-15-18-29-35-299
//--------------------------------------------------
std::string ofGetTimestampString(){
	std::string timeFormat = "%Y-%m-%d-%H-%M-%S-%i";
	return ofGetTimestampString(timeFormat);
}

//specify the string format - eg: %Y-%m-%d-%H-%M-%S-%i ( 2011-01-15-18-29-35-299 )
//--------------------------------------------------
std::string ofGetTimestampString(const std::string& timestampFormat){
	std::stringstream str;
	auto now = std::chrono::system_clock::now();
	auto t = std::chrono::system_clock::to_time_t(now);    std::chrono::duration<double> s = now - std::chrono::system_clock::from_time_t(t);
    int ms = s.count() * 1000;
	auto tm = *std::localtime(&t);
	constexpr int bufsize = 256;
	char buf[bufsize];
	if (std::strftime(buf,bufsize,timestampFormat.c_str(),&tm) != 0){
		str << buf;
	}
	auto ret = str.str();
	ofStringReplace(ret,"%i",ofToString(ms));

    return ret;
}

//--------------------------------------------------
int ofGetSeconds(){
	time_t 	curr;
	tm 		local;
	time(&curr);
	local	=*(localtime(&curr));
	return local.tm_sec;
}

//--------------------------------------------------
int ofGetMinutes(){
	time_t 	curr;
	tm 		local;
	time(&curr);
	local	=*(localtime(&curr));
	return local.tm_min;
}

//--------------------------------------------------
int ofGetHours(){
	time_t 	curr;
	tm 		local;
	time(&curr);
	local	=*(localtime(&curr));
	return local.tm_hour;
}

//--------------------------------------------------
int ofGetYear(){
  time_t    curr;
  tm       local;
  time(&curr);
  local   =*(localtime(&curr));
  int year = local.tm_year + 1900;
  return year;
}

//--------------------------------------------------
int ofGetMonth(){
  time_t    curr;
  tm       local;
  time(&curr);
  local   =*(localtime(&curr));
  int month = local.tm_mon + 1;
  return month;
}

//--------------------------------------------------
int ofGetDay(){
  time_t    curr;
  tm       local;
  time(&curr);
  local   =*(localtime(&curr));
  return local.tm_mday;
}

//--------------------------------------------------
int ofGetWeekday(){
  time_t    curr;
  tm       local;
  time(&curr);
  local   =*(localtime(&curr));
  return local.tm_wday;
}

//--------------------------------------------------
void ofEnableDataPath(){
	enableDataPath = true;
}

//--------------------------------------------------
void ofDisableDataPath(){
	enableDataPath = false;
}

//--------------------------------------------------
std::string defaultDataPath(){
#if defined TARGET_OSX
	return std::string("../../../data/");
#elif defined TARGET_ANDROID
	return std::string("sdcard/");
#elif defined(TARGET_LINUX) || defined(TARGET_WIN32)
	return std::string(ofFilePath::join(ofFilePath::getCurrentExeDir(),  "data/"));
#else
	return std::string("data/");
#endif
}

//--------------------------------------------------
static std::filesystem::path & defaultWorkingDirectory(){
	static auto * defaultWorkingDirectory = new std::filesystem::path();
	return * defaultWorkingDirectory;
}

//--------------------------------------------------
static std::filesystem::path & dataPathRoot(){
	static auto * dataPathRoot = new std::filesystem::path(defaultDataPath());
	return *dataPathRoot;
}

//--------------------------------------------------
void ofSetWorkingDirectoryToDefault(){
#ifdef TARGET_OSX
	#ifndef TARGET_OF_IOS
		char path[MAXPATHLEN];
		uint32_t size = sizeof(path);
		
		if (_NSGetExecutablePath(path, &size) == 0){
			std::filesystem::path classPath(path);
			classPath = classPath.parent_path();
			chdir( classPath.native().c_str() );
		}else{
			ofLogFatalError("ofUtils") << "ofSetDataPathRoot(): path buffer too small, need size " << (unsigned int) size;
		}
	#endif
#endif
	defaultWorkingDirectory() = std::filesystem::absolute(std::filesystem::current_path());
}
	
//--------------------------------------------------
void ofSetDataPathRoot(const std::string& newRoot){
	dataPathRoot() = newRoot;
}

//--------------------------------------------------
std::string ofToDataPath(const std::string& path, bool makeAbsolute){
	if (!enableDataPath)
		return path;
	
	// if our Current Working Directory has changed (e.g. file open dialog)
#ifdef TARGET_WIN32
	if (defaultWorkingDirectory() != std::filesystem::current_path()) {
		// change our cwd back to where it was on app load
		int ret = chdir(defaultWorkingDirectory().string().c_str());
		if(ret==-1){
			ofLogWarning("ofUtils") << "ofToDataPath: error while trying to change back to default working directory " << defaultWorkingDirectory();
		}
	}
#endif
	// this could be performed here, or wherever we might think we accidentally change the cwd, e.g. after file dialogs on windows
	
	const auto  & dataPath = dataPathRoot();
	std::filesystem::path inputPath(path);
	std::filesystem::path outputPath;
	
	// if path is already absolute, just return it
	if (inputPath.is_absolute()) {
		return path;
	}
	
	// here we check whether path already refers to the data folder by looking for common elements
	// if the path begins with the full contents of dataPathRoot then the data path has already been added
	// we compare inputPath.toString() rather that the input var path to ensure common formatting against dataPath.toString()
	auto strippedDataPath = dataPath.string();
	// also, we strip the trailing slash from dataPath since `path` may be input as a file formatted path even if it is a folder (i.e. missing trailing slash)
	strippedDataPath = ofFilePath::removeTrailingSlash(strippedDataPath);
	
	if (inputPath.string().find(strippedDataPath) != 0) {
		// inputPath doesn't contain data path already, so we build the output path as the inputPath relative to the dataPath
		outputPath = dataPath / inputPath;
	} else {
		// inputPath already contains data path, so no need to change
		outputPath = inputPath;
	}
	
	// finally, if we do want an absolute path and we don't already have one
	if (makeAbsolute) {
		// then we return the absolute form of the path
		return std::filesystem::absolute(outputPath).string();
	} else {
		// or output the relative path
		return outputPath.string();
	}
}


//----------------------------------------
template<>
std::string ofFromString(const std::string& value){
	return value;
}

//----------------------------------------
template<>
const char * ofFromString(const std::string& value){
	return value.c_str();
}

//----------------------------------------
template <>
std::string ofToHex(const std::string& value) {
	std::ostringstream out;
	// how many bytes are in the string
	int numBytes = value.size();
	for(int i = 0; i < numBytes; i++) {
		// print each byte as a 2-character wide hex value
		out << std::setfill('0') << std::setw(2) << std::hex << (unsigned int) ((unsigned char)value[i]);
	}
	return out.str();
}

//----------------------------------------
std::string ofToHex(const char* value) {
	// this function is necessary if you want to print a string
	// using a syntax like ofToHex("test")
	return ofToHex((std::string) value);
}

//----------------------------------------
int ofToInt(const std::string& intString) {
	int x = 0;
	std::istringstream cur(intString);
	cur >> x;
	return x;
}

//----------------------------------------
int ofHexToInt(const std::string& intHexString) {
	int x = 0;
	std::istringstream cur(intHexString);
	cur >> std::hex >> x;
	return x;
}

//----------------------------------------
char ofHexToChar(const std::string& charHexString) {
	int x = 0;
	std::istringstream cur(charHexString);
	cur >> std::hex >> x;
	return (char) x;
}

//----------------------------------------
float ofHexToFloat(const std::string& floatHexString) {
	union intFloatUnion {
		int x;
		float f;
	} myUnion;
	myUnion.x = 0;
	std::istringstream cur(floatHexString);
	cur >> std::hex >> myUnion.x;
	return myUnion.f;
}

//----------------------------------------
std::string ofHexToString(const std::string& stringHexString) {
	std::stringstream out;
	std::stringstream stream(stringHexString);
	// a hex string has two characters per byte
	int numBytes = stringHexString.size() / 2;
	for(int i = 0; i < numBytes; i++) {
		std::string curByte;
		// grab two characters from the hex string
		stream >> std::setw(2) >> curByte;
		// prepare to parse the two characters
		std::stringstream curByteStream(curByte);
		int cur = 0;
		// parse the two characters as a hex-encoded int
		curByteStream >> std::hex >> cur;
		// add the int as a char to our output stream
		out << (char) cur;
	}
	return out.str();
}

//----------------------------------------
float ofToFloat(const std::string& floatString) {
	float x = 0;
	std::istringstream cur(floatString);
	cur >> x;
	return x;
}

//----------------------------------------
double ofToDouble(const std::string& doubleString) {
	double x = 0;
	std::istringstream cur(doubleString);
	cur >> x;
	return x;
}

//----------------------------------------
bool ofToBool(const std::string& boolString) {
	auto lower = ofToLower(boolString);
	if(lower == "true") {
		return true;
	}
	if(lower == "false") {
		return false;
	}
	bool x = false;
	std::istringstream cur(lower);
	cur >> x;
	return x;
}

//----------------------------------------
char ofToChar(const std::string& charString) {
	char x = '\0';
	std::istringstream cur(charString);
	cur >> x;
	return x;
}

//----------------------------------------
template <> std::string ofToBinary(const std::string& value) {
	std::stringstream out;
	int numBytes = value.size();
	for(int i = 0; i < numBytes; i++) {
		std::bitset<8> bitBuffer(value[i]);
		out << bitBuffer;
	}
	return out.str();
}

//----------------------------------------
std::string ofToBinary(const char* value) {
	// this function is necessary if you want to print a string
	// using a syntax like ofToBinary("test")
	return ofToBinary((std::string) value);
}

//----------------------------------------
int ofBinaryToInt(const std::string& value) {
	const int intSize = sizeof(int) * 8;
	std::bitset<intSize> binaryString(value);
	return (int) binaryString.to_ulong();
}

//----------------------------------------
char ofBinaryToChar(const std::string& value) {
	const int charSize = sizeof(char) * 8;
	std::bitset<charSize> binaryString(value);
	return (char) binaryString.to_ulong();
}

//----------------------------------------
float ofBinaryToFloat(const std::string& value) {
	const int floatSize = sizeof(float) * 8;
	std::bitset<floatSize> binaryString(value);
	union ulongFloatUnion {
			unsigned long result;
			float f;
	} myUFUnion;
	myUFUnion.result = binaryString.to_ulong();
	return myUFUnion.f;
}
//----------------------------------------
std::string ofBinaryToString(const std::string& value) {
	std::ostringstream out;
	std::stringstream stream(value);
	std::bitset<8> byteString;
	int numBytes = value.size() / 8;
	for(int i = 0; i < numBytes; i++) {
		stream >> byteString;
		out << (char) byteString.to_ulong();
	}
	return out.str();
}

//--------------------------------------------------
std::vector<std::string> ofSplitString(const std::string & source, const std::string & delimiter, bool ignoreEmpty, bool trim) {
	std::vector<std::string> result;
	if (delimiter.empty()) {
		result.push_back(source);
		return result;
	}
	std::string::const_iterator substart = source.begin(), subend;
	while (true) {
		subend = search(substart, source.end(), delimiter.begin(), delimiter.end());
		std::string sub(substart, subend);
		if(trim) {
			sub = ofTrim(sub);
		}
		if (!ignoreEmpty || !sub.empty()) {
			result.push_back(sub);
		}
		if (subend == source.end()) {
			break;
		}
		substart = subend + delimiter.size();
	}
	return result;
}

//--------------------------------------------------
std::string ofJoinString(const std::vector<std::string>& stringElements, const std::string & delimiter){
	if(stringElements.empty()){
		return "";
	}
	return std::accumulate(stringElements.cbegin() + 1, stringElements.cend(), stringElements[0],
		[delimiter](std::string a, std::string b) -> const char *{
			return (a + delimiter + b).c_str();
		}
	);
}

//--------------------------------------------------
void ofStringReplace(std::string& input, const std::string& searchStr, const std::string& replaceStr){
	auto pos = input.find(searchStr);
	while(pos != std::string::npos){
		input.replace(pos, searchStr.size(), replaceStr);
		pos += replaceStr.size();
		std::string nextfind(input.begin() + pos, input.end());
		auto nextpos = nextfind.find(searchStr);
		if(nextpos==std::string::npos){
			break;
		}
		pos += nextpos;
	}
}

//--------------------------------------------------
bool ofIsStringInString(const std::string& haystack, const std::string& needle){
    return haystack.find(needle) != std::string::npos;
}

//--------------------------------------------------
int ofStringTimesInString(const std::string& haystack, const std::string& needle){
	const size_t step = needle.size();

	size_t count(0);
	size_t pos(0) ;

	while( (pos=haystack.find(needle, pos)) != std::string::npos) {
		pos +=step;
		++count ;
	}

	return count;
}

//--------------------------------------------------
std::string ofToLower(const std::string & src, const std::string & locale){
	std::string dst;
	utf8::iterator<const char*> it(&src.front(), &src.front(), (&src.back())+1);
	utf8::iterator<const char*> end((&src.back())+1, &src.front(), (&src.back())+1);
	while(it!=end){
		auto next = *it++;
		ofAppendUTF8(dst, next);
	}
	return dst;
}

//--------------------------------------------------
std::string ofToUpper(const std::string & src, const std::string & locale){
	std::string dst;
	utf8::iterator<const char*> it(&src.front(), &src.front(), (&src.back())+1);
	utf8::iterator<const char*> end((&src.back())+1, &src.front(), (&src.back())+1);
	while(it!=end){
		auto next = *it++;
		ofAppendUTF8(dst, next);
	}
	return dst;
}

std::string ofTrimFront(const std::string & src){
	auto front = std::find_if_not(src.begin(),src.end(),[](int c){return std::isspace(c);});
	return std::string(front,src.end());
}

std::string ofTrimBack(const std::string & src){
	auto back = std::find_if_not(src.rbegin(),src.rend(),[](int c){return std::isspace(c);}).base();
	return std::string(src.begin(),back);
}

std::string ofTrim(const std::string & src){
	auto front = std::find_if_not(src.begin(),src.end(),[](int c){return std::isspace(c);});
	auto back = std::find_if_not(src.rbegin(),src.rend(),[](int c){return std::isspace(c);}).base();
	return (back<=front ? std::string() : std::string(front,back));
}

void ofAppendUTF8(std::string & str, int utf8){
	try{
		utf8::append(utf8, back_inserter(str));
	}catch(...){}
}

//--------------------------------------------------
std::string ofVAArgsToString(const char * format, ...){
	// variadic args to string:
	// http://www.codeproject.com/KB/string/string_format.aspx
	char aux_buffer[10000];
	std::string retStr("");
	if (NULL != format){

		va_list marker;

		// initialize variable arguments
		va_start(marker, format);

		// Get formatted string length adding one for NULL
		size_t len = vsprintf(aux_buffer, format, marker) + 1;

		// Reset variable arguments
		va_end(marker);

		if (len > 0)
		{
			va_list args;

			// initialize variable arguments
			va_start(args, format);

			// Create a char vector to hold the formatted string.
			std::vector<char> buffer(len, '\0');
			vsprintf(&buffer[0], format, args);
			retStr = &buffer[0];
			va_end(args);
		}

	}
	return retStr;
}

std::string ofVAArgsToString(const char * format, va_list args){
	// variadic args to string:
	// http://www.codeproject.com/KB/string/string_format.aspx
	char aux_buffer[10000];
	std::string retStr("");
	if (NULL != format){

		// Get formatted string length adding one for NULL
		vsprintf(aux_buffer, format, args);
		retStr = aux_buffer;

	}
	return retStr;
}

//--------------------------------------------------
void ofLaunchBrowser(const std::string& url, bool uriEncodeQuery){
    network::uri uri;
    
    try {
        if(uriEncodeQuery) {
        	auto pos_q = url.find("?");
        	if(pos_q!=std::string::npos){
				std::string encoded;
				network::uri::encode_query(url.begin() + pos_q + 1, url.end(), std::back_inserter(encoded));
				std::cout << encoded << std::endl;
				uri = network::uri_builder(network::uri(url.begin(), url.begin() + pos_q))
					.query(encoded)
					.uri();
        	}else{
                uri = network::uri(url);
        	}
        }else{
            uri = network::uri(url);
        }
    } catch(const std::exception& exc) {
        ofLogError("ofUtils") << "ofLaunchBrowser(): malformed url \"" << url << "\": " << exc.what();
        return;
    }
        
	// http://support.microsoft.com/kb/224816
	// make sure it is a properly formatted url:
	//   some platforms, like Android, require urls to start with lower-case http/https
    //   Poco::URI automatically converts the scheme to lower case
	if(uri.scheme() != boost::none && uri.scheme().get() != "http" && uri.scheme().get() != "https"){
		ofLogError("ofUtils") << "ofLaunchBrowser(): url does not begin with http:// or https://: \"" << uri.string() << "\"";
		return;
	}

	#ifdef TARGET_WIN32
		#if (_MSC_VER)
		// microsoft visual studio yaks about strings, wide chars, unicode, etc
		ShellExecuteA(NULL, "open", uri.string().c_str(),
                NULL, NULL, SW_SHOWNORMAL);
		#else
		ShellExecute(NULL, "open", uri.string().c_str(),
                NULL, NULL, SW_SHOWNORMAL);
		#endif
	#endif

	#ifdef TARGET_OSX
        // could also do with LSOpenCFURLRef
		string commandStr = "open \"" + uri.string() + "\"";
		int ret = system(commandStr.c_str());
        if(ret!=0) {
			ofLogError("ofUtils") << "ofLaunchBrowser(): couldn't open browser, commandStr \"" << commandStr << "\"";
		}
	#endif

	#ifdef TARGET_LINUX
        cout << uri.string() << endl;
		string commandStr = "xdg-open \"" + uri.string() + "\"";
		int ret = system(commandStr.c_str());
		if(ret!=0) {
			ofLogError("ofUtils") << "ofLaunchBrowser(): couldn't open browser, commandStr \"" << commandStr << "\"";
		}
	#endif

	#ifdef TARGET_OF_IOS
		ofxiOSLaunchBrowser(url);
	#endif

	#ifdef TARGET_ANDROID
		ofxAndroidLaunchBrowser(url);
	#endif
}

//--------------------------------------------------
std::string ofGetVersionInfo(){
	std::stringstream sstr;
	sstr << OF_VERSION_MAJOR << "." << OF_VERSION_MINOR << "." << OF_VERSION_PATCH;

	if (!std::string(OF_VERSION_PRE_RELEASE).empty())
	{
		sstr << "-" << OF_VERSION_PRE_RELEASE;
	}

	sstr << std::endl;
	return sstr.str();
}

unsigned int ofGetVersionMajor() {
	return OF_VERSION_MAJOR;
}

unsigned int ofGetVersionMinor() {
	return OF_VERSION_MINOR;
}

unsigned int ofGetVersionPatch() {
	return OF_VERSION_PATCH;
}

std::string ofGetVersionPreRelease() {
	return OF_VERSION_PRE_RELEASE;
}


//---- new to 006
//from the forums http://www.openframeworks.cc/forum/viewtopic.php?t=1413

//--------------------------------------------------
void ofSaveScreen(const std::string& filename) {
   /*ofImage screen;
   screen.allocate(ofGetWidth(), ofGetHeight(), OF_IMAGE_COLOR);
   screen.grabScreen(0, 0, ofGetWidth(), ofGetHeight());
   screen.save(filename);*/
	ofPixels pixels;
	ofGetGLRenderer()->saveFullViewport(pixels);
	ofSaveImage(pixels,filename);
}

//--------------------------------------------------
void ofSaveViewport(const std::string& filename) {
	// because ofSaveScreen doesn't related to viewports
	/*ofImage screen;
	ofRectangle view = ofGetCurrentViewport();
	screen.allocate(view.width, view.height, OF_IMAGE_COLOR);
	screen.grabScreen(0, 0, view.width, view.height);
	screen.save(filename);*/

	ofPixels pixels;
	ofGetGLRenderer()->saveFullViewport(pixels);
	ofSaveImage(pixels,filename);
}

//--------------------------------------------------
int saveImageCounter = 0;
void ofSaveFrame(bool bUseViewport){
	std::string fileName = ofToString(saveImageCounter) + ".png";
	if (bUseViewport){
		ofSaveViewport(fileName);
	} else {
		ofSaveScreen(fileName);
	}
	saveImageCounter++;
}

//--------------------------------------------------
std::string ofSystem(const std::string& command){
	FILE * ret = NULL;
#ifdef TARGET_WIN32
	ret = _popen(command.c_str(),"r");
#else 
	ret = popen(command.c_str(),"r");
#endif
	
	std::string strret;
	int c;

	if (ret == NULL){
		ofLogError("ofUtils") << "ofSystem(): error opening return file for command \"" << command  << "\"";
	}else{
		c = fgetc (ret);
		while (c != EOF) {
			strret += c;
			c = fgetc (ret);
		}
#ifdef TARGET_WIN32
		_pclose (ret);
#else
		pclose (ret);
#endif
	}

	return strret;
}

//--------------------------------------------------
ofTargetPlatform ofGetTargetPlatform(){
#ifdef TARGET_LINUX
	std::string arch = ofSystem("uname -m");
    if(ofIsStringInString(arch,"x86_64")) {
        return OF_TARGET_LINUX64;
    } else if(ofIsStringInString(arch,"armv6l")) {
        return OF_TARGET_LINUXARMV6L;
    } else if(ofIsStringInString(arch,"armv7l")) {
        return OF_TARGET_LINUXARMV7L;
    } else {
        return OF_TARGET_LINUX;
    }
#elif defined(TARGET_OSX)
    return OF_TARGET_OSX;
#elif defined(TARGET_WIN32)
    #if (_MSC_VER)
        return OF_TARGET_WINVS;
    #else
        return OF_TARGET_WINGCC;
    #endif
#elif defined(TARGET_ANDROID)
    return OF_TARGET_ANDROID;
#elif defined(TARGET_OF_IOS)
    return OF_TARGET_IOS;
#elif defined(TARGET_EMSCRIPTEN)
    return OF_TARGET_EMSCRIPTEN;
#endif
}
