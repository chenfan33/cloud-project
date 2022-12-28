#include "load_balance.h"

#include "../common/hash.h"
#include "../common/http_operation.h"
#include "../common/kv_interface.h"
#include "../master/admin.h"

void run_command(MasterRequest& master_req, int fd){
    debug_v2("[Master]: Processing req %s.\n", master_req.DebugString().c_str());

    if(master_req.type() == FRONTEND_INITIAL){
		Address addr;
		addr.init(master_req.addr());
		frontends[fd] = addr;
		debug("Frontend port: %d\n", frontends[fd].port);
    }
	else if(master_req.type() == BACKEND_INITIAL){
		Address addr;
		addr.init(master_req.addr());
		set_backend_fd(addr, fd);
		std::string finish = "Finished";
		tcp_write_msg(fd, finish);
    }
	else if(master_req.type() == EMAIL_INITIAL){
		debug("EMAIL\n");
    }
	else if(master_req.type() == USR_TO_BACKEND){
        FrontEndResp ret;
        std::string msg;
		Backend backend;
		get_primary(master_req.addr(), backend);
        ret.add_backend_addrs(backend.addr.name);
        ret.SerializeToString(&msg);
		tcp_write_msg(fd, msg);
		debug("Return primary %s\n", backend.addr.name.c_str());
    }
	else if(master_req.type() == HEARTBEAT) {
		debug("receving heartbeat from fd %d\n", fd);
	}
}

void* handle_connect(void *arg){
	int epoll_fd = epoll_create1(0);
	if(epoll_fd < 0){
		error("epoll_create error.\n");
	}

	struct epoll_event event;
	event.events = EPOLLIN;

	std::string msg;

	MasterRequest master_req;

	while(true){
		sockfd_mtx.lock();
		while(!sockfd_queue.empty()){
			event.data.fd = sockfd_queue.front();
			sockfd_queue.pop();
			if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event.data.fd, &event) != 0) {
				warn("Fail to add fd to epoll.\n");
			}
		}
		sockfd_mtx.unlock();

		int ret = epoll_wait(epoll_fd, &event, 1, 100);

		if(ret < 0){
			warn("epoll_wait error.\n");
		}

		if(ret > 0){
			if(tcp_read_msg(event.data.fd, msg)){
				if (!master_req.ParseFromString(msg)) {
                    warn("Master: Failed to parse msg %s\n", msg.c_str());
                }
                debug_v2("[Master]: Received msg %s.\n", msg.c_str());
                run_command(master_req, event.data.fd);
			}
			else{
				if(frontends.find(event.data.fd) != frontends.end())
                    frontends.erase(event.data.fd);
				else{
					delete_backend_fd(event.data.fd);
				}
				close(event.data.fd);
				epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event.data.fd, &event);
			}
		}
	}
}

int main(){
    std::ios::sync_with_stdio(false); // to speed up

	if(!initialize_backend()){
        error("Cannot initialize backend\n");
	}

	pthread_t lb;
    pthread_create(&lb, NULL, load_balance, NULL);
    pthread_detach(lb);

	int listen_fd = tcp_server_socket(MASTER_PORT);
	if(listen_fd < 0)
		exit(1);

	pthread_t admin;
	pthread_create(&admin, NULL, admin_console, NULL);
	pthread_detach(admin);

    pthread_t conn;
	pthread_create(&conn, NULL, handle_connect, NULL);
	pthread_detach(conn);

    Address address;
    while(!dead){
		struct sockaddr_in clientaddr;
		socklen_t clientaddrlen = sizeof(clientaddr);

		int fd = accept(listen_fd, (struct sockaddr*)&clientaddr, &clientaddrlen);
		if(fd < 0){
			warn("Accept fails\n");
			continue;
		}
        address.init(clientaddr);
        sockfd_mtx.lock();
		sockfd_queue.push(fd);
		sockfd_mtx.unlock();
	}

	return 0;
}