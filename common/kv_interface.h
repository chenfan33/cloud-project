#ifndef KV_INTERFACE_H_
#define KV_INTERFACE_H_

#include <iostream>

#include <vector>
#include <string>

#include "tcp_operation.h"
#include "proto_gen/proto.pb.h"

enum KV_State{
	FINISHED = 0,
	LINK_ERROR = -1,
	USER_ERROR = -2,
	KEY_ERROR = -3,
	VALUE_ERROR = -4,
    SEQ_ERROR = -5,  // When received an operation out of sequence. 
    LOG_ERROR = -6,  // Error when logging KV operations
    REC_ERROR = -7,  // Error when recovering from logging
    SYNC_ERROR = -8,  // Error when syncing recovered node with primary
};

// Send kv_command to KV store and wait for kv_ret
bool kv_trans(int fd, kv_command& command){
	std::string send;

	if(!command.SerializeToString(&send))
		return false;

	if(!tcp_write_msg(fd, send))
		return false;

	return true;
}

bool kv_trans(int fd, kv_command& command, kv_ret& ret){
	std::string send, receive;

	if(!command.SerializeToString(&send))
		return false;

	if(!tcp_write_msg(fd, send))
		return false;
	if(!tcp_read_msg(fd, receive))
		return false;

	if(!ret.ParseFromString(receive))
		return false;

	return true;
}

// PUT(usr, key, value): Stores "value" in column "key" of row "usr"
int kv_puts(int fd, std::string usr, std::string key, 
				std::string value){
	kv_command command;
	kv_ret ret;

	command.set_com("PUTS");
	command.set_usr(usr);
	command.set_key(key);
	command.set_value1(value);

	if(kv_trans(fd, command, ret))
		return ret.status();
	else
		return LINK_ERROR;
}

// CPUT(usr, key, value1, value2): Stores "value2" in column "key" of row "usr",
// but only if the current value is "value1"
int kv_cput(int fd, std::string usr, std::string key, 
				std::string value1, std::string value2){
	kv_command command;
	kv_ret ret;

	command.set_com("CPUT");
	command.set_usr(usr);
	command.set_key(key);
	command.set_value1(value1);
	command.set_value2(value2);

	if(kv_trans(fd, command, ret))
		return ret.status();
	else
		return LINK_ERROR;
}

// GET(usr, key): Returns the value (str) stored in column "key" of row "usr"
int kv_gets(int fd, std::string& str, std::string usr, std::string key){
	kv_command command;
	kv_ret ret;

	command.set_com("GETS");
	command.set_usr(usr);
	command.set_key(key);

	if(kv_trans(fd, command, ret)){
		str = ret.value();
		return ret.status();
	}
	else
		return LINK_ERROR;
}

int kv_gets_all(int fd, std::unordered_map<std::string, std::string>& kvs, std::string usr){
 	kv_command command;
 	kv_ret ret;

 	command.set_com("ALL");
 	command.set_usr(usr);

	kvs.clear();
 	if(kv_trans(fd, command, ret)){
		for(uint32_t i = 0;i < ret.key_values_size();++i){
			auto kv = ret.key_values(i);
			kvs[kv.key()] = kv.value();
		}
 		return ret.status();
 	}
 	else
 		return LINK_ERROR;
}

// DELETE(usr, key): Deletes the value in column "key" of row "usr"
int kv_dele(int fd, std::string usr, std::string key){
	kv_command command;
	kv_ret ret;

	command.set_com("DELE");
	command.set_usr(usr);
	command.set_key(key);

	if(kv_trans(fd, command, ret))
		return ret.status();
	else
		return LINK_ERROR;
}

int kv_cluster(int fd, std::vector<std::string> addrs){
	kv_command command;
	command.set_com("CLUSTER");
	for(auto addr : addrs){
		command.add_addrs(addr);
	}

	if(kv_trans(fd, command))
		return FINISHED;
	else
		return LINK_ERROR;
}

int kv_kill(int fd){
	kv_command command;
	command.set_com("KILL");

	if(kv_trans(fd, command))
		return FINISHED;
	else
		return LINK_ERROR;
}

int kv_restart(int fd){
	kv_command command;
	command.set_com("RESTART");

	if(kv_trans(fd, command))
		return FINISHED;
	else
		return LINK_ERROR;
}

#endif
