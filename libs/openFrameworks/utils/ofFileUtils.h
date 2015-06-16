#pragma once

#include "ofConstants.h"
#if _MSC_VER
//#define BOOST_NO_CXX11_SCOPED_ENUMS
#include <boost/filesystem.hpp>
namespace std {
	namespace filesystem = boost::filesystem;
}
#else
#define BOOST_NO_CXX11_SCOPED_ENUMS
#include <boost/filesystem.hpp>
namespace std{
	namespace filesystem = boost::filesystem;
}
#endif
//----------------------------------------------------------
// ofBuffer
//----------------------------------------------------------

class ofBuffer{
	
public:
	ofBuffer();
	ofBuffer(const char * buffer, unsigned int size);
	ofBuffer(const std::string & text);
	ofBuffer(std::istream & stream);

	void set(const char * _buffer, unsigned int _size);
	void set(const std::string & text);
	bool set(std::istream & stream);
	void append(const std::string& _buffer);
	void append(const char * _buffer, unsigned int _size);

	bool writeTo(std::ostream & stream) const;

	void clear();

	void allocate(long _size);

	char * getData();
	const char * getData() const;
	OF_DEPRECATED_MSG("Use getData instead",char * getBinaryBuffer());
	OF_DEPRECATED_MSG("Use getData instead",const char * getBinaryBuffer() const);

	std::string getText() const;
	operator std::string() const;  // cast to string, to use a buffer as a string
	ofBuffer & operator=(const std::string & text);

	long size() const;
	static void setIOBufferSize(size_t ioSize);

	OF_DEPRECATED_MSG("use a lines iterator instead", std::string getNextLine());
	OF_DEPRECATED_MSG("use a lines iterator instead", std::string getFirstLine());
	OF_DEPRECATED_MSG("use a lines iterator instead",bool isLastLine());
	OF_DEPRECATED_MSG("use a lines iterator instead",void resetLineReader());
    
	friend std::ostream & operator<<(std::ostream & ostr, const ofBuffer & buf);
	friend std::istream & operator>>(std::istream & istr, ofBuffer & buf);

	std::vector<char>::iterator begin();
	std::vector<char>::iterator end();
	std::vector<char>::const_iterator begin() const;
	std::vector<char>::const_iterator end() const;
	std::vector<char>::reverse_iterator rbegin();
	std::vector<char>::reverse_iterator rend();
	std::vector<char>::const_reverse_iterator rbegin() const;
	std::vector<char>::const_reverse_iterator rend() const;

	struct Line: public std::iterator<std::forward_iterator_tag,Line>{
		Line(std::vector<char>::iterator _begin, std::vector<char>::iterator _end);
        const std::string & operator*() const;
        const std::string * operator->() const;
        const std::string & asString() const;
        Line& operator++();
        Line operator++(int);
        bool operator!=(Line const& rhs) const;
        bool operator==(Line const& rhs) const;
        bool empty() const;

	private:
        std::string line;
        std::vector<char>::iterator _current, _begin, _end;
	};

	struct Lines{
		Lines(std::vector<char> & buffer);
        Line begin();
        Line end();

	private:
        std::vector<char>::iterator _begin, _end;
	};

	Lines getLines();

private:
	std::vector<char> 	buffer;
	Line			currentLine;
	static size_t	ioSize;
};

//--------------------------------------------------
ofBuffer ofBufferFromFile(const std::string & path, bool binary=false);

//--------------------------------------------------
bool ofBufferToFile(const std::string & path, ofBuffer & buffer, bool binary=false);


//--------------------------------------------------
class ofFilePath{
public:
		
	static std::string getFileExt(std::string filename);
	static std::string removeExt(std::string filename);
	static std::string addLeadingSlash(std::string path);
	static std::string addTrailingSlash(std::string path);
	static std::string removeTrailingSlash(std::string path);
	static std::string getPathForDirectory(std::string path);
	static std::string getAbsolutePath(std::string path, bool bRelativeToData = true);

	static bool isAbsolute(std::string path);
	
	static std::string getFileName(std::string filePath, bool bRelativeToData = true);
	static std::string getBaseName(std::string filePath); // filename without extension

	static std::string getEnclosingDirectory(std::string filePath, bool bRelativeToData = true);
	static bool createEnclosingDirectory(std::string filePath, bool bRelativeToData = true, bool bRecursive = true);
	static std::string getCurrentWorkingDirectory();
	static std::string join(std::string path1, std::string path2);
	
	static std::string getCurrentExePath();
	static std::string getCurrentExeDir();

	static std::string getUserHomeDir();
};

class ofFile: public std::fstream{

public:
	
	enum Mode{
		Reference,
		ReadOnly,
		WriteOnly,
		ReadWrite,
		Append
	};

	ofFile();
	ofFile(const std::filesystem::path & path, Mode mode=ReadOnly, bool binary=true);
	ofFile(const ofFile & mom);
	ofFile & operator= (const ofFile & mom);
	~ofFile();

	bool open(const std::filesystem::path & path, Mode mode=ReadOnly, bool binary=false);
	bool changeMode(Mode mode, bool binary=false); // reopens a file to the same path with a different mode;
	void close();
	bool create();
	
	bool exists() const;
	std::string path() const;
	
	std::string getExtension() const;
	std::string getFileName() const;
	std::string getBaseName() const; // filename without extension
	std::string getEnclosingDirectory() const;
	std::string getAbsolutePath() const;

