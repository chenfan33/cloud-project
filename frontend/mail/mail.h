#ifndef MAIL_H_
#define MAIL_H_

#include <vector>
#include <string>

struct Mail{
	uint64_t id;
	std::string usr; // sender of the email
	std::string time; 
	std::string text; // content of the email

	Mail(uint64_t _id = 0, std::string _usr = "", std::string _text = "") : 
		id(_id), usr(_usr), text(_text){
		std::time_t t = std::time(NULL);
		time = std::string(std::ctime(&t));
	}

	void clear(){
		id = 0;
		usr = text = "";
	}

	std::string get_header(){
		std::string header = text.substr(0, 25);
		std::replace(header.begin(), header.end(), '\n', ' '); 
		if (header.length() <= 25)
			header += "...";
		return header;
	}

	bool empty(){
		return (id == 0 && usr == "" && text == "");
	}

	std::string to_local_string(){
		std::string ret;
		if (usr.find('@') != std::string::npos)
			ret = "From <" + usr + "> ";
        else 
			ret = "From <" + usr + "@localhost> ";
		return ret + time + std::to_string(id) + "\n" + text;
	}

	std::string to_remote_string(){
		std::string ret;
		if (usr.find('@') != std::string::npos)
			ret = "From <" + usr + "> ";
        else 
			ret = "From <" + usr + "@localhost> ";
		return ret + time + text;
	}
};

#endif