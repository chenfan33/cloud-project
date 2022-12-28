#include <regex>

#include "parse.h"
#include "mail.h"
#include "mbox.h"
#include "server.h"

#include "../common/master_interface.h"
#include "../common/address_parse.h"
#include "../common/hash.h"

Server* smtp;

static int master_fd = -1;

std::string from_pattern = "MAIL FROM:<.+@.+>";
std::string to_pattern = "RCPT TO:<.+@.+>";
std::string local_pattern = "RCPT TO:<.+@localhost>";

std::regex from_regex = std::regex(from_pattern, std::regex::icase);
std::regex to_regex = std::regex(to_pattern, std::regex::icase);
std::regex local_regex = std::regex(local_pattern, std::regex::icase);

enum State{
	Init = 0,
	From = 1,
	To = 2,
	Data = 3
};

std::unordered_map<int, std::string> msg_map =
		{
				{220, "220 localhost service ready"},
				{221, "221 localhost Service closing transmission channel"},
				{250, "250 OK"},
				{354, "354 Start mail input"},
				{500, "500 Syntax error, command unrecognized"},
				{501, "501 Syntax error in parameters or arguments"},
				{503, "503 Bad sequence of commands"},
				{550, "550 No such user here"},
		};

void send_msg(int fd, int msg_code){
	if(msg_map.find(msg_code) == msg_map.end()){
		fprintf (stderr, "Cannot find message code %d\n", msg_code);
		exit(1);
	}

	std::string send_msg = msg_map[msg_code];
	if(!do_write(fd, send_msg))
		pthread_exit(NULL);
	return;
}

void* smtp_worker(void *arg){
	int comm_fd = *(int*)arg;
	send_msg(comm_fd, 220);

	std::smatch match;

	Mail mail;
	State state = Init;
	Address dst;

	while(true){
		std::string rev_msg;
		if(!do_read(comm_fd,rev_msg))
			pthread_exit(NULL);

		if(state == Data){
			if(rev_msg == "."){
				std::time_t t = std::time(NULL);
				mail.time = std::string(std::ctime(&t));

				for(auto recipient : mail.recipients){
					MBox mbox;
					std::string backend;
					if(usr_to_address(master_fd, recipient, backend)){
    					dst.init(backend);
						int fd = tcp_client_socket(dst);
						if (fd > 0 && mbox.init(fd, recipient)){
							mbox.write(mail);
							close(fd);
						}
					}
				}

				mail.clear();
				state = Init;
				send_msg(comm_fd, 250);
			}
			else{
				mail.text += rev_msg + "\n";
			}
			continue;
		}

		std::string command = rev_msg.substr(0, 4);

		/*
		 * HELO <domain>, which starts a connection.
		 */
		if(command == "HELO"){
			std::string domain = rev_msg.substr(5);
			std::string send_msg = "250 localhost";
			if(!do_write(comm_fd, send_msg))
				pthread_exit(NULL);
		}
		/*
		 * MAIL FROM:, which tells the server who the sender of the email is.
		 */
		else if(command == "MAIL"){
			if(state != Init){
				send_msg(comm_fd, 503);
			}
			else{
				if(std::regex_match(rev_msg, match, from_regex)){
					mail.usr = rev_msg.substr(11, rev_msg.size() - 12);
					state = From;
					send_msg(comm_fd, 250);
				}
				else{
					send_msg(comm_fd, 501);
				}
			}
		}
		/*
		 * RCPT TO:, which specifies the recipient.
		 */
		else if(command == "RCPT"){
			if(state != From && state != To){
				send_msg(comm_fd, 503);
			}
			else{
				if(std::regex_match(rev_msg, match, to_regex)){
					if(std::regex_match(rev_msg, match, local_regex)){
						std::string recipient = rev_msg.substr(9, rev_msg.size() - 20);
						mail.recipients.push_back(recipient);
						state = To;
						send_msg(comm_fd, 250);
					}
					else{
						send_msg(comm_fd, 550);
					}
				}
				else{
					send_msg(comm_fd, 501);
				}
			}
		}
		/*
		 * DATA, which is followed by the text of the email
		 * and then a dot (.) on a line by itself
		 */
		else if(command == "DATA"){
			if(state != To){
				send_msg(comm_fd, 503);
			}
			else{
				state = Data;
				send_msg(comm_fd, 354);
			}
		}
		/*
		 * QUIT, which terminates the connection.
		 */
		else if(command == "QUIT"){
			send_msg(comm_fd, 221);
			close(comm_fd);
			*(int*)arg = -1;
			if(debug)
				fprintf (stderr, "[%d] Connection closed\n", comm_fd);
			pthread_exit(NULL);
		}
		/*
		 * RSET, which aborts a mail transaction.
		 */
		else if(command == "RSET"){
			mail.clear();
			state = Init;
			send_msg(comm_fd, 250);
		}
		/*
		 * NOOP, which does nothing.
		 */
		else if(command == "NOOP"){
			send_msg(comm_fd, 250);
		}
		else{
			send_msg(comm_fd, 500);
		}
	}

	pthread_exit(NULL);
}

void sig_handler(int sig) {
	smtp->stop();
	exit(0);
}

int main(int argc, char *argv[])
{
	std::ios::sync_with_stdio(false); // to speed up

	Args args;
	args.port = 2500; // default port

	parse_args(argc, argv, args);
	debug = args.debug;

	Address dst;
    dst.init("127.0.0.1:10000");

    while((master_fd = tcp_client_socket(dst)) < 0);
	debug("Start SMTP server\n");
    
	smtp = new Server(args.port, smtp_worker);
	signal(SIGINT, sig_handler);
	smtp->start();

	return 0;
}