#ifndef REPLY_H_
#define REPLY_H_

#include <sys/time.h>

#include "../common/kv_interface.h"
#include "storage.h"

#include "request.h"
#include "login.h"

#include "storage_reply.h"
#include "email_reply.h"

/**
 * Initialize user by adding user's metadata to KVSTORE, 
 *     create connection with assigned backend server
*/
bool initialize_usr(std::string usr){
    std::string initial_metadata = "Latest ID: 1\nRoot folder: /" +
        usr + " ID 1 Items 0\n";
    debug("Writing initial metadata for user [%s]: %s\n",
        usr.c_str(), initial_metadata.c_str());
    
    std::string backend;
	if(usr_to_address(master_fd, usr, backend)){
		Address dst;
    	dst.init(backend);

		int fd = tcp_client_socket(dst);
		if (fd > 0){
            int ret = kv_puts(fd, usr, storage::kMetadataFp, initial_metadata);
            close(fd);
            return (ret >= 0);
        }
	}
    warn("Failed to connect to KV store for file handling.");
    return false;
}

uint64_t create_cookie(std::string username){
    uint64_t number = random_number();
    if(cookie_mp.size() > 1000){
        cookie_mp.clear();
    }
    while(cookie_mp.find(number) != cookie_mp.end()){
        number = random_number();
    }
    cookie_mp[number] = username;
    return number;
}

Response login_handler(Request& request){
    Response response;

    if (request.method_ == "GET") {
        response.load_content(OK, "text/html", ABSOLUTE_DIR + "/html/login.html");
    } else if (request.method_ == "POST") {
        std::vector<std::string> params = split(request.body_.c_str(), '&');
        std::string username = split(params.at(0), '=').at(1);
        std::string password = split(params.at(1), '=').at(1);
        int ret = sign_in(username, password);
        if (ret >= 0) {
            response.load_content(OK, "text/html", ABSOLUTE_DIR + "/html/home.html");
            uint64_t number = create_cookie(username);
            response.headers_[SET_COOKIE] = "sessionid=" + std::to_string(number);
        }         
        else {
            std::string err_msg = err_mp[ret];
            response.load_content(OK, "text/html", ABSOLUTE_DIR + "/html/error_msg.html");
            response.body_ = replace(response.body_, "$error", err_msg);
            response.body_ = replace(response.body_, "$redirect_path", "login");
            warn("Fail to log in\n");
        }
    }

    return response;
}

Response signup_handler(Request& request){
    Response response;

    if (request.method_ == "GET") {
        response.load_content(OK, "text/html", ABSOLUTE_DIR + "/html/signup.html");
    } else if (request.method_ ==  "POST") {
        std::vector<std::string> params = split(request.body_.c_str(), '&');
        std::string username = split(params.at(0), '=').at(1);
        std::string password = split(params.at(1), '=').at(1);
        int ret = sign_up(username, password);

        if (ret >= 0 && initialize_usr(username)) {
            response.status_ = REDIRECT;
            response.body_ = "HTTP/1.1 302 Found\r\nLocation: http://localhost:" + std::to_string(port) + 
                "/login\r\n\r\n";
        } 
        else {
            std::string err_msg = err_mp[ret];
            response.load_content(OK, "text/html", ABSOLUTE_DIR + "/html/error_msg.html");
            response.body_ = replace(response.body_, "$error", err_msg);
            response.body_ = replace(response.body_, "$redirect_path", "signup");
            warn("Fail to sign up\n");
        }
    }

    return response;
}

