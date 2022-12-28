#ifndef MBOX_H_
#define MBOX_H_

#include <regex>
#include <resolv.h>

#include "../common/kv_interface.h"
#include "mail.h"

// Pattern to match the header of each email
static std::string header_pattern = "FROM <.+@.+> ... ... .. ........ ....";
static std::regex header_regex = std::regex(header_pattern, std::regex::icase);

struct MBox{
    int fd; // fd to the corresponding backend (KV store) server
    uint64_t current_id;

	std::string usr; // owner of the mbox

    // We put all emails in the same file (mbox)
    std::string text; // text of mbox
    std::vector<Mail> mails; // emails in mbox

    bool init(int _fd, std::string _usr){
        current_id = 0;
        fd = _fd;
        usr = _usr;
        // Get the mbox file from the backend (KV store) server
        auto state = kv_gets(fd, text, usr, "mbox");

        if(state == KEY_ERROR){
            text = "";
            warn("Cannot find mbox\n");
        }
        else if(state != FINISHED){
            warn("Cannot open mbox of %s in fd %d\n", usr.c_str(), fd);
            return false;
        }

        parse_mails();
        return true;
    }

    // Parse the text in mbox to emails
    void parse_mails(){
        Mail mail;

        std::stringstream ss(text);
        std::string line;

		std::smatch match;

		while(std::getline(ss, line, '\n')) {
			if(std::regex_match(line, match, header_regex)){
				if (!mail.empty()){
                    mails.push_back(mail);
                    //debug("Push mail: %s\n", mail.text.c_str());
                }

				mail.clear();

				mail.usr = line.substr(6, line.size() - 32);
				mail.time = line.substr(line.size() - 24) + "\n";

                std::getline(ss, line, '\n');
                mail.id = std::stoull(line);
                current_id = std::max(mail.id, current_id);
		    }
			else if(!mail.empty()){
				mail.text += line + "\n";
			}
		}

		if (!mail.empty()){
            mails.push_back(mail);
            //debug("Push mail: %s\n", mail.text.c_str());
        }
    }
    
    bool write(Mail& mail){
        mail.id = current_id + 1;
        current_id += 1;
        // Write an email to the end of the mbox file
        std::string post_text = text + mail.to_string();
        // Write the mbox file back to backend (KV store) server
        auto state = kv_cput(fd, usr, "mbox", text, post_text);
        while(state != FINISHED && state != LINK_ERROR){
            warn("CPUT mbox fails. Reload the mbox.\n ");
            if(init(fd, usr) < 0)
                return false;
            post_text = text + mail.to_string();
            state = kv_cput(fd, usr, "mbox", text, post_text);
        }

        return (state == FINISHED);
    }
};

#endif