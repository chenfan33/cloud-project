#ifndef EMAIL_H_
#define EMAIL_H_

#include "../common/hash.h"
#include "mail/relay.h"

#include "frontend_config.h"

bool send_local_mail(std::string receiver, Mail& mail){
    MBox mbox;
    std::string backend;
	if(usr_to_address(master_fd, receiver, backend)){
		Address dst;
    	dst.init(backend);

		int fd = tcp_client_socket(dst);
		if (fd > 0 && mbox.init(fd, receiver)){
            bool ret = mbox.write(mail);
            close(fd);
            return ret;
        }
	}
    return false;
}

bool delete_mail(std::string usr, uint64_t id){
    MBox mbox;
    std::string backend;
	if(usr_to_address(master_fd, usr, backend)){
		Address dst;
    	dst.init(backend);

		int fd = tcp_client_socket(dst);
		if (fd > 0 && mbox.init(fd, usr)){
            bool ret = mbox.del(id);
            close(fd);
            return ret;
        }
	}
    return false;
}

bool get_mails(std::string usr, std::vector<Mail>& mails){
    MBox mbox;
    std::string backend;
	if(usr_to_address(master_fd, usr, backend)){
		Address dst;
    	dst.init(backend);

		int fd = tcp_client_socket(dst);
		if (fd > 0 && mbox.init(fd, usr)){
            mails = mbox.mails;
            close(fd);
            return true;
        }
	}
    return false;
}

bool send_mails(std::string usr, std::vector<std::string> recipients,
        std::string text){
    for(auto recipient : recipients){
        Mail mail(0, usr, text);

        std::cout << recipient << std::endl;
        // If receiver is also in the local domain
        if(recipient.substr(recipient.size() - 10) == "@localhost"){
            std::string receiver = recipient.substr(0, recipient.size() - 10);
            if(!send_local_mail(receiver, mail))
                return false;
        }
        // If receiver is also in other domains
        else{
            size_t pos = recipient.find("@");
            if(pos == std::string::npos)
                return false;

            std::string receiver = recipient.substr(0, pos);
            std::string domain = recipient.substr(pos + 1);

            if(!send_remote_mail(receiver, domain, mail))
                return false;
        }
    }
    return true;
}

#endif