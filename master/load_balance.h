#ifndef LOAD_BALANCE_H_
#define LOAD_BALANCE_H_

#include <sys/epoll.h>

#include "monitor.h"

#include "../common/msg_operation.h"
#include "../common/tcp_operation.h"
#include "../common/file_operation.h"

#define LOAD_BALANCE_PORT 10001

static std::mutex browserfd_mtx;
static std::queue<int> browserfd_queue;

// randomly select a frontend server
Address select_frontend(){
	auto it = frontends.begin();
	uint32_t index = rand() % frontends.size();
	std::advance(it, index);
	return it->second;
}

void* handle_browser(void *arg){
	int epoll_fd = epoll_create1(0);
	if(epoll_fd < 0){
		error("epoll_create error.\n");
	}

	struct epoll_event event;
	event.events = EPOLLIN;

	while(true){
		browserfd_mtx.lock();
		while(!browserfd_queue.empty()){
			event.data.fd = browserfd_queue.front();
			browserfd_queue.pop();
			if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event.data.fd, &event) != 0) {
				warn("Fail to add fd to epoll.\n");
			}
		}
		browserfd_mtx.unlock();

		int ret = epoll_wait(epoll_fd, &event, 1, 100);

		if(ret < 0){
			warn("epoll_wait error.\n");
		}

		char buf[2000] = {0};

        std::string err = "HTTP/1.1 503 Service Unavailable\r\n";
		std::string msg = "HTTP/1.1 302 Found\r\nLocation: http://localhost:";

		if(ret > 0){
			if(read(event.data.fd, buf, 2000) > 0){
                if(frontends.size() < 1){
                    tcp_write(event.data.fd, err.c_str(), err.size());
                }
                else{
					Address addr = select_frontend();
                    msg += std::to_string(addr.port);
                    msg += "/login\r\n\r\n";
				    tcp_write(event.data.fd, msg.c_str(), msg.size());
					debug("Load balance msg: %s\n", msg.c_str());
                }
			}
			close(event.data.fd);
			epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event.data.fd, &event);
		}
	}
}

void* load_balance(void *arg){
	int listen_fd = tcp_server_socket(LOAD_BALANCE_PORT);
	if(listen_fd < 0)
		exit(1);

	pthread_t conn, work;

	pthread_create(&work, NULL, handle_browser, NULL);
	pthread_detach(work);

    while(!dead){
		struct sockaddr_in clientaddr;
		socklen_t clientaddrlen = sizeof(clientaddr);

		int fd = accept(listen_fd, (struct sockaddr*)&clientaddr, &clientaddrlen);
		if(fd < 0){
			warn("Accept fails\n");
			continue;
		}

        browserfd_mtx.lock();
		browserfd_queue.push(fd);
		browserfd_mtx.unlock();
	}
}

#endif