/**
 * For change password usage
*/
Response user_handler(Request& request){
    Response response;
    if(request.username_ == ""){
        response.status_ = REDIRECT;
        response.body_ = "HTTP/1.1 302 Found\r\nLocation: http://localhost:" + std::to_string(port) + 
                "/login\r\n\r\n";
    }
    else{
        if (request.method_ ==  "POST"){
            std::vector<std::string> params = split(request.body_.c_str(), '&');
            std::string old_pwd = split(params.at(0), '=').at(1);
            std::string new_pwd = split(params.at(1), '=').at(1);
            int ret = change_pwd(request.username_, old_pwd, new_pwd);

            if (ret >= 0) {
                response.status_ = REDIRECT;
                response.body_ = "HTTP/1.1 302 Found\r\nLocation: http://localhost:" + std::to_string(port) + 
                "/login\r\n\r\n";
                return response;
            }
            else {
                std::string err_msg = "FAILED TO CHANGE PASSWORD";
                response.load_content(OK, "text/html", ABSOLUTE_DIR + "/html/error_msg.html");
                response.body_ = replace(response.body_, "$error", err_msg);
                response.body_ = replace(response.body_, "$redirect_path", "");
                warn("Fail to change password\n");
                return response;
            }
        }
        response.load_content(OK, "text/html",
            ABSOLUTE_DIR + "/html" + request.path_ + ".html");
        response.body_ = replace(response.body_, "$username", request.username_);
        response.headers_[CONTENT_LEN] = std::to_string(response.body_.length());
    }
    return response;
}

Response home_handler(Request& request){
    Response response;
    if(request.username_ == ""){
        response.status_ = REDIRECT;
        response.body_ = "HTTP/1.1 302 Found\r\nLocation: http://localhost:" + std::to_string(port) + 
                "/login\r\n\r\n";
    }
    else
        response.load_content(OK, "text/html",
            ABSOLUTE_DIR + "/html" + request.path_ + ".html");
    return response;
}

Response about_handler(Request& request){
    Response response;
    response.load_content(OK, "text/html",
            ABSOLUTE_DIR + "/html" + request.path_ + ".html");
        response.body_ = replace(response.body_, "$username", request.username_);
        response.headers_[CONTENT_LEN] = std::to_string(response.body_.length());
    return response;
}

typedef Response (*func)(Request&);

std::unordered_map<std::string, func> func_mp = {
    {"/", login_handler},
    {"/login", login_handler},
    {"/signup", signup_handler},
    {"/home", home_handler},
    {"/user", user_handler},
    {"/email_inbox", email_inbox_handler},
    {"/about", about_handler},
};

void reply(int fd, Request& request){
    Response response;

    if(func_mp.find(request.path_) != func_mp.end()){
        response = func_mp[request.path_](request);
    }
    else{
        if (request.path_.length() >= 5 && request.path_.substr(0, 5) == "/file") {
            if(request.username_ == ""){
                response.status_ = REDIRECT;
                response.body_ = "HTTP/1.1 302 Found\r\nLocation: http://localhost:" + std::to_string(port) + 
                "/login\r\n\r\n";
            }
            else
                response = storage_handler(request);
        } else if (request.path_.length() >= 14 && request.path_.substr(0, 14) == "/email_display") {
            if(request.username_ == ""){
                response.status_ = REDIRECT;
                response.body_ = "HTTP/1.1 302 Found\r\nLocation: http://localhost:" + std::to_string(port) + 
                "/login\r\n\r\n";
            }
            else
                response = email_display_handler(request);
        } else if (request.path_.find(".js") != std::string::npos) {
            response.load_content(OK, "text/javascript", ABSOLUTE_DIR + "/html" + request.path_);
        } else if (request.path_.find(".css") != std::string::npos) {
            response.load_content(OK, "text/css", ABSOLUTE_DIR + "/html" + request.path_);
        } else if (request.path_.find(".png") != std::string::npos) {
            response.load_content(OK, "image/png", ABSOLUTE_DIR + request.path_);
        } else {
            response.load_content(OK, "text/html", ABSOLUTE_DIR + "/html/error_page.html");
        }
    }

    if(response.status_ == REDIRECT){
        debug("Redirect: %s\n", response.body_.c_str());
        http_write_request(fd, response.body_);
    }
    else{
        std::string body = request.http_version_ + response.to_string();
        http_write_request(fd, request.http_version_ + body);
    }
}

#endif