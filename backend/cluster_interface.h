
#ifndef CLUSTER_INTERFACE_H_
#define CLUSTER_INTERFACE_H_
#include "../common/kv_interface.h"
#include "kv_config.h"
#include "cache.h"

/**
 * Store cluster information including primary node's address
*/
bool parse_secondary_from_master(const kv_command& ret) {
	secondary.clear();
	for (const auto& addr_str : ret.addrs()) {
		Address addr;
		addr.init(addr_str);
		secondary.push_back(addr);
	}
	if (my_addr.port == secondary.at(0).port){
		isPrimary = true;
	}
	init = true;
	return true;
}

/**
 * Send BACKEND_INITIAL request to master 
 * return false if not success or not receiving FINISHED from master
*/
bool backend_initial(int master_fd, std::string addr) {
	MasterRequest command;
	command.set_type(BACKEND_INITIAL);
	command.set_addr(addr);

	std::string send = "";
    std::string receive = "";
	if (!command.SerializeToString(&send)) {
		return false;
    }

	if (!tcp_write_msg(master_fd, send)) {
		return false;
    }

	kv_command ret;
	if (!tcp_read_msg(master_fd, receive))
		return false;

	return true;
}

/**
 * For primary to broadcast commands to secondaries
*/
void connect_to_other(){
    int client_fd;
    for (auto node : secondary) {
        if (node.port != my_addr.port){
            client_fd = tcp_client_socket(node);
            if (client_fd < 0) {
                warn("Warn: Cannot open socket (%s)\n", strerror(errno));
            }
            fds.push_back(client_fd);
        }
    }
}

bool forward_to_secondary(kv_command& command){
	//connect to other backends
	fds.clear();
	connect_to_other();
    // kv_ret ret;
	std::string send;
	int res = true;
    if(!command.SerializeToString(&send))
		return false;
	for (auto fd : fds) {
		if(!kv_trans(fd, command)){
		    res = LINK_ERROR;
        }
		close(fd);
	}
	return res;
}


void checkpoint(KvCache::KvCache cache){
    kv_command command;
	kv_ret ret;
    
    // if primary forward
    if (secondary.at(0).port == my_addr.port){
	    command.set_com("CKPT");
        forward_to_secondary(command);
    }
    int res = cache.Checkpoint();
    ret.set_status(res);
    
}

#endif
