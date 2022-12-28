#ifndef HTTP_OPERATION_H_
#define HTTP_OPERATION_H_

#include "debug_operation.h"
#include "msg_operation.h"
#include "tcp_operation.h"
#include <cctype>
#include <iomanip>
#include <sstream>

std::string http_parse(const std::string& value)
{
    std::string result;
    result.reserve(value.size());
    
    for (std::size_t i = 0; i < value.size(); ++i) {
        auto ch = value[i];
        
        if (ch == '%' && (i + 2) < value.size()) {
            auto hex = value.substr(i + 1, 2);
            auto dec = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            result.push_back(dec);
            i += 2;
        }
        else if (ch == '+') {
            result.push_back(' ');
        }
        else {
            result.push_back(ch);
        }
    }
    return result;
}

bool http_write_request(int fd, std::string request){
    return tcp_write(fd, request.c_str(), request.size());
}

bool http_read_line(int fd, std::string& line){
    line = "";
    char c;
    int state = 0;

    while(state != 2){
        if(!tcp_read(fd, &c, sizeof(c)))
            return false;
        
        line += c;
        switch(state){
            case 0 : 
                if(c == '\r')
                    state = 1;
                break;
            case 1 :
                if(c == '\n'){
                    state = 2;
                }
                else{
                    state = 0;
                }
                break;
        }
    }
    line = line.substr(0, line.size() - 2);

    return true;
}

#endif