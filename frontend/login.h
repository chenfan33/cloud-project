#ifndef LOGIN_H_
#define LOGIN_H_

#include "frontend_config.h"
#include "../common/hash.h"

bool valid_userchar(const char c){
	if(c <= 0x20 || c > 0x7e)
		return false;
	if(c == '@' || c == '\\' || c == '/' || c == '$' || c == '%' || c == '&' || c == '=')
		return false;
	return true;
}

bool valid_username(std::string usr){
	for(char c : usr){
		if(!valid_userchar(c))
			return false;
	}
	return true;
}

int change_pwd(std::string usr, std::string old_pwd, std::string new_pwd){
	if(!valid_username(usr))
		return INVALID_NAME_ERR;
    std::string backend;
	int ret = LINK_ERR;
	if(usr_to_address(master_fd, usr, backend)){
		Address dst;
    	dst.init(backend);

		int fd = tcp_client_socket(dst);
		if (fd > 0){
            ret = kv_cput(fd, usr, "pwd", old_pwd, new_pwd);
			close(fd);
        }
	}
    return ret;
}

int sign_up(std::string usr, std::string pwd){
    return change_pwd(usr, "", pwd);
}

int sign_in(std::string usr, std::string pwd){
	if(!valid_username(usr))
		return INVALID_NAME_ERR;
    std::string backend;
	int ret = LINK_ERR;
	if(usr_to_address(master_fd, usr, backend)){
		Address dst;
    	dst.init(backend);

		int fd = tcp_client_socket(dst);
		if (fd > 0){
            std::string password;
            ret = kv_gets(fd, password, usr, "pwd");
			close(fd);

			
            if ((ret >= 0)){
				if (password == pwd)
					return 1;
				else 
					return WRONG_PASS_ERR;
			}
        }
	}
    return ret;
}

#endif