	bool canRead() const;
	bool canWrite() const;
	bool canExecute() const;

	bool isFile() const;
	bool isLink() const;
	bool isDirectory() const;
	bool isDevice() const;
	bool isHidden() const;

	void setWriteable(bool writeable=true);
	void setReadOnly(bool readable=true);
	void setExecutable(bool executable=true);
	
	//these all work for files and directories
	bool copyTo(std::string path, bool bRelativeToData = true, bool overwrite = false);
	bool moveTo(std::string path, bool bRelativeToData = true, bool overwrite = false);
	bool renameTo(std::string path, bool bRelativeToData = true, bool overwrite = false);
	
	
	//be careful! this deletes a file or folder :) 
	bool remove(bool recursive=false);

	uint64_t getSize() const;

	//this allows to compare files by their paths, also provides sorting and use as key in stl containers
	bool operator==(const ofFile & file) const;
	bool operator!=(const ofFile & file) const;
	bool operator<(const ofFile & file) const;
	bool operator<=(const ofFile & file) const;
	bool operator>(const ofFile & file) const;
	bool operator>=(const ofFile & file) const;


	//------------------
	// stream operations
	//------------------

	// since this class inherits from fstream it can be used as a r/w stream:
	// http://www.cplusplus.com/reference/iostream/fstream/


	//helper functions to read/write a whole file to/from an ofBuffer
	ofBuffer readToBuffer();
	bool writeFromBuffer(const ofBuffer & buffer);

	
	// this can be used to read the whole stream into an output stream. ie:
	// it's equivalent to rdbuf() just here to make it easier to use
	// ofLogNotice() << file.getFileBuffer();
	// write_file << file.getFileBuffer();
	std::filebuf * getFileBuffer() const;
	
	
	//-------
	//static helpers
	//-------

	static bool copyFromTo(std::string pathSrc, std::string pathDst, bool bRelativeToData = true,  bool overwrite = false);

	//be careful with slashes here - appending a slash when moving a folder will causes mad headaches in osx
	static bool moveFromTo(std::string pathSrc, std::string pathDst, bool bRelativeToData = true, bool overwrite = false);
	static bool doesFileExist(std::string fPath,  bool bRelativeToData = true);
	static bool removeFile(std::string path, bool bRelativeToData = true);

private:
	bool isWriteMode();
	bool openStream(Mode _mode, bool binary);
	void copyFrom(const ofFile & mom);
	std::filesystem::path myFile;
	Mode mode;
	bool binary;
};

class ofDirectory{

public:
	ofDirectory();
	ofDirectory(const std::filesystem::path & path);

	void open(const std::filesystem::path & path);
	void close();
	bool create(bool recursive = false);

	bool exists() const;
	std::string path() const;
	std::string getAbsolutePath() const;

	bool canRead() const;
	bool canWrite() const;
	bool canExecute() const;
	
	bool isDirectory() const;
	bool isHidden() const;

	void setWriteable(bool writeable=true);
	void setReadOnly(bool readable=true);
	void setExecutable(bool executable=true);
	void setShowHidden(bool showHidden);

	bool copyTo(std::string path, bool bRelativeToData = true, bool overwrite = false);
	bool moveTo(std::string path, bool bRelativeToData = true, bool overwrite = false);
	bool renameTo(std::string path, bool bRelativeToData = true, bool overwrite = false);

	//be careful! this deletes a file or folder :)
	bool remove(bool recursive);

	//-------------------
	// dirList operations
	//-------------------
	void allowExt(std::string extension);
	int listDir(std::string path);
	int listDir();

	std::string getOriginalDirectory();
	std::string getName(unsigned int position); // e.g., "image.png"
	std::string getPath(unsigned int position);
	ofFile getFile(unsigned int position, ofFile::Mode mode=ofFile::Reference, bool binary=false);
	const std::vector<ofFile> & getFiles() const;

	ofFile operator[](unsigned int position);

	bool getShowHidden();

	void reset(); //equivalent to close, just here for bw compatibility with ofxDirList
	void sort();

	unsigned int size();
	int numFiles(); // numFiles is deprecated, use size()

	//this allows to compare dirs by their paths, also provides sorting and use as key in stl containers
	bool operator==(const ofDirectory & dir);
	bool operator!=(const ofDirectory & dir);
	bool operator<(const ofDirectory & dir);
	bool operator<=(const ofDirectory & dir);
	bool operator>(const ofDirectory & dir);
	bool operator>=(const ofDirectory & dir);


	//-------
	//static helpers
	//-------

	static bool createDirectory(std::string dirPath, bool bRelativeToData = true, bool recursive = false);
	static bool isDirectoryEmpty(std::string dirPath, bool bRelativeToData = true );
	static bool doesDirectoryExist(std::string dirPath, bool bRelativeToData = true);
	static bool removeDirectory(std::string path, bool deleteIfNotEmpty,  bool bRelativeToData = true);

	std::vector<ofFile>::const_iterator begin() const;
	std::vector<ofFile>::const_iterator end() const;
	std::vector<ofFile>::const_reverse_iterator rbegin() const;
	std::vector<ofFile>::const_reverse_iterator rend() const;

private:
	std::filesystem::path myDir;
	std::string originalDirectory;
	std::vector <std::string> extensions;
	std::vector <ofFile> files;
	bool showHidden;

};

