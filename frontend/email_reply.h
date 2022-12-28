#ifndef EMAIL_REPLY_H_
#define EMAIL_REPLY_H_

#include "./mail/mail.h"
#include "email.h"
#include "email_reply.h"
#include "storage_reply.h"
#include "frontend_config.h"
#include "../common/file_operation.h"

static int to_be_deleted;

/* Helper function for displaying email list in email_inbox list
    Return substituted html result */
std::string email_list_construct(std::vector<Mail> list) {
    std::string result = "";
    for (int i = 0; i < list.size(); i++) {
        char row[1000];
        sprintf(row, TABLE_FORMAT, list[i].usr.c_str(), list[i].id, 
                list[i].get_header().c_str(), 
                list[i].time.c_str());
        std::string s = row;
        result += s;
        s.clear();
        bzero(row, sizeof(row));
    }
    return result;
}

/* Helper function for displaying email list in email_inbox list
    Retrieve mails for the user, and return the response body */
void display_email_list(std::string usr, 
                        Response& response) {
    std::string html;
    read_file(ABSOLUTE_DIR + "/html/email_inbox.html", html);
    std::vector<Mail> mails;
    get_mails(usr, mails);
    std::string table_content = email_list_construct(mails);
    html = replace(html, "$table", table_content);
    response.body_ = html;
    response.headers_[CONTENT_LEN] = std::to_string(response.body_.length());
}

/* Handle all requests for email_inbox page
   Operations: compose and view each email */
Response email_inbox_handler(Request& req) {
    Response response;
    std::string usr = req.username_;

    if(usr == ""){
        response.status_ = REDIRECT;
        response.body_ = "HTTP/1.1 302 Found\r\nLocation: http://localhost:" + std::to_string(port) + 
                "/login\r\n\r\n";
        return response;
    }

    response.status_ = OK;
    response.headers_[CONTENT_TYPE] = "text/html";

    if (req.method_.compare("GET") == 0) {
        display_email_list(usr, response);
    }
    else {
        std::vector<std::string> vec;
        vec = split(req.body_, '\r');
        if (vec[1].find("name=\"WRIT\"") != std::string::npos) {
            std::string recipients = vec[7].substr(1, vec[7].length());
            std::string msg_content;
            for (int i = 11; i < vec.size() - 2; i++) {
                msg_content += vec[i];
            }
            std::vector<std::string> recipient_list;
            recipient_list = split(recipients, ',');
            for (int i = 0; i < recipient_list.size(); i++) {
                recipient_list[i] = remove_leading_space(recipient_list[i]);
            }
            msg_content += "\r\n";
            bool send_success = send_mails(usr, recipient_list, msg_content);
            debug("%d, SEND MAIL: \n <FROM>%s\n <TO>%s\n <MSG>%s\n", 
                send_success, usr.c_str(), 
                recipients.c_str(), msg_content.c_str());
            if(send_success){
                response.status_ = REDIRECT;
                response.body_ = "HTTP/1.1 302 Found\r\nLocation: http://localhost:" + std::to_string(port) + 
                        "/email_inbox\r\n\r\n";
            }
        } else if (vec[1].find("name=\"DELE\"") != std::string::npos) {
            debug("DELETE EMAIL: %d\n", to_be_deleted);
            if(delete_mail(usr, to_be_deleted)){
                response.status_ = REDIRECT;
                response.body_ = "HTTP/1.1 302 Found\r\nLocation: http://localhost:" + std::to_string(port) + 
                        "/email_inbox\r\n\r\n";
            }
        }
    }
    return response;
}


/* Helper function for displaying one email detail(email_display page)
    Return concatenated email content */
std::string email_content_construct(const std::string& content) {
    std::string result = "";
    std::vector<std::string> tokens = split(content, '\n');
    for (std::string t : tokens) {
        char line[1000];
        sprintf(line, CONTENT_FORMAT, t.c_str());
        std::string s = line;
        result += s;
    }
    return result;
}

/* Helper function for displaying one email detail(email_display page)
    Return formatted html with email sender, time and content */
void display_email_detail(Mail displayed_email, Response& response) {
    std::string html;
    read_file(ABSOLUTE_DIR + "/html/email_display.html", html);

    html = replace(html, "$sender", displayed_email.usr);
    html = replace(html, "$time", displayed_email.time);
    html = replace(html, "$content", email_content_construct(displayed_email.text));
    response.body_ = html;
    response.headers_[CONTENT_LEN] = std::to_string(response.body_.length());
}

/* Main function for displaying one email detail(email_display page)
    Operation supported: compose, reply, forward, delete
 */
Response email_display_handler(Request& req) {
    Response response;
    std::string usr = req.username_;
    response.status_ = OK;
    response.headers_[CONTENT_TYPE] = "text/html";

    int idx_start = req.path_.find("=");
    int email_index = stoi(req.path_.substr(idx_start + 1, req.path_.length() - idx_start));
    to_be_deleted = email_index;
    std::vector<Mail> email_list;
    get_mails(req.username_, email_list);
    Mail displayed_email;
    for (auto obj: email_list) {
        if (obj.id == email_index)
            displayed_email = obj;
    }
    
    if (req.method_.compare("GET") == 0) {
        display_email_detail(displayed_email, response);
    } else {
        std::vector<std::string> vec;
        vec = split(req.body_, '\r');
        std::string recipients;
        std::string msg_content;

        // extract recipients, email content from the http request
        if (vec[1].find("name=\"WRIT\"") != std::string::npos) {
            recipients = vec[7].substr(1, vec[7].length());
            for (int i = 11; i < vec.size() - 2; i++) {
                msg_content += vec[i];
            }
        } else if (vec[1].find("name=\"FORW\"") != std::string::npos) {
            recipients = vec[7].substr(1, vec[7].length());
            for (int i = 11; i < vec.size() - 2; i++) {
                msg_content += vec[i];
            }
            msg_content = msg_content + FORWARD_SEPERATOR + 
                    "From: " + displayed_email.usr + 
                    "\n Date: " + displayed_email.time + 
                    "\n " + displayed_email.text;
        } else if (vec[1].find("name=\"REPL\"") != std::string::npos) {
            if (displayed_email.usr.find('@') != std::string::npos)
                recipients = displayed_email.usr;
            else 
                recipients = displayed_email.usr + EMAIL_SUFFIX;
            for (int i = 7; i < vec.size() - 2; i++) {
                msg_content += vec[i];
            }
            msg_content = msg_content + REPLY_SEPERATOR + 
                    "From: " + displayed_email.usr + 
                    "\n Date: " + displayed_email.time + 
                    "\n " + displayed_email.text;
        } 

        // Parse recipients and send emails to backend server
        std::vector<std::string> recipient_list;
        recipient_list = split(recipients, ',');
        for (int i = 0; i < recipient_list.size(); i++) {
            recipient_list[i] = remove_leading_space(recipient_list[i]);
        }
        msg_content += "\r\n";
        bool send_success = send_mails(usr, recipient_list, msg_content);
        debug("%d, SEND MAIL: \n <FROM>%s\n <TO>%s\n <MSG>%s\n", 
            send_success, usr.c_str(), 
            recipients.c_str(), msg_content.c_str());
    }
    return response;
}

#endif