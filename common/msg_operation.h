#ifndef MSG_OPERATION_H_
#define MSG_OPERATION_H_

#include <string.h>

#include <vector>
#include <string>
#include <sstream>

#include "debug_operation.h"

bool is_printable(const char c){
	return (c >= 0x20) && (c <= 0x7e);
}

bool is_printable(const std::string text){
	for(char c : text){
		if(!is_printable(c))
			return false;
	}
	return true;
}

std::vector<char> text_to_binary(const std::string text){
	std::vector<char> binary(text.begin(), text.end());
	return binary;
}

std::string binary_to_text(const std::vector<char> binary){
	std::string text(binary.begin(), binary.end());
	return text;
}

int32_t binary_to_num32(const std::vector<char> binary){
	int32_t* p = (int32_t*)binary.data();
	return (*p);
}

std::vector<char> num32_to_binary(int32_t number){
	std::vector<char> ret;
	ret.resize(sizeof(int32_t));
	memcpy(ret.data(), &number, ret.size());
	return ret;
}

int64_t binary_to_num64(const std::vector<char> binary){
	int64_t* p = (int64_t*)binary.data();
	return (*p);
}

std::vector<char> num64_to_binary(int64_t number){
	std::vector<char> ret;
	ret.resize(sizeof(int64_t));
	memcpy(ret.data(), &number, ret.size());
	return ret;
}

// split a string by delimiter
std::vector<std::string> split(const std::string s, char delim) {
    std::vector<std::string> ret;
    std::stringstream ss;
    ss.str(s);

    std::string item;
    while (getline(ss, item, delim)) {
        ret.push_back(item);
    }
    return ret;
}

std::string replace(std::string str, std::string from, std::string to) {
	if(from.size() <= 0)
		return str;

  	size_t index = 0;
	while ((index = str.find(from, index)) != std::string::npos) {
		str.replace(index, from.length(), to);
		index += to.length();
	}
	return str;
}

std::string remove_leading_space(std::string line) {
	for (int i = 0; i < line.length(); i++) {
		if (line[i] != ' ') {
			std::string res = line.substr(i);
			return res;
		}
	}
	return "";
}

#endif
