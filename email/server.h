/*
 * server.h
 */

#ifndef SERVER_H_
#define SERVER_H_

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#include <arpa/inet.h>

#include <fstream>
#include <iostream>

#include <list>
#include <string>

static bool debug; //print debug information or not

/*
 * Based on codes from course web page.
 */
bool do_write(int fd, std::string& message) {
	std::string send_msg = message + "\r\n";

	const char *buf = send_msg.data();
	int len = send_msg.size();

	int sent = 0;
	while(sent < len){
		int n = write(fd, &buf[sent],len - sent);
		if(n < 0){
			if(debug)
				fprintf(stderr, "[%d] Write Fail\n", fd);
			return false;
		}
		sent += n;
	}

	if(debug)
		fprintf(stderr, "[%d] S: %s\n", fd, message.data());
	return true;
}

/*
 * Based on codes from course web page.
 * Read one character at a time.
 */
bool do_read(int fd, std::string& message) {
	message = "";
	char c;
	bool gr = false;

	while(true){
		int rcvd = 0;
		while (rcvd < 1) {
			int n = read(fd, (void*)(&c), 1 - rcvd);
			if(n < 0){
				if(debug)
					fprintf(stderr, "[%d] Read Fail\n", fd);
				return false;
			}
			rcvd += n;
		}

		if(!gr){
			if(c == '\r'){
				gr = true;
			}
			else{
				message += c;
			}
		}
		else{
			if(c == '\n'){
				break;
			}
			else{
				message += '\r';
				message += c;
				gr = false;
			}
		}
	}
	if(debug)
		fprintf(stderr, "[%d] C: %s\n", fd, message.data());
	return true;
}

class Server{
public:

	/*
	 * socket + bind + listen
	 */
	Server(int _port, void* (*_worker) (void *)):
		port(_port), worker(_worker){
		int ret;
		listen_fd = socket(PF_INET, SOCK_STREAM, 0);
		if(listen_fd < 0){
			fprintf(stderr, "Error: Cannot open email socket (%s)\n", strerror(errno));
			exit(1);
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
            error("Failed to set socket options due to %s. \n",
                    strerror(errno));
        }

		ret = bind(listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr));
		if(ret < 0){
			fprintf (stderr, "Bind fail\n");
			exit(1);
		}
		ret = listen(listen_fd, 100);
	}

	/*
	 * Accept connections.
	 */
	void start(){
		while (true) {
			// Remove closed connection
			auto it = children.begin();
			while(it != children.end()){
				if(*(*it) < 0){
					delete (*it);
					it = children.erase(it);
				}
				else{
					++it;
				}
			}

			struct sockaddr_in clientaddr;
			socklen_t clientaddrlen = sizeof(clientaddr);

			int *fd = new int;
			*fd = accept(listen_fd, (struct sockaddr*)&clientaddr, &clientaddrlen);
			if(debug)
				fprintf (stderr, "[%d] New connection\n", *fd);

			pthread_t thread;
			pthread_create(&thread, NULL, worker, fd);
			pthread_detach(thread);

			children.push_back(fd);
		}
	}

	/*
	 * Close all connections.
	 */
	void stop(bool send = false){
		close(listen_fd);

		auto it = children.begin();
		while(it != children.end()){
			if(*(*it) >= 0){
				if(send){
					std::string message = "-ERR Server shutting down";
					do_write(*(*it), message);
				}
				close(*(*it));
				if(debug)
					fprintf (stderr, "[%d] Connection closed\n", *(*it));
			}
			delete (*it);
			it = children.erase(it);
		}
	}

protected:
	int port;
	int listen_fd;

	void* (*worker) (void *arg);

	std::list<int*> children; // fd of all connections
};



#endif /* SERVER_H_ */
