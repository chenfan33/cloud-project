#ifndef FILE_OPERATION_H_
#define FILE_OPERATION_H_

#include <sys/stat.h>

#include <fstream>
#include <iostream>

#include <vector>
#include <string>
#include <iterator>

#include "msg_operation.h"

bool read_file(const char* path, std::vector<char>& binary){
	std::ifstream file(path, std::ios::binary);
	if(!file.is_open()){
		return false;
	}

	file.seekg(0, file.end);
	size_t length = file.tellg();
	file.seekg(0, file.beg);

	binary.resize(length);
	file.read(binary.data(), length);

	file.close();
	return true;
}

bool read_file(std::string path, std::vector<char>& binary){
	return read_file(path.c_str(), binary);
}

bool read_file(const char* path, std::string& text){
	std::vector<char> binary;
	bool ret = read_file(path, binary);
	text = binary_to_text(binary);
	return ret;
}

bool read_file(std::string path, std::string& text){
	return read_file(path.c_str(), text);
}

bool write_file(const char* path, std::vector<char> binary){
	std::ofstream file(path, std::ios::binary);
	if(!file.is_open()){
		return false;
	}

	file.write(binary.data(), binary.size());
	file.close();
	return true;
}

bool write_file(const char* path, std::string text){
	return write_file(path, text_to_binary(text));
}

bool exist_file(const char* path){
	struct stat info;
	return (stat(path, &info) == 0);
}

bool move_file(const char* src, const char* dst){
	if(exist_file(src))
		return (std::rename(src, dst) == 0);
	else
		return false;
}

bool remove_file(const char* path){
	std::string com = "rm -rf " + std::string(path);
	return (system(com.c_str()) == 0);
}

bool create_dir(const char* path){
	return (mkdir(path, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0);
}

#endif
