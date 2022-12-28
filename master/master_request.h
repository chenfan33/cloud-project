#ifndef REQUEST_H_
#define REQUEST_H_

#include <string>
#include <unordered_map>

#include "../common/file_operation.h"

const static std::string OK = "200 OK";

struct Response {
    std::string status_;
    std::map<std::string, std::string> headers_;
    std::string body_;

    std::string to_string(){
        headers_["Connection: "] = "close";

        std::string ret = " " + status_ + "\r\n";
        for (auto it = headers_.begin(); it != headers_.end(); ++it) {
            ret += it->first + it->second + "\r\n";
        }
        ret += "\r\n" + body_;
        return ret;
    }

    bool load_content(const std::string& status,
                      const std::string& content_type,
                      const std::string& content_file_path) {
        status_ = status;
        headers_["Content-Type: "] = content_type;
        if(!read_file(content_file_path.c_str(), body_)){
            warn("Cannot find file %s\n", content_file_path.c_str());
        }
        headers_["Content-Length: "] = std::to_string(body_.length());
        return true;
    }
};

struct Request{
    std::string username_;

    std::string method_;
    std::string path_;
    std::string http_version_;

    std::unordered_map<std::string, std::string> headers_;

    uint64_t content_length_;
    std::string body_;

    bool http_read(int fd){
        std::string line;
        if(!http_read_line(fd, line))
            return false;
        
        debug_v3("First line: %s\n", line.c_str());
        std::vector<std::string> vec;
        vec = split(line, ' ');
        if(vec.size() != 3)
            return false;
        
        method_ = vec[0];
        path_ = vec[1];
        http_version_ = vec[2];

        while(!line.empty()){
            if(line.substr(0, 16) == "Content-Length: "){
                content_length_ = atoi(line.substr(16).c_str());
            }

            if(!http_read_line(fd, line))
                return false;
            debug_v3("Other line: %s\n", line.c_str());
        }

        if(method_ == "POST" && content_length_ > 0){
            char* buf = new char [content_length_ + 1];
            if(!tcp_read(fd, buf, content_length_))
                return false;

            body_.assign(buf, buf + content_length_);
            buf[content_length_] = 0;
            delete [] buf;
            debug("HTTP body: %s\n", body_.c_str());
        }

        return true;
    }
};

#endif