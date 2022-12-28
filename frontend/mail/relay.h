#ifndef RELAY_H_
#define RELAY_H_

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "mbox.h"

// We have to use a real domain to send the email out
#define DOMAIN std::string("pku.edu.cn")

int mail_response(int fd, std::string& response) {
    char buf[1000] = {0};
    int ret = read(fd, buf, 1000);
    response = buf;
    return ret;
}

bool DNS_Query(std::string domain, std::vector<std::string>& servers){
    unsigned char buf[1000];
    int length = res_query(domain.c_str(), C_IN, T_MX, buf, 1000);
    if(length < 0)
        return false;

    ns_msg msg;
    if (ns_initparse(buf, length, &msg) < 0)
        return false;

    int n = ns_msg_count(msg, ns_s_an);

    for (int i = 0; i < n; i++) {
        ns_rr rr;
        if(ns_parserr(&msg, ns_s_an, i, &rr) < 0)
            continue;

        char parse[1000] = {0};
        ns_sprintrr(&msg, &rr, NULL, NULL, parse, 1000);

        auto rdata = ns_rr_rdata(rr);
        auto priority = ns_get16(rdata);
        char _server[NS_MAXDNAME] = {0};
        if(dn_expand(buf, buf + length, rdata + 2, _server, NS_MAXDNAME) < 0)
            continue;

        servers.push_back(_server);
    }

    return (servers.size() != 0);
}

int connect_remote(std::vector<std::string>& servers){
    int fd = -1;

    for(auto server : servers){
        auto result = gethostbyname(server.c_str());
        if (!result)
            continue;

        auto addr_list = (struct in_addr **)result->h_addr_list;
        for(uint32_t i = 0; addr_list[i] != NULL; i++) {

            fd = socket(AF_INET, SOCK_STREAM, 0);
            if(fd < 0)
                continue;
            
            struct sockaddr_in addr;
            bzero(&addr, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(25);
            addr.sin_addr = *addr_list[i];

            if(connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
                warn("Connect fail\n");
                close(fd);
                fd = -1;
                continue;
            }
            else{
                std::string helo = "HELO " + DOMAIN + "\r\n";
                if(!tcp_write(fd, helo.c_str(), helo.size())){
                    close(fd);
                    fd = -1;
                    continue;
                }

                std::string response = "";
                if(mail_response(fd, response) < 0 ||
                        response.find("250") == std::string::npos){
                    close(fd);
                    fd = -1;
                    continue;
                }
                else{
                    debug("Response: %s\n", response.c_str());
                    return fd;
                }
            }
        }
    }
    
    return fd;
}

bool quit_email(int fd){
    std::string command = "QUIT\r\n";
    if(!tcp_write(fd, command.c_str(), command.size())){
        close(fd);
        warn("Write email commands (%s) error\n", command.c_str());
        return false;
    }

    std::string response = "";
    if(mail_response(fd, response) < 0 ||
            response.find("221") == std::string::npos){
        close(fd);
        warn("Read email response (%s) error\n", command.c_str());
        return false;
    }

    debug("Command: %s\n", command.c_str());
    debug("Response: %s\n", response.c_str());

    return true;
}


// Send the email to other domain (e.g., seas.upenn.edu)
bool send_remote_mail(std::string receiver, std::string domain, Mail& mail){
    std::vector<std::string> servers;
    // Get server names of the domain according to DNS query
    if(!DNS_Query(domain, servers))
        return false;

    // Connect to one of the server (any one that can work)
    int fd = connect_remote(servers);
    if(fd < 0)
        return false;

    std::vector<std::string> commands = 
        {
            "MAIL FROM:<" + mail.usr + "@" + DOMAIN + ">\r\n",
            "RCPT TO:<" + receiver + "@" + domain + ">\r\n",
            "DATA\r\n",
            mail.to_remote_string() + "\r\n.\r\n"
        };

    std::vector<std::string> expected_codes = 
        {"250", "250","354","250"};

    // Send commands to the server
    for(uint32_t i = 0;i < commands.size();++i){
        if(!tcp_write(fd, commands[i].c_str(), commands[i].size())){
            warn("Write email commands (%s) error\n", commands[i].c_str());
            quit_email(fd);
            return false;
        }

        std::string response = "";
        if(mail_response(fd, response) < 0 ||
                response.find(expected_codes[i]) == std::string::npos){
            warn("Read email response (%s) error\n", commands[i].c_str());
            quit_email(fd);
            return false;
        }

        debug("Command: %s\n", commands[i].c_str());
        debug("Response: %s\n", response.c_str());
    }
    
    quit_email(fd);
    return true;
}

#endif