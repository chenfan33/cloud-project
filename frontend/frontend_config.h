#ifndef FRONTEND_CONFIG_H_
#define FRONTEND_CONFIG_H_

#include <queue>
#include <mutex>
#include <random>

#include "../common/address_parse.h"
#include "../common/kv_interface.h"
#include "../common/proto_gen/proto.pb.h"

#define ABSOLUTE_DIR std::string("/home/cis5050/git/T01/frontend")

static int port = -1;
static Address my_addr;

// Whether the server is killed
static bool dead = false;

// mutex corresponding to sockfd_queue
static std::mutex sockfd_mtx;
// queue of fd to frontend servers
static std::queue<int> sockfd_queue;

static int master_fd = -1;

static std::random_device rd;
static std::mt19937_64 random_number(rd());
static std::unordered_map<uint64_t, std::string> cookie_mp;

const static std::string CONTENT_TYPE = "Content-Type: ";
const static std::string CONTENT_LEN = "Content-Length: ";
const static std::string CONTENT_DISPOSITION = "Content-Disposition: ";
const static std::string COOKIE = "Cookie: ";
const static std::string SET_COOKIE = "Set-Cookie: ";
const static std::string OK = "200 OK";
const static std::string REDIRECT = "302 Found";
const static int INVALID_NAME_ERR = -10;
const static int WRONG_PASS_ERR = -9;
const static int LINK_ERR = -1;
static std::unordered_map<int, std::string> err_mp = {
    {LINK_ERR, "CONNECTION ERROR"},
    {USER_ERROR, "USER DOES NOT EXIST"},
    {KEY_ERROR, "ITEM DOES NOT EXIST"},
    {VALUE_ERROR, "VALUE EXISTED"},
    {WRONG_PASS_ERR, "WRONG PASSWORD"},
    {INVALID_NAME_ERR, "INVALID USERNAME"},
    {SEQ_ERROR, "TRANSACTION FAILED"}

};

const static std::string FORWARD_SEPERATOR = "\n --------- FORWARD MESSAGE --------- \n";
const static std::string REPLY_SEPERATOR = "\n --------- REPLY MESSAGE --------- \n";
const static std::string EMAIL_SUFFIX = "@localhost";
const static char *CONTENT_FORMAT = "<p><span class=\"u-text-body-color\">%s</span></p>";
const static char *TABLE_FORMAT = "<tr><td class=\"name\">%s</a></td><td class=\"subjet\"><a href=\"email_display?index=%d\">%s</a></td><td class=\"time\">%s</td></tr>";

#endif