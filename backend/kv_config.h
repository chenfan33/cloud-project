#ifndef KV_CONFIG_H_
#define KV_CONFIG_H_

#include <queue>
#include <mutex>
#include <time.h>

#include "../common/kv_interface.h"
#include "../common/address_parse.h"
#include "../common/proto_gen/proto.pb.h"

// Whether the server is killed
static bool dead = false;
static bool init = false;

// Which folder the data should be put
static std::string PREFIX = "/home/cis5050/";

// mutex corresponding to sockfd_queue
static std::mutex sockfd_mtx;
// queue of fd to frontend servers
static std::queue<int> sockfd_queue;

static int port = -1;
static Address my_addr;

// Need to close master_fd after restart/skill
static int master_fd = -1;

// Need to set to false after restart/kill
static bool isPrimary;
// Need to set to true after restart/kill
static bool killed = false;

// DO NOT reset after kill/restart
static int max_sequence = 0;
// Need to reset after kill/restart
static std::vector<Address> secondary;
// Need to reset after kill/restart
static std::vector<int> fds;

static int CHECKPOINT_PERIOD = 5;
static clock_t last_checkpoint_time;


#endif
 