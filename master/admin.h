#include "master_request.h"
#include "master_config.h"

/* Retrieve all raw data of the given user from backend */
int get_rawdata(std::string usr){
    int ret = LINK_ERROR;
    Backend backend;
	if(get_primary(usr, backend)){
        int fd = tcp_client_socket(backend.addr);
        if (fd > 0){
            ret = kv_gets_all(fd, kvs, usr);
            close(fd);
        }
    }
    return ret;
}

/* Helper function to view raw data
 Retrieve all raw data for the user and format the html response
 */
std::string raw_data_list_construct(std::string raw_data_usr) {
    std::string result = "";
    if (raw_data_usr == "")
        return "";
    else {
        get_rawdata(raw_data_usr);            
        int index = 1;
        for (auto item: kvs) {
            char row[1000];
            sprintf(row, RAW_TABLE_FORMAT, index, item.first.c_str(), 
                    item.second.c_str());
            index ++;
            std::string s = row;
            result += s;
        }
    }
    return result;
}

/* Helper function to display backend list
   Retrieve the status of backend nodes with according supported actions
    and format the html response
 */
std::string backend_list_construct(std::vector<Backend> backends) {
	std::string result = "";
    for (auto item: backends) {
        char row[1000];
        if (item.state == ALIVE) {
            sprintf(row, BACKEND_ALIVE_FORMAT, item.group_id, item.addr.name.c_str(), 
                    item.addr.name.c_str());
        } else if (item.state == DEAD) {
            sprintf(row, BACKEND_DEAD_FORMAT, item.group_id, item.addr.name.c_str(), 
                    item.addr.name.c_str());
        } else if (item.state == CRASH) {
            sprintf(row, BACKEND_CRASH_FORMAT, item.group_id, item.addr.name.c_str());
        }
        std::string s = row;
        result += s;
    }
    return result;
}

/* Helper function to display frontend list
   All returned nodes are ALIVE
 */
std::string frontend_list_construct(std::unordered_map<int, Address> frontends) {
	std::string result = "";
    int index = 1;
    for (auto item: frontends) {
        char row[1000];
        sprintf(row, FRONTEND_FORMAT, index, 
                item.second.name.c_str(), "Alive");
        std::string s = row;
        result += s;
		index ++;
    }
    return result;
}

/* Helper function to display admin page
   Components: frontend list, backend list, view raw data function
   Retrieve current state node information, 
   and populate the constructed html body
 */
void display_admin_page(std::string raw_data_usr, Response& response) {
    std::string html;
    read_file(ABSOLUTE_FRONTEND_DIR + "/html/admin.html", html);
    
    std::string frontend_table = frontend_list_construct(frontends);
    html = replace(html, "$frontend_table", frontend_table);
    
    std::string backend_table = backend_list_construct(backends);
    html = replace(html, "$backend_table", backend_table);

    std::string raw_table = raw_data_list_construct(raw_data_usr);
    html = replace(html, "$raw_table", raw_table);
    
    response.body_ = html;
    response.headers_["Content-Length: "] = std::to_string(response.body_.length());
}

/* Main function to handle requests in display admin page
   Operations: KILL/RESTART backend nodes, VIEW raw data for one user
 */
Response admin_handler(Request& request){
    Response response;
    std::string raw_data_usr = "";
    if (request.method_ == "GET") {
        display_admin_page(raw_data_usr, response);
    } else if (request.method_ ==  "POST") {
		std::vector<std::string> vec;
        vec = split(request.body_, '\r');
        if (vec[1].find("name=\"KILL\"") != std::string::npos) {
            std::string node_name = vec[3].substr(1);
            Address kill_addr;
            kill_addr.init(node_name);
            kill_backend(kill_addr);
            debug("KILL: %s\n", node_name.c_str());
        }
		else if (vec[1].find("name=\"RESTART\"") != std::string::npos) {
            std::string node_name = vec[3].substr(1);
            Address restart_addr;
            restart_addr.init(node_name);
            restart_backend(restart_addr);
            debug("RESTART: %s\n", node_name.c_str());
        } 
		else if (vec[1].find("name=\"RAW\"") != std::string::npos) {
			raw_data_usr = vec[3].substr(1);
            debug("RAW DATA FOR USER: %s\n", raw_data_usr.c_str());
		}
        display_admin_page(raw_data_usr, response);
    }
    return response;
}

/* If request path is /admin, return response
    Otherwise return 404 not found
 */
void reply_admin(int fd, Request& request){
    Response response;
    if(request.path_.compare("/admin") == 0){
        response = admin_handler(request);
    }
    else{
        if (request.path_.find(".js") != std::string::npos) {
            response.load_content(OK, "text/javascript", ABSOLUTE_FRONTEND_DIR + "/html" + request.path_);
        } else if (request.path_.find(".css") != std::string::npos) {
            response.load_content(OK, "text/css", ABSOLUTE_FRONTEND_DIR + "/html" + request.path_);
        } else if (request.path_.find(".png") != std::string::npos) {
            response.load_content(OK, "image/png", ABSOLUTE_FRONTEND_DIR + request.path_);
        } else {
            response.load_content(OK, "text/html", ABSOLUTE_FRONTEND_DIR + "/html/error_page.html");
        }
    }
    std::string body = request.http_version_ + response.to_string();
    http_write_request(fd, request.http_version_ + body);
}

/* Open socket and listen for requests in admin port */
void *handle_admin_browser(void *arg) {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        error("epoll_create error.\n");
    }
    struct epoll_event event;
    event.events = EPOLLIN;
    while (true) {
        sockfd_admin_mtx.lock();
        while (!sockfd_admin_queue.empty()) {
            event.data.fd = sockfd_admin_queue.front();
            sockfd_admin_queue.pop();
            if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event.data.fd, &event) != 0) {
                warn("Fail to add fd to epoll.\n");
            }
        }
        sockfd_admin_mtx.unlock();
        int ret = epoll_wait(epoll_fd, &event, 1, 100);
        if (ret < 0) {
            warn("epoll_wait error.\n");
        }
        if (ret > 0) {
            Request request;
            if (request.http_read(event.data.fd)) {
                reply_admin(event.data.fd, request);
            }
            close(event.data.fd);
            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, event.data.fd, &event);
        }
    }
}

/* Open admin port connection and wait for any client connections*/
void* admin_console(void *arg) {
	debug("Admin console: 8600/admin\n");
	int listen_fd = tcp_server_socket(ADMIN_PORT);
	if(listen_fd < 0)
		exit(1);

	pthread_t work;
	pthread_create(&work, NULL, handle_admin_browser, NULL);
	pthread_detach(work);

    while(!dead){
		struct sockaddr_in clientaddr;
		socklen_t clientaddrlen = sizeof(clientaddr);
		int fd = accept(listen_fd, (struct sockaddr*)&clientaddr, &clientaddrlen);
		if(fd < 0){
			warn("Accept fails\n");
			continue;
		}
		sockfd_admin_mtx.lock();
		sockfd_admin_queue.push(fd);
		sockfd_admin_mtx.unlock();
	}
}
