#include <sys/epoll.h>

#include "../common/hash.h"
#include "../common/http_operation.h"
#include "../common/master_interface.h"
#include "frontend_config.h"
#include "reply.h"

void *handle_connect(void *arg) {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        error("epoll_create error.\n");
    }

    struct epoll_event event;
    event.events = EPOLLIN;

    while (true) {
        sockfd_mtx.lock();
        while (!sockfd_queue.empty()) {
            event.data.fd = sockfd_queue.front();
            sockfd_queue.pop();
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event.data.fd, &event) !=
                0) {
                warn("Fail to add fd to epoll.\n");
            }
        }
        sockfd_mtx.unlock();

        int ret = epoll_wait(epoll_fd, &event, 1, 100);

        if (ret < 0) {
            warn("epoll_wait error.\n");
        }

        if (ret > 0) {
            Request request;
            if (request.http_read(event.data.fd)) {
                reply(event.data.fd, request);
            }
            close(event.data.fd);
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event.data.fd, &event);
        }
    }
}

int main(int argc, char *argv[]) {
    std::ios::sync_with_stdio(false);  // to speed up

    int c;
    while ((c = getopt(argc, argv, "p:")) != -1) {
        switch (c) {
            case 'p':
                port = atoi(optarg);
                break;
            case '?':
                if (optopt == 'p')
                    printf("Option -%c requires an argument.\n", optopt);
                else if (isprint(optopt))
                    printf("Unknown option `-%c'.\n", optopt);
                else
                    printf("Unknown option character `\\x%x'.\n", optopt);
                exit(1);
            default:
                abort();
        }
    }

    assert(port > 0 && port < 65536);
    my_addr.init("127.0.0.1:" + std::to_string(port));

    Address dst;
    dst.init("127.0.0.1:10000");

    master_fd = tcp_client_socket(dst);
    if (master_fd < 0) {
        error("Error: Frontend cannot open master socket (%s)\n", strerror(errno));
    }

    if (!frontend_initial(master_fd, my_addr.name)) {
        error("Connect to Master error\n");
    }

    int listen_fd = tcp_server_socket(port);
    if (listen_fd < 0){
        error("Error: Frontend cannot open listen socket (%s)\n", strerror(errno));
    }
    
    pthread_t conn;

    pthread_create(&conn, NULL, handle_connect, NULL);
    pthread_detach(conn);

    while (!dead) {
        struct sockaddr_in clientaddr;
        socklen_t clientaddrlen = sizeof(clientaddr);

        int fd =
            accept(listen_fd, (struct sockaddr *)&clientaddr, &clientaddrlen);
        if (fd < 0) {
            warn("Accept fails\n");
            continue;
        }

        sockfd_mtx.lock();
        sockfd_queue.push(fd);
        sockfd_mtx.unlock();
    }

    return 0;
}