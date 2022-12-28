#include <sys/epoll.h>
#include <time.h>

#include "../common/file_operation.h"
#include "../common/kv_interface.h"
#include "../common/master_interface.h"
#include "../common/tcp_operation.h"
#include "cache.h"
#include "cluster_interface.h"
#include "kv_config.h"
KvCache::KvCache cache;

void run_command(kv_command& command, int sender_fd, kv_ret& ret) {
    std::string dir = PREFIX + command.usr() + "/";
    std::string path = PREFIX + command.usr() + "/" + command.key();

    debug_v2(
        "[KvStore %s]: Node IsPrimary %d handling new command with type %s\n",
        my_addr.name.c_str(), isPrimary, command.com().data());

    std::vector<char> value;
    if (command.com() == "RESTART" && killed) {
        debug("[KvStore %s]: Restarting... Node isPrimary? %d\n",
              my_addr.name.c_str(), isPrimary);
        killed = false;

        // Recover
        int primary_fd = -1;
        if (isPrimary) {
            if (cache.InitCacheForPrimary() != FINISHED) {
                error(
                    "[KvStore]: Failed to initialize primary during syncing. "
                    "Exiting....\n");
            }
        } else {
            // Connect to primary, and ask to sync
            Address primary = secondary.at(0);
            int max_attempt = 5;
            while (max_attempt > 0 && primary_fd < 0) {
                primary_fd = tcp_client_socket(primary);
                max_attempt--;
                sleep(1);
            }

            if (primary_fd < 0) {
                error("[KvStore]: Failed to connect to primary. Exiting....\n");
            }

            if (cache.SecondaryRecoverFromPrimary(primary_fd) != FINISHED) {
                cache.SecondarySendFinishedRecovery(primary_fd,
                                                    /*success=*/false);
                error(
                    "Failed to initialize node due to syncing error. "
                    "Exiting....\n");
            }
            // send msg to primary indicate sync finished
            cache.SecondarySendFinishedRecovery(primary_fd, /*success=*/true);
        }
        return;
    } else if (command.com() == "CLUSTER") {
        debug("[KvStore %s]: Received CLUSTER command\n", my_addr.name.c_str());
        parse_secondary_from_master(command);
        return;
    }

    if (killed) {
        debug("[KvStore %s]: Node is killed. Returning...\n",
              my_addr.name.c_str());
        return;
    }

    if (command.com() == "PUTS") {
        if (isPrimary) {
            forward_to_secondary(command);
        }
        int res = cache.Puts(command.usr(), command.key(), command.value1(),
                             ++max_sequence);
        ret.set_status(res);
    }

    else if (command.com() == "CPUT") {
        if (isPrimary) {
            forward_to_secondary(command);
        }
        std::string old_value;
        // if gets return FINISHED
        if (cache.Gets(command.usr(), command.key(), old_value) == FINISHED) {
            old_value = binary_to_text(value);
        } else
            old_value = "";

        const std::string& prev_val = command.value1();

        // if pwd file empty and folder not exist, and prev value empty
        if (command.key() == "pwd" && !exist_file(dir.c_str()) &&
            prev_val == "" && old_value == "") {
            create_dir(dir.c_str());
        }

        int res = cache.Cputs(command.usr(), command.key(), command.value1(),
                              command.value2(), ++max_sequence);
        ret.set_status(res);
        // max_sequence += 1;

    }

    else if (command.com() == "GETS") {
        std::string val;
        int res = cache.Gets(command.usr(), command.key(), val);
        ret.set_status(res);
        ret.set_value(val);
    }

    else if (command.com() == "DELE") {
        if (isPrimary) {
            forward_to_secondary(command);
        }
        int res = cache.Dele(command.usr(), command.key(), ++max_sequence);
        ret.set_status(res);
    } else if (command.com() == "CKPT") {
        debug("[KvStore %s]: Checkpointing\n", my_addr.name.c_str());
        checkpoint(cache);
    }

    else if (command.com() == "SYNC") {
        debug("[KvStore %s]: Received SYNC Command.\n", my_addr.name.c_str());
        int res = cache.PrimarySyncSecondary(sender_fd);
        ret.set_status(res);
        debug(
            "[KvStore %s]: Finished syncing with status %d. Continue accepting "
            "commands....\n",
            my_addr.name.c_str(), ret.status());
    }

    else if (command.com() == "ALL") {
        debug("[KvStore]: Processing ALL request with user %s.\n",
              command.usr().c_str());
        if (!command.has_usr()) {
            warn(
                "#KvServerError: Failed to processs GetsALL request as user "
                "field is null.\n");
            ret.set_status(USER_ERROR);
            return;
        }
        int res = cache.GetsAll(command.usr(), ret);
        ret.set_status(res);
    } else if (command.com() == "KILL") {
        debug("[KvStore %s]: Node being killed.\n", my_addr.name.c_str());
        killed = true;
        isPrimary = false;
    }
}

