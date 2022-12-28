#ifndef MASTER_INTERFACE_H_
#define MASTER_INTERFACE_H_

#include "proto_gen/proto.pb.h"

// Send heartbeat msg to main coodinater
bool send_heartbeat(int master_fd, std::string addr) {
	MasterRequest command;
	command.set_type(HEARTBEAT);
	command.set_addr(addr);

	std::string send;
	if (!command.SerializeToString(&send))
		return false;
	if (!tcp_write_msg(master_fd, send))
		return false;
	return true;
}

bool frontend_initial(int master_fd, std::string addr) {
	MasterRequest command;
	command.set_type(FRONTEND_INITIAL);
	command.set_addr(addr);

	std::string send;
	if (!command.SerializeToString(&send))
		return false;
	if (!tcp_write_msg(master_fd, send))
		return false;

	return true;
}

bool email_initial(int master_fd, std::string addr) {
	MasterRequest command;
	command.set_type(EMAIL_INITIAL);

	std::string send;
	if (!command.SerializeToString(&send))
		return false;
	if (!tcp_write_msg(master_fd, send))
		return false;

	return true;
}

// Get the primary backend address of one user
bool usr_to_address(int master_fd, std::string usr, std::string &backend) {
	MasterRequest command;
	command.set_type(USR_TO_BACKEND);
	command.set_addr(usr);

	std::string send, receive;
	if (!command.SerializeToString(&send))
		return false;

	if (!tcp_write_msg(master_fd, send))
		return false;
	if (!tcp_read_msg(master_fd, receive))
		return false;

	FrontEndResp ret;
	if (!ret.ParseFromString(receive))
		return false;
	if (ret.backend_addrs_size() <= 0)
		return false;

	backend = ret.backend_addrs()[0];
	return true;
}

#endif