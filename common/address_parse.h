#ifndef ADDRESS_PARSE_H_
#define ADDRESS_PARSE_H_

#include <arpa/inet.h>

#include <regex>
#include <string>

#include "debug_operation.h"

// Regex to match address (e.g., 127.0.0.1:5005)
static std::string address_pattern = "^([0-9]{1,3}\\.){3}[0-9]{1,3}:([0-9]{1,5})$";
static std::regex address_regex = std::regex(address_pattern, std::regex::icase);

// Check whether the string is an address
bool isAddress(std::string name){
	std::smatch match;
	return std::regex_match(name, match, address_regex);
}

struct Address{
	std::string name;

	std::string ip;
	int port;

	void init(std::string _name){
		name = _name;

		uint32_t pos = name.find(":");
		ip = name.substr(0, pos);
		port = atoi(name.substr(pos + 1).c_str());
	}

	void init(struct sockaddr_in src){
		ip = inet_ntoa(src.sin_addr);
		port = ntohs(src.sin_port);

		name = ip + ":" + std::to_string(port);
	}

	// Transfer address to sockaddr_in
	sockaddr_in combine(){
		struct sockaddr_in addr_inet;
		bzero(&addr_inet, sizeof(addr_inet));
		addr_inet.sin_family = AF_INET;
		addr_inet.sin_port = htons(port);
		addr_inet.sin_addr.s_addr = inet_addr(ip.c_str());
		return addr_inet;
	}
};

bool operator == (const Address a, const Address b){
	return (a.name == b.name);
}

bool operator < (const Address a, const Address b){
	return (a.name < b.name);
}

#endif
