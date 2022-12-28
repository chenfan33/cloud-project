#ifndef TCP_OPERATION_H_
#define TCP_OPERATION_H_

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/ip.h>

#include <vector>
#include <iostream>

#include "address_parse.h"
#include "debug_operation.h"
#include "msg_operation.h"

// tcp_write <-> tcp_read
// tcp_write_msg <-> tcp_read_msg
// In tcp_*_msg, we add the length of message at the beginning

bool tcp_write(int fd, const char* buf, size_t length){
	size_t sent = 0;
	while(sent < length){
		int n = write(fd, &buf[sent], length - sent);
		if(n < 0){
			warn("Cannot write TCP fd %d\n", fd);
			warn("Error (%s)\n", strerror(errno));
			return false;
		}
		sent += n;
	}
	return true;
}

bool tcp_read(int fd, char* buf, size_t length){
	size_t sent = 0;
	while(sent < length){
		int n = read(fd, &buf[sent], length - sent);
		if(n <= 0){
			//warn("Cannot read TCP fd %d\n", fd);
			return false;
		}
		sent += n;
	}
	return true;
}


bool tcp_write_msg(int fd, std::string& msg) {
	size_t length = msg.size();
	if(tcp_write(fd, (char*)&length, sizeof(size_t))){
		return tcp_write(fd, msg.data(), msg.size());
	}
	return false;
}

bool tcp_read_msg(int fd, std::string& msg) {
	bool ret = false;
	size_t length = 0;
	std::vector<char> buf;
	if(tcp_read(fd, (char*)&length, sizeof(size_t))){
		buf.resize(length);
		ret = tcp_read(fd, buf.data(), buf.size());
	}
	msg.assign(buf.begin(), buf.end());
	return ret;
}

int tcp_client_socket(Address dst){
	int fd = socket(PF_INET, SOCK_STREAM, 0);
	if(fd < 0){
		warn("TCP client - Cannot open socket %s (%s)\n", dst.name.c_str(), strerror(errno));
		return -1;
	}

	struct sockaddr_in servaddr = dst.combine();
    if(connect(fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
		close(fd);
		return -1;
	}
	return fd;
}

int tcp_server_socket(int port){
	// Listen connections
	int listen_fd = socket(PF_INET, SOCK_STREAM, 0);
	if(listen_fd < 0){
		warn("TCP server - Cannot open socket %d (%s)\n", port, strerror(errno));
		return -1;
	}

	struct sockaddr_in servaddr;
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = htons(INADDR_ANY);
	servaddr.sin_port = htons(port);

    // Set socket option to make it reuse address and ports.
    int socket_opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                        &socket_opt, sizeof(socket_opt)) < 0) {
        warn("Failed to set socket options due to %s. \n", strerror(errno));
    }

	int ret = bind(listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr));
	if(ret < 0){
		warn("Bind fail\n");
		return -1;
	}
	listen(listen_fd, 10);

	return listen_fd;
}

#endif
