#ifndef MASTER_CONFIG_H_
#define MASTER_CONFIG_H_

#include <queue>
#include <unordered_set>
#include <unordered_map>

#include "../common/address_parse.h"
#include "../common/proto_gen/proto.pb.h"

#define MASTER_PORT 10000
#define ADMIN_PORT 8600

#define ABSOLUTE_DIR std::string("/home/cis5050/git/T01/common/")
#define ABSOLUTE_FRONTEND_DIR std::string("/home/cis5050/git/T01/frontend")
#define BACKEND_PATH (ABSOLUTE_DIR + "backend.txt")

static bool dead = false;

static std::mutex sockfd_mtx;
static std::queue<int> sockfd_queue;

static std::mutex sockfd_admin_mtx;
static std::queue<int> sockfd_admin_queue;

static std::unordered_map<std::string, std::string> kvs;

const static char *FRONTEND_FORMAT = "<tr><td>%d</td><td>%s</td><td>%s</td></tr>";
const static char *BACKEND_ALIVE_FORMAT = "<tr><td>%d</td><td>%s</td><td>Alive</td>"
"<td><form method=\"post\" enctype=\"multipart/form-data\">"
"<button class=\"button\" name=\"KILL\" value=\"%s\">&nbsp; &nbsp; Kill &nbsp; &nbsp;"
"</button></form></td>";
const static char *BACKEND_DEAD_FORMAT = "<tr><td>%d</td><td>%s</td><td>Dead</td>"
"<td><form method=\"post\" enctype=\"multipart/form-data\">"
"<button class=\"button\" name=\"RESTART\" value=\"%s\">Restart</button></form></td></tr>";
const static char *BACKEND_CRASH_FORMAT = "<tr><td>%d</td><td>%s</td><td>Crash</td></tr>";
const static char *RAW_TABLE_FORMAT = "<tr><td>%d</td><td>%s</td>"
"<td><div class=\"scrollable\">%s</div></td></tr>";

#endif