void* handle_connect(void* arg) {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        error("epoll_create error.\n");
    }

    struct epoll_event event;
    event.events = EPOLLIN;

    std::string msg;
    last_checkpoint_time = 1;

    debug_v2("[KvStore %s]: Start handling requests\n", my_addr.name.c_str());

    kv_command command;
    while (true) {
        // Pop fds and add them into epoll
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

        // Wait for commands from frontend,
        int ret = epoll_wait(epoll_fd, &event, 1, 100);

        if (ret < 0) {
            warn("epoll_wait error.\n");
        }

        if (ret > 0) {
            // Read commands and process them
            if (tcp_read_msg(event.data.fd, msg)) {
                last_checkpoint_time += 1;
                command.ParseFromString(msg);

                kv_ret ret;
                ret.set_status(FINISHED);
                run_command(command, event.data.fd, ret);

                if (isPrimary) {
                    ret.SerializeToString(&msg);
                    tcp_write_msg(event.data.fd, msg);
                }

                if (isPrimary && last_checkpoint_time % 20 == 0) {
                    debug("Primary checkpt\n");
                    checkpoint(cache);
                }
            }
            // Close the connection which is closed by frontend servers
            else {
                close(event.data.fd);
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event.data.fd, &event);
            }
        }
    }
}

void* getting_request(void* arg) {
    int listen_fd = *(int*)(arg);

    debug_v2("[KvStore %s]: Start accepting new connections\n",
             my_addr.name.c_str());

    while (true) {
        struct sockaddr_in clientaddr;
        socklen_t clientaddrlen = sizeof(clientaddr);

        int fd =
            accept(listen_fd, (struct sockaddr*)&clientaddr, &clientaddrlen);
        if (fd < 0) {
            warn("Accept fails\n");
            continue;
        }

        // Push the fd to queue
        sockfd_mtx.lock();
        sockfd_queue.push(fd);
        sockfd_mtx.unlock();
    }
}

void RunServer() {
    assert(port > 0 && port < 65536);
    my_addr.init("127.0.0.1:" + std::to_string(port));
    PREFIX += std::to_string(port) + "/";

    debug_v2("[KvStore %s]: Attempting to initiate KV store server at %s.\n",
             my_addr.name.c_str(), PREFIX.c_str());
    std::string command = "mkdir -p " + PREFIX;
    if (system(command.c_str()) != 0) {
        error("Cannot create folder\n");
    }

    init = false;

    debug_v2(
        "[KvStore %s]: Initiating KV store server with storage location %s\n.",
        my_addr.name.c_str(), PREFIX.c_str());
    cache.UpdateLogging(PREFIX + "logging");

    int listen_fd = tcp_server_socket(my_addr.port);
    if (listen_fd < 0) {
        error("Error: Cannot open listen socket (%s)\n", strerror(errno));
    }
    pthread_t conn, start_listening;
    pthread_create(&start_listening, NULL, getting_request, &listen_fd);
    pthread_detach(start_listening);

    // Create a thread to handle connections from frontend servers
    pthread_create(&conn, NULL, handle_connect, NULL);
    pthread_detach(conn);

    // connect to master
    Address dst;
    dst.init("127.0.0.1:10000");

    debug_v2("[KvStore %s]: connecting to master\n", my_addr.name.c_str());
    master_fd = tcp_client_socket(dst);
    if (master_fd < 0) {
        error("Error: Cannot open master socket (%s)\n", strerror(errno));
    }

    // while backend+initial, syncing to primary
    if (!backend_initial(master_fd, my_addr.name)) {
        error("Connect to Master error\n");
    }

    debug_v2("[KvStore %s]: Node is init? %d\n", my_addr.name.c_str(), init);
    while (!init);

    debug_v2("[KvStore %s]: Node initiating syncing process.\n",
             my_addr.name.c_str());

    int primary_fd = -1;
    if (isPrimary) {
        debug_v2("[KvStore %s]: Primary initiating cache.\n",
                 my_addr.name.c_str());
        if (cache.InitCacheForPrimary() != FINISHED) {
            error("Failed to initialize primary during syncing. Exiting....\n");
        }
    } else {
        debug_v2("[KvStore %s]: Secondary start syncing with primary.\n",
                 my_addr.name.c_str());
        // Connect to primary, and ask to sync
        Address primary = secondary.at(0);
        int max_attempt = 5;
        while (max_attempt > 0 && primary_fd < 0) {
            primary_fd = tcp_client_socket(primary);
            max_attempt--;
            sleep(1);
        }

        if (primary_fd < 0) {
            error("[KvStore %s]: Failed to connect to primary. Exiting....\n",
                  my_addr.name.c_str());
        }

        if (cache.SecondaryRecoverFromPrimary(primary_fd) != FINISHED) {
            cache.SecondarySendFinishedRecovery(primary_fd, /*success=*/false);
            error(
                "[KvStore %s]: Failed to initialize node due to syncing error. "
                "Exiting....\n",
                my_addr.name.c_str());
        }
        // send msg to primary indicate sync finished
        cache.SecondarySendFinishedRecovery(primary_fd, /*success=*/true);
    }

    // // send msg to priary to indicate finish syncing
    // if (!isPrimary) {
    // }

    sleep(1);
    close(primary_fd);

    while (true) {
        send_heartbeat(master_fd, my_addr.name);
        sleep(20);
    }

    close(listen_fd);
}

int main(int argc, char* argv[]) {
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

    RunServer();
    return 0;
}